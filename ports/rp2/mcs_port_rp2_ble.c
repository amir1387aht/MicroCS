/*
 * MicroCS - Pico W / Pico 2 W Bluetooth LE backend: BTstack on the CYW43439 (pico-sdk >= 1.5.1,
 * lib/btstack + lib/cyw43-driver submodules). -DMICROCS_BLE=ON with -DPICO_BOARD=pico_w / pico2_w;
 * docs/BLE.md.
 *
 * Threads: BTstack runs on the cyw43 async context (a FreeRTOS task with MICROCS_OS=freertos,
 * a low-priority interrupt without an OS). Every call into BTstack from the VM holds the
 * async-context lock (cyw43_thread_enter); BTstack's callbacks run under that lock and forward
 * everything through mcs_ble_on_*(), whose queue a critical section guards. Connection handles
 * given to the front end are slot + 1 (HCI handles are the controller's).
 *
 * The local GATT database is rebuilt with att_db_util (MCS_RP2_BLE_DB_SIZE bytes) and installed
 * with att_set_db(); all values are dynamic (read / written through the front end). Bonds live
 * in BTstack's TLV store in the last 8 KB of flash (the file system ends below it).
 */
#include "mcs_port_rp2.h"
#if MCS_RP2_BLE
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/critical_section.h"
#include "btstack.h"
#include "mcs_ble.h"
#include "mcs_hal.h"

#define B_MAXC MAX_NR_HCI_CONNECTIONS
#define B_MAXH (8 + MCS_BLE_MAX_SERVICES + MCS_BLE_MAX_ATTRS * 3)   /* attribute handles we map */
#define LOCK() cyw43_thread_enter()
#define UNLOCK() cyw43_thread_exit()

static critical_section_t g_cs;
static volatile uint8_t g_on, g_working;
static char g_name[32] = MCS_BLE_NAME;
static int32_t g_opt[MCS_BLE_OPT__COUNT];
static hci_con_handle_t g_hci[B_MAXC];               /* slot -> HCI handle (HCI_CON_HANDLE_INVALID = free) */
static btstack_packet_callback_registration_t g_hci_reg, g_sm_reg;

/* ---------------------------------------------------------------- helpers */
static int slot_of(hci_con_handle_t h) {
    for (int i = 0; i < B_MAXC; i++) if (g_hci[i] == h) return i;
    return -1;
}
static int connh(hci_con_handle_t h) { int s = slot_of(h); return s < 0 ? -1 : s + 1; }
static hci_con_handle_t hci_of(int conn) { return conn >= 1 && conn <= B_MAXC ? g_hci[conn - 1] : HCI_CON_HANDLE_INVALID; }
static int st(int rc) {                    /* BTstack status -> MCS_HAL_E* */
    switch (rc) {
    case ERROR_CODE_SUCCESS: return 0;
    case ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER: return MCS_HAL_ENODEV;
    case ERROR_CODE_COMMAND_DISALLOWED: case BTSTACK_ACL_BUFFERS_FULL: case GATT_CLIENT_BUSY:
    case ATT_HANDLE_VALUE_INDICATION_IN_PROGRESS: case BTSTACK_MEMORY_ALLOC_FAILED: return MCS_HAL_EBUSY;
    case GATT_CLIENT_IN_WRONG_STATE: case GATT_CLIENT_NOT_CONNECTED: return MCS_HAL_ENODEV;
    case GATT_CLIENT_VALUE_TOO_LONG: case ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS: return MCS_HAL_EINVAL;
    case ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE: return MCS_HAL_ENOTSUP;
    default: return MCS_HAL_ERR;
    }
}
static int att_st(uint8_t e) { return e ? MCS_BLE_ATT_ERR(e) : 0; }
static int att_code(int rc) { return MCS_BLE_IS_ATT_ERR(rc) ? (int)MCS_BLE_ATT_CODE(rc) : (int)ATT_ERROR_UNLIKELY_ERROR; }   /* -> ATT error byte */
static void addr_in(mcs_ble_addr_t* o, int type, const bd_addr_t a) { o->type = (uint8_t)type; memcpy(o->a, a, 6); }  /* both MSB first */
/* mcs_ble_uuid_t is little-endian (on air); BTstack's uuid128 arrays are big-endian */
static void uuid_in(mcs_ble_uuid_t* o, uint16_t u16, const uint8_t* be128) {
    memset(o, 0, sizeof *o);
    if (u16) { mcs_ble_uuid16(o, u16); return; }
    o->len = 16;
    for (int i = 0; i < 16; i++) o->b[i] = be128[15 - i];
}
static uint16_t uuid_out(const mcs_ble_uuid_t* u, uint8_t be128[16]) {   /* 16-bit value, or 0 + be128 */
    uint16_t s = mcs_ble_uuid_short(u);
    if (s) return s;
    if (u->len == 4) {                     /* 32-bit -> the Bluetooth base UUID */
        static const uint8_t base[16] = { 0, 0, 0, 0, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB };
        memcpy(be128, base, 16);
        for (int i = 0; i < 4; i++) be128[3 - i] = u->b[i];
        return 0;
    }
    for (int i = 0; i < 16; i++) be128[i] = u->b[15 - i];
    return 0;
}
static bool wait_flag(volatile uint8_t* f, uint32_t ms) {   /* called without the lock */
    for (uint32_t t = 0; !*f && t < ms; t += 5) sleep_ms(5);
    return *f != 0;
}

/* ---------------------------------------------------------------- GATT server (dynamic database) */
#ifndef MCS_RP2_BLE_DB_SIZE
#define MCS_RP2_BLE_DB_SIZE MAX_ATT_DB_SIZE
#endif
enum { H_NONE = 0, H_VALUE, H_CCCD, H_NAME, H_APPEARANCE };
static uint8_t g_hkind[B_MAXH];
static uint16_t g_hid[B_MAXH];
static uint8_t g_cccd[B_MAXC][MCS_BLE_MAX_ATTRS];
static struct { uint16_t handle; uint16_t len; int conn; uint8_t buf[MCS_BLE_MAX_VALUE]; } g_prep;   /* prepared (long) write */
static size_t attr_size(uint16_t u16, size_t data) { return 6 + (u16 ? 2 : 16) + data; }

static uint16_t att_read_cb(hci_con_handle_t con, uint16_t h, uint16_t offset, uint8_t* buf, uint16_t size) {
    if (h >= B_MAXH) return 0;
    int s = slot_of(con);
    switch (g_hkind[h]) {
    case H_NAME: return att_read_callback_handle_blob((const uint8_t*)g_name, (uint16_t)strlen(g_name), offset, buf, size);
    case H_APPEARANCE: return att_read_callback_handle_little_endian_16((uint16_t)g_opt[MCS_BLE_OPT_APPEARANCE], offset, buf, size);
    case H_CCCD: return att_read_callback_handle_little_endian_16(s >= 0 ? g_cccd[s][g_hid[h]] : 0, offset, buf, size);
    case H_VALUE: {
        if (!buf) { int n = mcs_ble_attr_len(g_hid[h]); return n > 0 ? (uint16_t)n : 0; }
        int n = mcs_ble_attr_read(g_hid[h], s + 1, offset, buf, size, NULL);
        if (n < 0) return (uint16_t)(ATT_READ_ERROR_CODE_OFFSET + att_code(n));
        return (uint16_t)n;
    }
    default: return 0;
    }
}
static int att_write_cb(hci_con_handle_t con, uint16_t h, uint16_t mode, uint16_t offset, uint8_t* buf, uint16_t size) {
    int s = slot_of(con);
    if (mode == ATT_TRANSACTION_MODE_VALIDATE) return 0;
    if (mode == ATT_TRANSACTION_MODE_CANCEL) { g_prep.handle = 0; return 0; }
    if (mode == ATT_TRANSACTION_MODE_EXECUTE) {
        if (!g_prep.handle || g_prep.handle != h) return 0;
        int rc = mcs_ble_attr_write(g_hid[h], g_prep.conn, 0, g_prep.buf, g_prep.len);
        g_prep.handle = 0;
        return rc < 0 ? att_code(rc) : 0;
    }
    if (h >= B_MAXH || s < 0) return ATT_ERROR_UNLIKELY_ERROR;
    if (g_hkind[h] == H_CCCD) {
        if (size < 2) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        uint8_t f = (uint8_t)(little_endian_read_16(buf, 0) & (MCS_BLE_SUB_NOTIFY | MCS_BLE_SUB_INDICATE));
        g_cccd[s][g_hid[h]] = f;
        mcs_ble_on_subscribe(s + 1, g_hid[h], f);
        return 0;
    }
    if (g_hkind[h] != H_VALUE) return ATT_ERROR_WRITE_NOT_PERMITTED;
    if (mode == ATT_TRANSACTION_MODE_ACTIVE) {          /* a part of a long write: kept until Execute */
        if (g_prep.handle && g_prep.handle != h) return ATT_ERROR_PREPARE_QUEUE_FULL;
        if ((size_t)offset + size > sizeof g_prep.buf) return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        if (!g_prep.handle) { g_prep.handle = h; g_prep.len = 0; g_prep.conn = s + 1; }
        memcpy(g_prep.buf + offset, buf, size);
        if (offset + size > g_prep.len) g_prep.len = (uint16_t)(offset + size);
        return 0;
    }
    int rc = mcs_ble_attr_write(g_hid[h], s + 1, offset, buf, size);
    return rc < 0 ? att_code(rc) : 0;
}
static uint8_t perm(uint16_t flags) {
    return (flags & MCS_BLE_PERM_AUTHENTICATED) ? ATT_SECURITY_AUTHENTICATED : (flags & MCS_BLE_PERM_ENCRYPTED) ? ATT_SECURITY_ENCRYPTED : ATT_SECURITY_NONE;
}
static void map(uint16_t h, uint8_t kind, uint16_t id) { if (h < B_MAXH) { g_hkind[h] = kind; g_hid[h] = id; } }
static void db_base(void) {                 /* GAP (name, appearance) + GATT services */
    att_db_util_init();
    att_db_util_add_service_uuid16(ORG_BLUETOOTH_SERVICE_GENERIC_ACCESS);
    map(att_db_util_add_characteristic_uuid16(ORG_BLUETOOTH_CHARACTERISTIC_GAP_DEVICE_NAME, ATT_PROPERTY_READ | ATT_PROPERTY_DYNAMIC, ATT_SECURITY_NONE, ATT_SECURITY_NONE, NULL, 0), H_NAME, 0);
    map(att_db_util_add_characteristic_uuid16(ORG_BLUETOOTH_CHARACTERISTIC_GAP_APPEARANCE, ATT_PROPERTY_READ | ATT_PROPERTY_DYNAMIC, ATT_SECURITY_NONE, ATT_SECURITY_NONE, NULL, 0), H_APPEARANCE, 0);
    att_db_util_add_service_uuid16(ORG_BLUETOOTH_SERVICE_GENERIC_ATTRIBUTE);
}
static int b_gatts_set(void* ctx, mcs_ble_svc_def_t* svcs, int n) {
    (void)ctx;
    uint8_t be[16];
    size_t need = 2 + attr_size(2, 2) * 2 + (attr_size(2, 5) + attr_size(2, 0)) * 2;   /* the base services */
    int nattr = 0;
    for (int s = 0; s < n; s++) {
        need += attr_size(2, svcs[s].uuid.len == 2 ? 2 : 16);
        for (int c = 0; c < svcs[s].nchr; c++) {
            mcs_ble_chr_def_t* cd = &svcs[s].chr[c];
            uint16_t u = uuid_out(&cd->uuid, be);
            need += attr_size(2, u ? 5 : 19) + attr_size(u, 0);
            if (cd->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) need += attr_size(2, 2);
            nattr += 3 + cd->ndsc;
            for (int d = 0; d < cd->ndsc; d++) need += attr_size(uuid_out(&cd->dsc[d].uuid, be), 0);
        }
    }
    if (need > MCS_RP2_BLE_DB_SIZE || 8 + n + nattr > B_MAXH) return MCS_HAL_EINVAL;   /* too many attributes: raise MCS_RP2_BLE_DB_SIZE / MAX_ATT_DB_SIZE */
    LOCK();
    memset(g_hkind, 0, sizeof g_hkind);
    memset(g_cccd, 0, sizeof g_cccd);
    g_prep.handle = 0;
    db_base();
    for (int s = 0; s < n; s++) {
        uint16_t u = uuid_out(&svcs[s].uuid, be);
        if (svcs[s].primary) svcs[s].handle = u ? att_db_util_add_service_uuid16(u) : att_db_util_add_service_uuid128(be);
        else svcs[s].handle = u ? att_db_util_add_secondary_service_uuid16(u) : att_db_util_add_secondary_service_uuid128(be);
        for (int c = 0; c < svcs[s].nchr; c++) {
            mcs_ble_chr_def_t* cd = &svcs[s].chr[c];
            uint16_t props = (uint16_t)((cd->props & 0xFF) | ATT_PROPERTY_DYNAMIC);
            uint8_t p = perm(cd->props);
            u = uuid_out(&cd->uuid, be);
            cd->handle = u ? att_db_util_add_characteristic_uuid16(u, props, p, p, NULL, 0)
                           : att_db_util_add_characteristic_uuid128(be, props, p, p, NULL, 0);
            map(cd->handle, H_VALUE, cd->id);
            cd->cccd_handle = 0;
            if (cd->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) { cd->cccd_handle = cd->handle + 1; map(cd->cccd_handle, H_CCCD, cd->id); }
            for (int d = 0; d < cd->ndsc; d++) {
                mcs_ble_dsc_def_t* dd = &cd->dsc[d];
                if (mcs_ble_uuid_short(&dd->uuid) == 0x2902) { dd->handle = cd->cccd_handle; continue; }
                uint16_t dp = (uint16_t)(ATT_PROPERTY_DYNAMIC | ((dd->flags & MCS_BLE_DSC_READ) ? ATT_PROPERTY_READ : 0)
                                         | ((dd->flags & MCS_BLE_DSC_WRITE) ? ATT_PROPERTY_WRITE : 0));
                uint8_t q = perm(dd->flags);
                u = uuid_out(&dd->uuid, be);
                dd->handle = u ? att_db_util_add_descriptor_uuid16(u, dp, q, q, NULL, 0) : att_db_util_add_descriptor_uuid128(be, dp, q, q, NULL, 0);
                map(dd->handle, H_VALUE, dd->id);
            }
        }
    }
    att_set_db(att_db_util_get_address());
    UNLOCK();
    return 0;
}
static int b_gatts_notify(void* ctx, int conn, const mcs_ble_chr_def_t* chr, const uint8_t* data, size_t n, int indicate) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK();
    int rc = indicate ? att_server_indicate(h, chr->handle, data, (uint16_t)n) : att_server_notify(h, chr->handle, data, (uint16_t)n);
    UNLOCK();
    return st(rc);
}

/* ---------------------------------------------------------------- advertising + scanning */
static mcs_ble_adv_t g_adv;                 /* BTstack keeps pointers into it */
static btstack_timer_source_t g_adv_timer, g_scan_timer, g_conn_timer;
static uint8_t g_advertising, g_scanning;
static void adv_timeout(btstack_timer_source_t* t) {
    (void)t;
    if (!g_advertising) return;
    gap_advertisements_enable(0); g_advertising = 0;
    mcs_ble_on_event(MCS_BLE_EV_ADV_DONE, -1, 0);
}
static int b_adv_start(void* ctx, const mcs_ble_adv_t* a) {
    (void)ctx;
    LOCK();
    if (g_advertising) gap_advertisements_enable(0);
    btstack_run_loop_remove_timer(&g_adv_timer);
    g_adv = *a;
    uint16_t iv = (uint16_t)((a->interval_ms ? a->interval_ms : 100) * 8 / 5);   /* 0.625 ms units */
    uint8_t type = a->connectable ? 0 : a->scannable ? 2 : 3;                   /* ADV_IND / ADV_SCAN_IND / ADV_NONCONN_IND */
    bd_addr_t none = { 0 };
    gap_advertisements_set_params(iv, iv, type, 0, none, 0x07, 0);
    gap_advertisements_set_data(g_adv.len, g_adv.data);
    gap_scan_response_set_data(g_adv.rsp_len, g_adv.rsp);
    gap_advertisements_enable(1);
    g_advertising = 1;
    if (a->duration_ms) {
        btstack_run_loop_set_timer_handler(&g_adv_timer, adv_timeout);
        btstack_run_loop_set_timer(&g_adv_timer, a->duration_ms);
        btstack_run_loop_add_timer(&g_adv_timer);
    }
    UNLOCK();
    return 0;
}
static int b_adv_stop(void* ctx) {
    (void)ctx;
    LOCK();
    btstack_run_loop_remove_timer(&g_adv_timer);
    gap_advertisements_enable(0); g_advertising = 0;
    UNLOCK();
    return 0;
}
static void scan_timeout(btstack_timer_source_t* t) {
    (void)t;
    if (!g_scanning) return;
    gap_stop_scan(); g_scanning = 0;
    mcs_ble_on_event(MCS_BLE_EV_SCAN_DONE, -1, 0);
}
static int b_scan_start(void* ctx, const mcs_ble_scan_t* s) {
    (void)ctx;
    uint16_t iv = (uint16_t)(s->interval_ms ? s->interval_ms * 8 / 5 : 0x60), win = (uint16_t)(s->window_ms ? s->window_ms * 8 / 5 : 0x30);
    if (win > iv) win = iv;
    LOCK();
    if (g_scanning) gap_stop_scan();
    btstack_run_loop_remove_timer(&g_scan_timer);
    gap_set_scan_duplicate_filter(s->filter_duplicates != 0);
    gap_set_scan_parameters(s->active ? 1 : 0, iv, win);
    gap_start_scan();
    g_scanning = 1;
    if (s->duration_ms) {
        btstack_run_loop_set_timer_handler(&g_scan_timer, scan_timeout);
        btstack_run_loop_set_timer(&g_scan_timer, s->duration_ms);
        btstack_run_loop_add_timer(&g_scan_timer);
    }
    UNLOCK();
    return 0;
}
static int b_scan_stop(void* ctx) {
    (void)ctx;
    LOCK();
    btstack_run_loop_remove_timer(&g_scan_timer);
    if (g_scanning) gap_stop_scan();
    g_scanning = 0;
    UNLOCK();
    return 0;
}

/* ---------------------------------------------------------------- connections */
static struct { uint16_t interval, latency, timeout; uint8_t role; mcs_ble_addr_t peer; } g_ci[B_MAXC];
static volatile uint8_t g_rssi_done;
static volatile int8_t g_rssi;
static uint8_t g_connecting;
static int b_disconnect(void* ctx, int conn) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK(); int rc = gap_disconnect(h); UNLOCK();
    return st(rc);
}
static int b_conn_info(void* ctx, int conn, mcs_ble_conn_info_t* o) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    int s = conn - 1;
    LOCK();
    o->peer = g_ci[s].peer;
    o->role = g_ci[s].role;
    o->encrypted = gap_encryption_key_size(h) > 0;
    o->authenticated = gap_authenticated(h);
    o->bonded = gap_bonded(h);
    o->mtu = att_server_get_mtu(h);
    uint16_t m;
    if (g_ci[s].role == MCS_BLE_ROLE_CENTRAL && gatt_client_get_mtu(h, &m) == ERROR_CODE_SUCCESS && m > o->mtu) o->mtu = m;
    o->interval = g_ci[s].interval; o->latency = g_ci[s].latency; o->timeout = g_ci[s].timeout;
    UNLOCK();
    return 0;
}
static int b_conn_rssi(void* ctx, int conn) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    g_rssi_done = 0;
    LOCK(); int rc = gap_read_rssi(h); UNLOCK();
    if (rc != ERROR_CODE_SUCCESS) return st(rc);
    return wait_flag(&g_rssi_done, 500) ? g_rssi : MCS_HAL_ETIMEOUT;
}
static int b_conn_update(void* ctx, int conn, int mn, int mx, int lat, int to) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK();
    int rc = g_ci[conn - 1].role == MCS_BLE_ROLE_CENTRAL
        ? gap_update_connection_parameters(h, (uint16_t)mn, (uint16_t)mx, (uint16_t)lat, (uint16_t)to)
        : gap_request_connection_parameter_update(h, (uint16_t)mn, (uint16_t)mx, (uint16_t)lat, (uint16_t)to);
    UNLOCK();
    return st(rc);
}

/* ---------------------------------------------------------------- central + GATT client */
static gatt_client_notification_t g_listener;
static uint8_t g_wbuf[MCS_BLE_MAX_VALUE], g_cccd_out[2];
static uint8_t g_op_mtu;                     /* the running operation is an MTU exchange */
static void gc_handler(uint8_t type, uint16_t ch, uint8_t* p, uint16_t size) {
    (void)ch; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    mcs_ble_attr_t a;
    memset(&a, 0, sizeof a);
    switch (hci_event_packet_get_type(p)) {
    case GATT_EVENT_SERVICE_QUERY_RESULT: {
        gatt_client_service_t sv; gatt_event_service_query_result_get_service(p, &sv);
        a.kind = MCS_BLE_DISC_SERVICES; uuid_in(&a.uuid, sv.uuid16, sv.uuid128);
        a.handle = sv.start_group_handle; a.end = sv.end_group_handle;
        mcs_ble_op_attr(&a);
        break;
    }
    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
        gatt_client_characteristic_t c; gatt_event_characteristic_query_result_get_characteristic(p, &c);
        a.kind = MCS_BLE_DISC_CHARACTERISTICS; uuid_in(&a.uuid, c.uuid16, c.uuid128);
        a.props = (uint8_t)c.properties; a.handle = c.value_handle; a.end = c.start_handle;
        mcs_ble_op_attr(&a);
        break;
    }
    case GATT_EVENT_ALL_CHARACTERISTIC_DESCRIPTORS_QUERY_RESULT: {
        gatt_client_characteristic_descriptor_t d; gatt_event_all_characteristic_descriptors_query_result_get_characteristic_descriptor(p, &d);
        a.kind = MCS_BLE_DISC_DESCRIPTORS; uuid_in(&a.uuid, d.uuid16, d.uuid128); a.handle = d.handle;
        mcs_ble_op_attr(&a);
        break;
    }
    case GATT_EVENT_LONG_CHARACTERISTIC_VALUE_QUERY_RESULT:
        mcs_ble_op_data(gatt_event_long_characteristic_value_query_result_get_value(p), gatt_event_long_characteristic_value_query_result_get_value_length(p));
        break;
    case GATT_EVENT_QUERY_COMPLETE:
        mcs_ble_op_done(att_st(gatt_event_query_complete_get_att_status(p)), 0);
        break;
    case GATT_EVENT_MTU: {
        int c = connh(gatt_event_mtu_get_handle(p));
        uint16_t m = gatt_event_mtu_get_MTU(p);
        if (c > 0) mcs_ble_on_event(MCS_BLE_EV_MTU, c, m);
        if (g_op_mtu) { g_op_mtu = 0; mcs_ble_op_done(0, m); }
        break;
    }
    case GATT_EVENT_NOTIFICATION:
        mcs_ble_on_notify(connh(gatt_event_notification_get_handle(p)), gatt_event_notification_get_value_handle(p),
                          gatt_event_notification_get_value(p), gatt_event_notification_get_value_length(p), 0);
        break;
    case GATT_EVENT_INDICATION:
        mcs_ble_on_notify(connh(gatt_event_indication_get_handle(p)), gatt_event_indication_get_value_handle(p),
                          gatt_event_indication_get_value(p), gatt_event_indication_get_value_length(p), 1);
        break;
    default: break;
    }
}
static void conn_timeout(btstack_timer_source_t* t) { (void)t; if (g_connecting) gap_connect_cancel(); }
static int b_connect(void* ctx, const mcs_ble_addr_t* peer, uint32_t timeout_ms) {
    (void)ctx;
    bd_addr_t a;
    memcpy(a, peer->a, 6);
    LOCK();
    if (g_connecting) { UNLOCK(); return MCS_HAL_EBUSY; }
    if (g_scanning) { btstack_run_loop_remove_timer(&g_scan_timer); gap_stop_scan(); g_scanning = 0; }
    int rc = gap_connect(a, (bd_addr_type_t)(peer->type & 1));
    if (rc == ERROR_CODE_SUCCESS) {
        g_connecting = 1;
        btstack_run_loop_set_timer_handler(&g_conn_timer, conn_timeout);
        btstack_run_loop_set_timer(&g_conn_timer, timeout_ms ? timeout_ms : MCS_BLE_CONNECT_TIMEOUT);
        btstack_run_loop_add_timer(&g_conn_timer);
    }
    UNLOCK();
    return st(rc);
}
static int b_connect_cancel(void* ctx) {
    (void)ctx;
    LOCK();
    btstack_run_loop_remove_timer(&g_conn_timer);
    int rc = g_connecting ? gap_connect_cancel() : 0;
    UNLOCK();
    return st(rc);
}
static int b_discover(void* ctx, int conn, int kind, uint16_t start, uint16_t end) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    int rc;
    LOCK();
    if (kind == MCS_BLE_DISC_SERVICES) rc = gatt_client_discover_primary_services(gc_handler, h);
    else if (kind == MCS_BLE_DISC_CHARACTERISTICS) {
        gatt_client_service_t sv; memset(&sv, 0, sizeof sv);
        sv.start_group_handle = start; sv.end_group_handle = end;
        rc = gatt_client_discover_characteristics_for_service(gc_handler, h, &sv);
    } else {
        gatt_client_characteristic_t c; memset(&c, 0, sizeof c);   /* descriptors: value_handle + 1 .. end */
        c.start_handle = start > 1 ? (uint16_t)(start - 2) : 0; c.value_handle = start ? (uint16_t)(start - 1) : 0; c.end_handle = end;
        rc = gatt_client_discover_characteristic_descriptors(gc_handler, h, &c);
    }
    UNLOCK();
    return st(rc);
}
static int b_read(void* ctx, int conn, uint16_t handle) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK(); int rc = gatt_client_read_long_value_of_characteristic_using_value_handle(gc_handler, h, handle); UNLOCK();
    return st(rc);
}
static int b_write(void* ctx, int conn, uint16_t handle, const uint8_t* data, size_t n, int with_response) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    if (n > sizeof g_wbuf) return MCS_HAL_EINVAL;
    int rc;
    LOCK();
    memcpy(g_wbuf, data, n);
    if (!with_response) {
        rc = gatt_client_write_value_of_characteristic_without_response(h, handle, (uint16_t)n, g_wbuf);
        if (rc == ERROR_CODE_SUCCESS) mcs_ble_op_done(0, 0);
    } else {
        uint16_t mtu = 23;
        gatt_client_get_mtu(h, &mtu);
        rc = n + 3 <= mtu ? gatt_client_write_value_of_characteristic(gc_handler, h, handle, (uint16_t)n, g_wbuf)
                          : gatt_client_write_long_value_of_characteristic(gc_handler, h, handle, (uint16_t)n, g_wbuf);
    }
    UNLOCK();
    return st(rc);
}
static int b_subscribe(void* ctx, int conn, uint16_t vh, uint16_t cccd, int flags) {
    (void)ctx; (void)vh;                     /* g_listener takes every notification / indication */
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    if (!cccd) return MCS_HAL_EINVAL;
    LOCK();
    little_endian_store_16(g_cccd_out, 0, (uint16_t)(flags & (MCS_BLE_SUB_NOTIFY | MCS_BLE_SUB_INDICATE)));
    int rc = gatt_client_write_value_of_characteristic(gc_handler, h, cccd, 2, g_cccd_out);
    UNLOCK();
    return st(rc);
}
static int b_exchange_mtu(void* ctx, int conn) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK();
    g_op_mtu = 1;
    gatt_client_send_mtu_negotiation(gc_handler, h);   /* GATT_EVENT_MTU, also when it was exchanged before */
    UNLOCK();
    return 0;
}

/* ---------------------------------------------------------------- security */
static void apply_auth(void) {
    uint8_t req = (g_opt[MCS_BLE_OPT_BONDING] ? SM_AUTHREQ_BONDING : 0) | (g_opt[MCS_BLE_OPT_MITM] ? SM_AUTHREQ_MITM_PROTECTION : 0)
                | (g_opt[MCS_BLE_OPT_SC] ? SM_AUTHREQ_SECURE_CONNECTION : 0);
    sm_set_io_capabilities((io_capability_t)g_opt[MCS_BLE_OPT_IO_CAP]);
    sm_set_authentication_requirements(req);
    if (g_opt[MCS_BLE_OPT_PASSKEY] >= 0) sm_use_fixed_passkey_in_display_role((uint32_t)g_opt[MCS_BLE_OPT_PASSKEY]);
}
static int b_pair(void* ctx, int conn) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK(); sm_request_pairing(h); UNLOCK();
    return 0;
}
static int b_passkey_reply(void* ctx, int conn, int kind, int32_t value) {
    (void)ctx;
    hci_con_handle_t h = hci_of(conn);
    if (h == HCI_CON_HANDLE_INVALID) return MCS_HAL_ENODEV;
    LOCK();
    switch (kind) {
    case MCS_BLE_PASSKEY_ENTRY: sm_passkey_input(h, (uint32_t)value); break;
    case MCS_BLE_PASSKEY_COMPARE: if (value) sm_numeric_comparison_confirm(h); else sm_bonding_decline(h); break;
    case MCS_BLE_PASSKEY_CONFIRM: if (value) sm_just_works_confirm(h); else sm_bonding_decline(h); break;
    default: break;
    }
    UNLOCK();
    return 0;
}
static int b_bonds(void* ctx, int op, mcs_ble_addr_t* addrs, int max) {
    (void)ctx;
    int n = 0;
    LOCK();
    int cap = le_device_db_max_count();
    for (int i = 0; i < cap; i++) {
        int type = BD_ADDR_TYPE_UNKNOWN; bd_addr_t a; sm_key_t irk;
        le_device_db_info(i, &type, a, irk);
        if (type == BD_ADDR_TYPE_UNKNOWN) continue;
        if (op == MCS_BLE_BONDS_CLEAR || (op == MCS_BLE_BONDS_DELETE && type == (addrs->type & 1) && !memcmp(a, addrs->a, 6))) {
            gap_delete_bonding((bd_addr_type_t)type, a); n++; continue;
        }
        if (op == MCS_BLE_BONDS_LIST && n < max) addr_in(&addrs[n], type, a);
        n++;
    }
    UNLOCK();
    return op == MCS_BLE_BONDS_DELETE || op == MCS_BLE_BONDS_CLEAR ? 0 : n;
}

/* ---------------------------------------------------------------- events */
static void hci_handler(uint8_t type, uint16_t ch, uint8_t* p, uint16_t size) {
    (void)ch; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(p)) {
    case BTSTACK_EVENT_STATE:
        g_working = btstack_event_state_get_state(p) == HCI_STATE_WORKING;
        break;
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(p) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            uint8_t status = gap_subevent_le_connection_complete_get_status(p);
            int role = gap_subevent_le_connection_complete_get_role(p) ? MCS_BLE_ROLE_PERIPHERAL : MCS_BLE_ROLE_CENTRAL;
            if (role == MCS_BLE_ROLE_CENTRAL) { g_connecting = 0; btstack_run_loop_remove_timer(&g_conn_timer); }
            if (status) {
                if (role == MCS_BLE_ROLE_CENTRAL) {
                    mcs_ble_on_event(MCS_BLE_EV_CONNECT_FAILED, -1, status);
                    mcs_ble_op_done(status == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER ? MCS_HAL_ETIMEOUT : MCS_HAL_ERR, 0);
                }
                break;
            }
            hci_con_handle_t h = gap_subevent_le_connection_complete_get_connection_handle(p);
            int s = slot_of(HCI_CON_HANDLE_INVALID);
            if (s < 0) { gap_disconnect(h); break; }
            g_hci[s] = h;
            bd_addr_t a; gap_subevent_le_connection_complete_get_peer_address(p, a);
            addr_in(&g_ci[s].peer, gap_subevent_le_connection_complete_get_peer_address_type(p), a);
            g_ci[s].role = (uint8_t)role;
            g_ci[s].interval = gap_subevent_le_connection_complete_get_conn_interval(p);
            g_ci[s].latency = gap_subevent_le_connection_complete_get_conn_latency(p);
            g_ci[s].timeout = gap_subevent_le_connection_complete_get_supervision_timeout(p);
            memset(g_cccd[s], 0, sizeof g_cccd[s]);
            if (role == MCS_BLE_ROLE_PERIPHERAL && g_advertising) {   /* like a legacy set: advertising ends */
                btstack_run_loop_remove_timer(&g_adv_timer);
                gap_advertisements_enable(0); g_advertising = 0;
            }
            mcs_ble_on_connect(s + 1, &g_ci[s].peer, role);
        }
        break;
    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(p)) {
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE: {
            int s = slot_of(hci_subevent_le_connection_update_complete_get_connection_handle(p));
            if (s < 0 || hci_subevent_le_connection_update_complete_get_status(p)) break;
            g_ci[s].interval = hci_subevent_le_connection_update_complete_get_conn_interval(p);
            g_ci[s].latency = hci_subevent_le_connection_update_complete_get_conn_latency(p);
            g_ci[s].timeout = hci_subevent_le_connection_update_complete_get_supervision_timeout(p);
            mcs_ble_on_event(MCS_BLE_EV_CONN_UPDATE, s + 1, g_ci[s].interval);
            break;
        }
        case HCI_SUBEVENT_LE_PHY_UPDATE_COMPLETE: {
            int c = connh(hci_subevent_le_phy_update_complete_get_connection_handle(p));
            if (c > 0 && !hci_subevent_le_phy_update_complete_get_status(p))
                mcs_ble_on_event(MCS_BLE_EV_PHY, c, hci_subevent_le_phy_update_complete_get_tx_phy(p) << 4 | p[7] /* rx_phy */);
            break;
        }
        default: break;
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        int s = slot_of(hci_event_disconnection_complete_get_connection_handle(p));
        if (s < 0) break;
        g_hci[s] = HCI_CON_HANDLE_INVALID;
        if (g_prep.conn == s + 1) g_prep.handle = 0;
        mcs_ble_on_disconnect(s + 1, hci_event_disconnection_complete_get_reason(p));
        break;
    }
    case HCI_EVENT_ENCRYPTION_CHANGE: {
        hci_con_handle_t h = hci_event_encryption_change_get_connection_handle(p);
        int c = connh(h);
        if (c > 0 && !hci_event_encryption_change_get_status(p) && hci_event_encryption_change_get_encryption_enabled(p))
            mcs_ble_on_event(MCS_BLE_EV_ENCRYPTED, c, 1 + (gap_authenticated(h) ? 1 : 0) + (gap_bonded(h) ? 1 : 0));
        break;
    }
    case GAP_EVENT_ADVERTISING_REPORT: {
        bd_addr_t a; gap_event_advertising_report_get_address(p, a);
        mcs_ble_addr_t o; addr_in(&o, gap_event_advertising_report_get_address_type(p), a);
        mcs_ble_on_report(&o, (int8_t)gap_event_advertising_report_get_rssi(p), gap_event_advertising_report_get_advertising_event_type(p),
                          gap_event_advertising_report_get_data(p), gap_event_advertising_report_get_data_length(p));
        break;
    }
    case GAP_EVENT_RSSI_MEASUREMENT:
        g_rssi = (int8_t)gap_event_rssi_measurement_get_rssi(p); g_rssi_done = 1;
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE: {
        int c = connh(att_event_mtu_exchange_complete_get_handle(p));
        if (c > 0) mcs_ble_on_event(MCS_BLE_EV_MTU, c, att_event_mtu_exchange_complete_get_MTU(p));
        break;
    }
    case ATT_EVENT_HANDLE_VALUE_INDICATION_COMPLETE:
        mcs_ble_on_event(MCS_BLE_EV_INDICATE_DONE, connh(att_event_handle_value_indication_complete_get_conn_handle(p)),
                         att_st(att_event_handle_value_indication_complete_get_status(p)));
        break;
    /* Security Manager */
    case SM_EVENT_JUST_WORKS_REQUEST:
        mcs_ble_on_passkey(connh(sm_event_just_works_request_get_handle(p)), MCS_BLE_PASSKEY_CONFIRM, 0);
        break;
    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        mcs_ble_on_passkey(connh(sm_event_numeric_comparison_request_get_handle(p)), MCS_BLE_PASSKEY_COMPARE, (int32_t)sm_event_numeric_comparison_request_get_passkey(p));
        break;
    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
        mcs_ble_on_passkey(connh(sm_event_passkey_display_number_get_handle(p)), MCS_BLE_PASSKEY_DISPLAY, (int32_t)sm_event_passkey_display_number_get_passkey(p));
        break;
    case SM_EVENT_PASSKEY_INPUT_NUMBER:
        mcs_ble_on_passkey(connh(sm_event_passkey_input_number_get_handle(p)), MCS_BLE_PASSKEY_ENTRY, 0);
        break;
    case SM_EVENT_PAIRING_COMPLETE: {
        uint8_t s = sm_event_pairing_complete_get_status(p);
        mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, connh(sm_event_pairing_complete_get_handle(p)), s ? (s == ERROR_CODE_AUTHENTICATION_FAILURE ? sm_event_pairing_complete_get_reason(p) : s) : 0);
        break;
    }
    default: break;
    }
}

/* ---------------------------------------------------------------- controller */
static uint8_t g_stack_init;
static int radio_init(void) {
#if MCS_RP2_WIFI
    return mcs_rp2_radio_init();             /* shared with Wi-Fi (country code etc.) */
#else
    static uint8_t up;
    if (up) return 0;
    if (cyw43_arch_init() != 0) return MCS_HAL_ENODEV;
    up = 1;
    return 0;
#endif
}
#if MCS_RP2_WIFI
bool mcs_rp2_ble_started(void) { return g_stack_init != 0; }
#endif
static int b_enable(void* ctx, int on) {
    (void)ctx;
    if (!on) {
        if (!g_on) return 0;
        LOCK();
        btstack_run_loop_remove_timer(&g_adv_timer); btstack_run_loop_remove_timer(&g_scan_timer); btstack_run_loop_remove_timer(&g_conn_timer);
        g_advertising = g_scanning = g_connecting = 0;
        hci_power_control(HCI_POWER_OFF);
        UNLOCK();
        for (int i = 0; i < B_MAXC; i++) g_hci[i] = HCI_CON_HANDLE_INVALID;
        g_on = g_working = 0;
        return 0;
    }
    if (g_on) return 0;
    int rc = radio_init();
    if (rc < 0) return rc;
    LOCK();
    if (!g_stack_init) {
        g_stack_init = 1;
        l2cap_init();
        sm_init();
        att_db_util_init();
        db_base();
        att_server_init(att_db_util_get_address(), att_read_cb, att_write_cb);
        gatt_client_init();
        l2cap_set_max_le_mtu(MCS_RP2_BLE_MTU);
        g_hci_reg.callback = hci_handler;
        hci_add_event_handler(&g_hci_reg);
        g_sm_reg.callback = hci_handler;
        sm_add_event_handler(&g_sm_reg);
        att_server_register_packet_handler(hci_handler);
        gatt_client_listen_for_characteristic_value_updates(&g_listener, gc_handler, GATT_CLIENT_ANY_CONNECTION, NULL);
    }
    apply_auth();
    gap_random_address_set_mode(g_opt[MCS_BLE_OPT_ADDR_TYPE] == MCS_BLE_ADDR_RANDOM ? GAP_RANDOM_ADDRESS_TYPE_STATIC : GAP_RANDOM_ADDRESS_TYPE_OFF);
    g_working = 0;
    hci_power_control(HCI_POWER_ON);
    UNLOCK();
    if (!wait_flag(&g_working, 3000)) return MCS_HAL_ETIMEOUT;
    g_on = 1;
    return 0;
}
static int b_address(void* ctx, mcs_ble_addr_t* out) {
    (void)ctx;
    if (!g_on) return MCS_HAL_EBUSY;
    uint8_t type = 0; bd_addr_t a;
    LOCK(); gap_le_get_own_address(&type, a); UNLOCK();
    addr_in(out, type, a);
    return 0;
}
static int b_set_name(void* ctx, const char* name) {   /* GAP 0x2A00 reads it; the front end puts it in the advertising data */
    (void)ctx;
    LOCK();
    strncpy(g_name, name, sizeof g_name - 1); g_name[sizeof g_name - 1] = 0;
    UNLOCK();
    return 0;
}
static int b_opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    switch (opt) {
    case MCS_BLE_OPT_MTU: *v = g_opt[opt] ? g_opt[opt] : MCS_RP2_BLE_MTU; return 0;
    case MCS_BLE_OPT_MAX_CONN: *v = B_MAXC; return 0;
    case MCS_BLE_OPT_TX_POWER: return MCS_HAL_ENOTSUP;
    default:
        if (opt <= 0 || opt >= MCS_BLE_OPT__COUNT) return MCS_HAL_ENOTSUP;
        *v = g_opt[opt]; return 0;
    }
}
static int b_opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_BLE_OPT_MTU:
        if (v < 23 || v > 517) return MCS_HAL_EINVAL;
        g_opt[opt] = v > MCS_RP2_BLE_MTU ? MCS_RP2_BLE_MTU : v;
        if (g_stack_init) { LOCK(); l2cap_set_max_le_mtu((uint16_t)g_opt[opt]); UNLOCK(); }
        return 0;
    case MCS_BLE_OPT_APPEARANCE: g_opt[opt] = v & 0xFFFF; return 0;
    case MCS_BLE_OPT_IO_CAP: if (v < 0 || v > 4) return MCS_HAL_EINVAL; g_opt[opt] = v; break;
    case MCS_BLE_OPT_BONDING: case MCS_BLE_OPT_MITM: case MCS_BLE_OPT_SC: g_opt[opt] = v != 0; break;
    case MCS_BLE_OPT_PASSKEY: if (v > 999999) return MCS_HAL_EINVAL; g_opt[opt] = v < 0 ? -1 : v; break;
    case MCS_BLE_OPT_ADDR_TYPE:
        if (v != MCS_BLE_ADDR_PUBLIC && v != MCS_BLE_ADDR_RANDOM) return MCS_HAL_EINVAL;
        g_opt[opt] = v;
        if (g_on) { LOCK(); gap_random_address_set_mode(v == MCS_BLE_ADDR_RANDOM ? GAP_RANDOM_ADDRESS_TYPE_STATIC : GAP_RANDOM_ADDRESS_TYPE_OFF); UNLOCK(); }
        return 0;
    case MCS_BLE_OPT_PHY: if (v <= 0 || v > 7) return MCS_HAL_EINVAL; g_opt[opt] = v; return 0;
    default: return MCS_HAL_ENOTSUP;
    }
    if (g_stack_init) { LOCK(); apply_auth(); UNLOCK(); }
    return 0;
}
static void b_lock(void* ctx) { (void)ctx; critical_section_enter_blocking(&g_cs); }
static void b_unlock(void* ctx) { (void)ctx; critical_section_exit(&g_cs); }

static const mcs_ble_t g_ble = {
    .name = "btstack",
    .enable = b_enable, .address = b_address, .set_name = b_set_name, .opt_get = b_opt_get, .opt_set = b_opt_set,
    .lock = b_lock, .unlock = b_unlock,
    .adv_start = b_adv_start, .adv_stop = b_adv_stop,
    .scan_start = b_scan_start, .scan_stop = b_scan_stop,
    .gatts_set = b_gatts_set, .gatts_notify = b_gatts_notify,
    .disconnect = b_disconnect, .conn_info = b_conn_info, .conn_rssi = b_conn_rssi, .conn_update = b_conn_update,
    .connect = b_connect, .connect_cancel = b_connect_cancel, .discover = b_discover, .read = b_read, .write = b_write,
    .subscribe = b_subscribe, .exchange_mtu = b_exchange_mtu,
    .pair = b_pair, .passkey_reply = b_passkey_reply, .bonds = b_bonds,
};
const mcs_ble_t* mcs_rp2_ble(void) {
    static bool init;
    if (!init) {
        init = true;
        critical_section_init(&g_cs);
        for (int i = 0; i < B_MAXC; i++) g_hci[i] = HCI_CON_HANDLE_INVALID;
        g_opt[MCS_BLE_OPT_IO_CAP] = MCS_BLE_IO_NONE;
        g_opt[MCS_BLE_OPT_BONDING] = 1;
        g_opt[MCS_BLE_OPT_SC] = 1;
        g_opt[MCS_BLE_OPT_PASSKEY] = -1;
        g_opt[MCS_BLE_OPT_PHY] = 1;
    }
    return &g_ble;
}
#endif
