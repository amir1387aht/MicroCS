/*
 * MicroCS - Zephyr Bluetooth LE backend (Zephyr host + any controller: nRF52 / nRF53 /
 * nRF54 SoftDevice Controller or Zephyr LL, HCI over UART / IPC ...).
 *
 * prj.conf: CONFIG_BT=y and CONFIG_MICROCS_BLE=y (ports/zephyr/example/overlay-ble.conf
 * has a complete set: peripheral + central, GATT client, dynamic GATT database, SMP).
 * Roles the configuration lacks are reported as missing (Ble.Has) instead of failing to
 * link. See docs/BLE.md.
 *
 * Threads: the front end calls these functions on the VM thread; the Bluetooth host calls
 * back on its RX thread / the system work queue. Everything shared goes through
 * mcs_ble_on_*() (a spinlock guards the front end's queue). Connection handles given to
 * the front end are bt_conn_index() + 1.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#if defined(CONFIG_BT_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#include <string.h>
#include "mcs_ble.h"
#include "mcs_hal.h"

#if MCS_ENABLE_BLE
#define Z_MAX_CONN CONFIG_BT_MAX_CONN
static struct k_spinlock g_lock;
static k_spinlock_key_t g_key;
static bool g_on;
static char g_name[32] = MCS_BLE_NAME;
static int32_t g_opt[MCS_BLE_OPT__COUNT];
static struct bt_conn* g_conns[Z_MAX_CONN];

/* ---------------------------------------------------------------- helpers */
static int st(int rc) {               /* negative errno -> MCS_HAL_E* */
    switch (rc) {
    case 0: return 0;
    case -ETIMEDOUT: case -EAGAIN: return MCS_HAL_ETIMEOUT;
    case -ENOTCONN: return MCS_HAL_ENODEV;
    case -EBUSY: case -EALREADY: case -ENOMEM: case -ENOBUFS: return MCS_HAL_EBUSY;
    case -EINVAL: return MCS_HAL_EINVAL;
    case -ENOTSUP: case -ENOSYS: return MCS_HAL_ENOTSUP;
    default: return rc > 0 ? rc : MCS_HAL_ERR;
    }
}
static int att_st(uint8_t err) { return err ? MCS_BLE_ATT_ERR(err) : 0; }
static int connh(const struct bt_conn* c) { return (int)bt_conn_index(c) + 1; }
static struct bt_conn* conn_of(int h) { return h >= 1 && h <= Z_MAX_CONN ? g_conns[h - 1] : NULL; }
static void addr_in(mcs_ble_addr_t* o, const bt_addr_le_t* a) {
    o->type = a->type;
    for (int i = 0; i < 6; i++) o->a[i] = a->a.val[5 - i];
}
static void addr_out(bt_addr_le_t* o, const mcs_ble_addr_t* a) {
    o->type = a->type;
    for (int i = 0; i < 6; i++) o->a.val[i] = a->a[5 - i];
}
typedef union { struct bt_uuid u; struct bt_uuid_16 u16; struct bt_uuid_32 u32; struct bt_uuid_128 u128; } z_uuid_t;
static void uuid_in(mcs_ble_uuid_t* o, const struct bt_uuid* u) {
    memset(o, 0, sizeof *o);
    if (u->type == BT_UUID_TYPE_16) mcs_ble_uuid16(o, BT_UUID_16(u)->val);
    else if (u->type == BT_UUID_TYPE_32) { o->len = 4; sys_put_le32(BT_UUID_32(u)->val, o->b); }
    else { o->len = 16; memcpy(o->b, BT_UUID_128(u)->val, 16); }
}
static void uuid_out(z_uuid_t* o, const mcs_ble_uuid_t* u) {
    memset(o, 0, sizeof *o);
    if (u->len == 2) { o->u16.uuid.type = BT_UUID_TYPE_16; o->u16.val = sys_get_le16(u->b); }
    else if (u->len == 4) { o->u32.uuid.type = BT_UUID_TYPE_32; o->u32.val = sys_get_le32(u->b); }
    else { o->u128.uuid.type = BT_UUID_TYPE_128; memcpy(o->u128.val, u->b, 16); }
}
struct bond_q { const bt_addr_le_t* find; bool found; mcs_ble_addr_t* out; int max, n; };
static void bond_cb(const struct bt_bond_info* info, void* p) {
    struct bond_q* q = p;
    if (q->find && bt_addr_le_eq(q->find, &info->addr)) q->found = true;
    if (q->out && q->n < q->max) addr_in(&q->out[q->n], &info->addr);
    q->n++;
}
static bool is_bonded(const bt_addr_le_t* a) {
#if defined(CONFIG_BT_SMP)
    struct bond_q q = { .find = a };
    bt_foreach_bond(BT_ID_DEFAULT, bond_cb, &q);
    return q.found;
#else
    (void)a; return false;
#endif
}

/* ---------------------------------------------------------------- controller */
static void apply_auth(void);
static int z_enable(void* ctx, int on) {
    (void)ctx;
    if (!on) {
        if (!g_on) return 0;
        int rc = bt_disable();
        if (rc == 0) g_on = false;
        return st(rc);
    }
    if (g_on) return 0;
    int rc = bt_enable(NULL);                   /* blocks until the controller is ready */
    if (rc && rc != -EALREADY) return st(rc);
#if defined(CONFIG_BT_SETTINGS)
    settings_load();                            /* identity + bonds */
#endif
    g_on = true;
#if defined(CONFIG_BT_DEVICE_NAME_DYNAMIC)
    bt_set_name(g_name);
#endif
#if defined(CONFIG_BT_DEVICE_APPEARANCE_DYNAMIC)
    if (g_opt[MCS_BLE_OPT_APPEARANCE]) bt_set_appearance((uint16_t)g_opt[MCS_BLE_OPT_APPEARANCE]);
#endif
    apply_auth();
    return 0;
}
static int z_address(void* ctx, mcs_ble_addr_t* out) {
    (void)ctx;
    bt_addr_le_t a[CONFIG_BT_ID_MAX];
    size_t n = ARRAY_SIZE(a);
    bt_id_get(a, &n);
    if (!n) return MCS_HAL_EBUSY;
    addr_in(out, &a[0]);
    return 0;
}
static int z_set_name(void* ctx, const char* name) {
    (void)ctx;
    strncpy(g_name, name, sizeof g_name - 1); g_name[sizeof g_name - 1] = 0;
#if defined(CONFIG_BT_DEVICE_NAME_DYNAMIC)
    return g_on ? st(bt_set_name(g_name)) : 0;
#else
    return 0;                                   /* CONFIG_BT_DEVICE_NAME is fixed; still advertised */
#endif
}
static int z_opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    switch (opt) {
    case MCS_BLE_OPT_MTU: *v = g_opt[opt] ? g_opt[opt] : BT_L2CAP_RX_MTU; return 0;
    case MCS_BLE_OPT_MAX_CONN: *v = Z_MAX_CONN; return 0;
    case MCS_BLE_OPT_ADDR_TYPE: { mcs_ble_addr_t a; if (z_address(ctx, &a) < 0) return MCS_HAL_EBUSY; *v = a.type; return 0; }
    case MCS_BLE_OPT_TX_POWER: return MCS_HAL_ENOTSUP;
    default:
        if (opt <= 0 || opt >= MCS_BLE_OPT__COUNT) return MCS_HAL_ENOTSUP;
        *v = g_opt[opt]; return 0;
    }
}
static int z_opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_BLE_OPT_MTU: if (v < 23 || v > 517) return MCS_HAL_EINVAL; g_opt[opt] = v; return 0;   /* CONFIG_BT_L2CAP_TX_MTU decides */
    case MCS_BLE_OPT_APPEARANCE:
        g_opt[opt] = v;
#if defined(CONFIG_BT_DEVICE_APPEARANCE_DYNAMIC)
        return g_on ? st(bt_set_appearance((uint16_t)v)) : 0;
#else
        return 0;
#endif
    case MCS_BLE_OPT_IO_CAP: if (v < 0 || v > 4) return MCS_HAL_EINVAL; g_opt[opt] = v; if (g_on) apply_auth(); return 0;
    case MCS_BLE_OPT_BONDING:
        g_opt[opt] = v != 0;
#if defined(CONFIG_BT_SMP)
        bt_set_bondable(v != 0);
#endif
        return 0;
    case MCS_BLE_OPT_MITM: case MCS_BLE_OPT_SC: g_opt[opt] = v != 0; return 0;
    case MCS_BLE_OPT_PASSKEY:
        if (v > 999999) return MCS_HAL_EINVAL;
        g_opt[opt] = v;
#if defined(CONFIG_BT_FIXED_PASSKEY)
        return st(bt_passkey_set(v >= 0 ? (unsigned int)v : BT_PASSKEY_INVALID));
#else
        return v >= 0 ? MCS_HAL_ENOTSUP : 0;    /* needs CONFIG_BT_FIXED_PASSKEY */
#endif
    case MCS_BLE_OPT_PHY: if (v <= 0 || v > 7) return MCS_HAL_EINVAL; g_opt[opt] = v; return 0;
    default: return MCS_HAL_ENOTSUP;
    }
}
static void z_lock(void* ctx) { (void)ctx; k_spinlock_key_t k = k_spin_lock(&g_lock); g_key = k; }
static void z_unlock(void* ctx) { (void)ctx; k_spin_unlock(&g_lock, g_key); }

/* ---------------------------------------------------------------- advertising */
#if defined(CONFIG_BT_BROADCASTER)
static struct bt_data g_ad[16], g_sd[16];
static void adv_done(struct k_work* w) { (void)w; bt_le_adv_stop(); mcs_ble_on_event(MCS_BLE_EV_ADV_DONE, -1, 0); }
static K_WORK_DELAYABLE_DEFINE(g_adv_work, adv_done);
static size_t ad_split(struct bt_data* out, const uint8_t* p, size_t n) {   /* raw AD structures -> bt_data[] */
    size_t k = 0;
    for (size_t i = 0; i + 1 < n && k < 16;) {
        uint8_t len = p[i];
        if (!len || i + 1 + len > n) break;
        out[k].type = p[i + 1]; out[k].data_len = (uint8_t)(len - 1); out[k].data = &p[i + 2];
        k++; i += 1 + len;
    }
    return k;
}
static mcs_ble_adv_t g_adv;                  /* the bt_data entries point into it */
static int z_adv_start(void* ctx, const mcs_ble_adv_t* a) {
    (void)ctx;
    g_adv = *a;
    size_t nad = ad_split(g_ad, g_adv.data, g_adv.len), nsd = ad_split(g_sd, g_adv.rsp, g_adv.rsp_len);
    uint32_t ms = a->interval_ms ? a->interval_ms : 100;
    struct bt_le_adv_param p = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY, ms * 8 / 5, ms * 8 / 5, NULL);
#if defined(CONFIG_BT_PERIPHERAL)
    if (a->connectable) p.options |= BT_LE_ADV_OPT_CONN;   /* stops when a central connects */
#endif
    int rc = bt_le_adv_start(&p, g_ad, nad, a->connectable || a->scannable ? g_sd : NULL, a->connectable || a->scannable ? nsd : 0);
    if (rc == 0 && a->duration_ms) k_work_reschedule(&g_adv_work, K_MSEC(a->duration_ms));
    return st(rc);
}
static int z_adv_stop(void* ctx) { (void)ctx; k_work_cancel_delayable(&g_adv_work); return st(bt_le_adv_stop()); }
#endif

/* ---------------------------------------------------------------- scanning */
#if defined(CONFIG_BT_OBSERVER)
static void scan_done(struct k_work* w) { (void)w; bt_le_scan_stop(); mcs_ble_on_event(MCS_BLE_EV_SCAN_DONE, -1, 0); }
static K_WORK_DELAYABLE_DEFINE(g_scan_work, scan_done);
static void scan_cb(const bt_addr_le_t* addr, int8_t rssi, uint8_t type, struct net_buf_simple* buf) {
    mcs_ble_addr_t a; addr_in(&a, addr);
    mcs_ble_on_report(&a, rssi, type, buf->data, buf->len);   /* BT_GAP_ADV_TYPE_* == MCS_BLE_ADV_* */
}
static int z_scan_start(void* ctx, const mcs_ble_scan_t* s) {
    (void)ctx;
    struct bt_le_scan_param p;
    memset(&p, 0, sizeof p);
    p.type = s->active ? BT_LE_SCAN_TYPE_ACTIVE : BT_LE_SCAN_TYPE_PASSIVE;
    p.options = s->filter_duplicates ? BT_LE_SCAN_OPT_FILTER_DUPLICATE : BT_LE_SCAN_OPT_NONE;
    p.interval = s->interval_ms ? (uint16_t)(s->interval_ms * 8 / 5) : BT_GAP_SCAN_FAST_INTERVAL;
    p.window = s->window_ms ? (uint16_t)(s->window_ms * 8 / 5) : BT_GAP_SCAN_FAST_WINDOW;
    if (p.window > p.interval) p.window = p.interval;
    int rc = bt_le_scan_start(&p, scan_cb);
    if (rc == 0 && s->duration_ms) k_work_reschedule(&g_scan_work, K_MSEC(s->duration_ms));
    return st(rc);
}
static int z_scan_stop(void* ctx) {
    (void)ctx;
    k_work_cancel_delayable(&g_scan_work);
    int rc = bt_le_scan_stop();
    return rc == -EALREADY ? 0 : st(rc);
}
#endif

/* ---------------------------------------------------------------- GATT server (dynamic database) */
#if defined(CONFIG_BT_GATT_DYNAMIC_DB)
#define Z_ATTRS (MCS_BLE_MAX_SERVICES + MCS_BLE_MAX_ATTRS * 3)
static struct bt_gatt_attr z_attr[Z_ATTRS];
static struct bt_gatt_chrc z_chrc[MCS_BLE_MAX_ATTRS];
static struct _bt_gatt_ccc z_ccc[MCS_BLE_MAX_ATTRS];
static z_uuid_t z_uuid[MCS_BLE_MAX_SERVICES + MCS_BLE_MAX_ATTRS * 2];
static struct bt_gatt_service z_svc[MCS_BLE_MAX_SERVICES];
static struct bt_gatt_attr* z_val[MCS_BLE_MAX_ATTRS];   /* by front-end id */
static int z_nsvc;
static ssize_t z_read(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset) {
    int n = mcs_ble_attr_read((uint16_t)(uintptr_t)attr->user_data, connh(conn), offset, buf, len, NULL);
    if (n < 0) return BT_GATT_ERR(MCS_BLE_IS_ATT_ERR(n) ? MCS_BLE_ATT_CODE(n) : BT_ATT_ERR_UNLIKELY);
    return n;
}
static ssize_t z_write(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (flags & BT_GATT_WRITE_FLAG_PREPARE) return 0;
    int rc = mcs_ble_attr_write((uint16_t)(uintptr_t)attr->user_data, connh(conn), offset, buf, len);
    if (rc < 0) return BT_GATT_ERR(MCS_BLE_IS_ATT_ERR(rc) ? MCS_BLE_ATT_CODE(rc) : BT_ATT_ERR_UNLIKELY);
    return len;
}
static ssize_t z_ccc_write(struct bt_conn* conn, const struct bt_gatt_attr* attr, uint16_t value) {
    const struct bt_gatt_attr* v = attr - 1;      /* the CCC follows the characteristic value */
    mcs_ble_on_subscribe(connh(conn), (uint16_t)(uintptr_t)v->user_data, value & 3);
    return sizeof value;
}
static uint16_t perm_of(uint16_t p, bool rd, bool wr) {
    uint16_t m = 0;
    if (rd) m |= BT_GATT_PERM_READ;
    if (wr) m |= BT_GATT_PERM_WRITE;
    if (p & (MCS_BLE_PERM_ENCRYPTED | MCS_BLE_PERM_AUTHENTICATED)) {
        if (rd) m |= BT_GATT_PERM_READ_ENCRYPT;
        if (wr) m |= BT_GATT_PERM_WRITE_ENCRYPT;
    }
    if (p & MCS_BLE_PERM_AUTHENTICATED) {
        if (rd) m |= BT_GATT_PERM_READ_AUTHEN;
        if (wr) m |= BT_GATT_PERM_WRITE_AUTHEN;
    }
    return m;
}
static int z_gatts_set(void* ctx, mcs_ble_svc_def_t* svcs, int n) {
    (void)ctx;
    for (int i = 0; i < z_nsvc; i++) bt_gatt_service_unregister(&z_svc[i]);
    z_nsvc = 0;
    memset(z_attr, 0, sizeof z_attr); memset(z_chrc, 0, sizeof z_chrc); memset(z_ccc, 0, sizeof z_ccc);
    memset(z_svc, 0, sizeof z_svc); memset(z_val, 0, sizeof z_val);
    int ai = 0, ui = 0, ci = 0, ki = 0;
    for (int s = 0; s < n && s < MCS_BLE_MAX_SERVICES; s++) {
        mcs_ble_svc_def_t* sd = &svcs[s];
        int first = ai;
        uuid_out(&z_uuid[ui], &sd->uuid);
        if (sd->primary) z_attr[ai++] = (struct bt_gatt_attr)BT_GATT_PRIMARY_SERVICE(&z_uuid[ui].u);
        else z_attr[ai++] = (struct bt_gatt_attr)BT_GATT_SECONDARY_SERVICE(&z_uuid[ui].u);
        ui++;
        for (int c = 0; c < sd->nchr; c++) {
            mcs_ble_chr_def_t* cd = &sd->chr[c];
            if (ai + 3 + cd->ndsc > Z_ATTRS || ci >= MCS_BLE_MAX_ATTRS) return MCS_HAL_EINVAL;
            uuid_out(&z_uuid[ui], &cd->uuid);
            z_chrc[ci] = (struct bt_gatt_chrc)BT_GATT_CHRC_INIT(&z_uuid[ui].u, 0, (uint8_t)(cd->props & 0xFF));
            z_attr[ai++] = (struct bt_gatt_attr)BT_GATT_ATTRIBUTE(BT_UUID_GATT_CHRC, BT_GATT_PERM_READ, bt_gatt_attr_read_chrc, NULL, &z_chrc[ci]);
            ci++;
            bool rd = cd->props & MCS_BLE_PROP_READ, wr = cd->props & (MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR);
            z_attr[ai] = (struct bt_gatt_attr)BT_GATT_ATTRIBUTE(&z_uuid[ui].u, perm_of(cd->props, rd, wr), rd ? z_read : NULL, wr ? z_write : NULL,
                                                                (void*)(uintptr_t)cd->id);
            if (cd->id < MCS_BLE_MAX_ATTRS) z_val[cd->id] = &z_attr[ai];
            ai++; ui++;
            if (cd->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) {
                z_ccc[ki].cfg_write = z_ccc_write;
                z_attr[ai++] = (struct bt_gatt_attr)BT_GATT_CCC_MANAGED(&z_ccc[ki], perm_of(cd->props, true, true));
                ki++;
            }
            for (int d = 0; d < cd->ndsc; d++) {
                mcs_ble_dsc_def_t* dd = &cd->dsc[d];
                if (mcs_ble_uuid_short(&dd->uuid) == 0x2902) continue;   /* the CCC above */
                uuid_out(&z_uuid[ui], &dd->uuid);
                bool drd = dd->flags & MCS_BLE_DSC_READ, dwr = dd->flags & MCS_BLE_DSC_WRITE;
                z_attr[ai++] = (struct bt_gatt_attr)BT_GATT_DESCRIPTOR(&z_uuid[ui].u, perm_of(dd->flags, drd, dwr), drd ? z_read : NULL, dwr ? z_write : NULL,
                                                                       (void*)(uintptr_t)dd->id);
                ui++;
            }
        }
        z_svc[s].attrs = &z_attr[first];
        z_svc[s].attr_count = (size_t)(ai - first);
        int rc = bt_gatt_service_register(&z_svc[s]);
        if (rc) { for (int i = 0; i < s; i++) bt_gatt_service_unregister(&z_svc[i]); return st(rc); }
        z_nsvc = s + 1;
    }
    /* handles are assigned now */
    for (int s = 0; s < z_nsvc; s++) {
        struct bt_gatt_attr* at = z_svc[s].attrs;
        svcs[s].handle = at[0].handle;
        size_t k = 1;
        for (int c = 0; c < svcs[s].nchr; c++) {
            mcs_ble_chr_def_t* cd = &svcs[s].chr[c];
            k++;                                       /* declaration */
            cd->handle = at[k++].handle;
            cd->cccd_handle = 0;
            if (cd->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) cd->cccd_handle = at[k++].handle;
            for (int d = 0; d < cd->ndsc; d++) {
                if (mcs_ble_uuid_short(&cd->dsc[d].uuid) == 0x2902) { cd->dsc[d].handle = cd->cccd_handle; continue; }
                cd->dsc[d].handle = at[k++].handle;
            }
        }
    }
    return 0;
}
static struct bt_gatt_indicate_params z_ind[Z_MAX_CONN];
static atomic_t z_ind_busy[Z_MAX_CONN];
static void ind_cb(struct bt_conn* conn, struct bt_gatt_indicate_params* p, uint8_t err) {
    (void)p;
    mcs_ble_on_event(MCS_BLE_EV_INDICATE_DONE, connh(conn), att_st(err));
}
static void ind_destroy(struct bt_gatt_indicate_params* p) { atomic_clear(&z_ind_busy[p - z_ind]); }
static int z_gatts_notify(void* ctx, int conn, const mcs_ble_chr_def_t* chr, const uint8_t* data, size_t n, int indicate) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    if (chr->id >= MCS_BLE_MAX_ATTRS || !z_val[chr->id]) return MCS_HAL_EINVAL;
    if (!indicate) return st(bt_gatt_notify(c, z_val[chr->id], data, (uint16_t)n));
    int i = conn - 1;
    if (!atomic_cas(&z_ind_busy[i], 0, 1)) return MCS_HAL_EBUSY;
    memset(&z_ind[i], 0, sizeof z_ind[i]);
    z_ind[i].attr = z_val[chr->id]; z_ind[i].func = ind_cb; z_ind[i].destroy = ind_destroy;
    z_ind[i].data = data; z_ind[i].len = (uint16_t)n;   /* copied into the PDU by bt_gatt_indicate */
    int rc = bt_gatt_indicate(c, &z_ind[i]);
    if (rc) atomic_clear(&z_ind_busy[i]);
    return st(rc);
}
#endif

/* ---------------------------------------------------------------- connections */
#if defined(CONFIG_BT_CONN)
static struct bt_conn* g_pending;              /* bt_conn_le_create in progress */
static void on_connected(struct bt_conn* conn, uint8_t err) {
    if (err) {
        if (conn == g_pending) {
            bt_conn_unref(g_pending); g_pending = NULL;
            mcs_ble_on_event(MCS_BLE_EV_CONNECT_FAILED, -1, err);
            mcs_ble_op_done(err == BT_HCI_ERR_UNKNOWN_CONN_ID ? MCS_HAL_ETIMEOUT : MCS_HAL_ERR, 0);
        }
        return;
    }
    int i = bt_conn_index(conn);
    if (!g_conns[i]) g_conns[i] = bt_conn_ref(conn);
    if (conn == g_pending) { bt_conn_unref(g_pending); g_pending = NULL; }
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0) return;
    mcs_ble_addr_t a; addr_in(&a, info.le.dst);
    mcs_ble_on_connect(i + 1, &a, info.role == BT_CONN_ROLE_CENTRAL ? MCS_BLE_ROLE_CENTRAL : MCS_BLE_ROLE_PERIPHERAL);
}
static void on_disconnected(struct bt_conn* conn, uint8_t reason) {
    int i = bt_conn_index(conn);
    mcs_ble_on_disconnect(i + 1, reason);
    if (g_conns[i]) { bt_conn_unref(g_conns[i]); g_conns[i] = NULL; }
}
static void on_param_updated(struct bt_conn* conn, uint16_t interval, uint16_t latency, uint16_t timeout) {
    (void)latency; (void)timeout;
    mcs_ble_on_event(MCS_BLE_EV_CONN_UPDATE, connh(conn), interval);
}
#if defined(CONFIG_BT_SMP)
static void on_security(struct bt_conn* conn, bt_security_t level, enum bt_security_err err) {
    if (err) { mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, connh(conn), err); return; }
    if (level < BT_SECURITY_L2) return;
    mcs_ble_on_event(MCS_BLE_EV_ENCRYPTED, connh(conn), 1 + (level >= BT_SECURITY_L3) + is_bonded(bt_conn_get_dst(conn)));
}
#endif
#if defined(CONFIG_BT_USER_PHY_UPDATE)
static void on_phy(struct bt_conn* conn, struct bt_conn_le_phy_info* p) {
    mcs_ble_on_event(MCS_BLE_EV_PHY, connh(conn), p->tx_phy << 4 | p->rx_phy);
}
#endif
BT_CONN_CB_DEFINE(mcs_ble_conn_cb) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .le_param_updated = on_param_updated,
#if defined(CONFIG_BT_SMP)
    .security_changed = on_security,
#endif
#if defined(CONFIG_BT_USER_PHY_UPDATE)
    .le_phy_updated = on_phy,
#endif
};
static void mtu_updated(struct bt_conn* conn, uint16_t tx, uint16_t rx) {
    (void)tx; (void)rx;
    mcs_ble_on_event(MCS_BLE_EV_MTU, connh(conn), bt_gatt_get_mtu(conn));
}
static struct bt_gatt_cb g_gatt_cb = { .att_mtu_updated = mtu_updated };

static int z_disconnect(void* ctx, int conn) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    return c ? st(bt_conn_disconnect(c, BT_HCI_ERR_REMOTE_USER_TERM_CONN)) : MCS_HAL_ENODEV;
}
static int z_conn_info(void* ctx, int conn, mcs_ble_conn_info_t* o) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    struct bt_conn_info info;
    if (!c || bt_conn_get_info(c, &info) != 0) return MCS_HAL_ENODEV;
    addr_in(&o->peer, info.le.dst);
    o->role = info.role == BT_CONN_ROLE_CENTRAL ? MCS_BLE_ROLE_CENTRAL : MCS_BLE_ROLE_PERIPHERAL;
    o->encrypted = info.security.level >= BT_SECURITY_L2;
    o->authenticated = info.security.level >= BT_SECURITY_L3;
    o->bonded = is_bonded(info.le.dst);
    o->mtu = bt_gatt_get_mtu(c);
    o->interval = info.le.interval; o->latency = info.le.latency; o->timeout = info.le.timeout;
    return 0;
}
static int z_conn_rssi(void* ctx, int conn) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    uint16_t h;
    if (!c || bt_hci_get_conn_handle(c, &h) != 0) return MCS_HAL_ENODEV;
    struct net_buf* buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(struct bt_hci_cp_read_rssi));
    if (!buf) return MCS_HAL_EBUSY;
    struct bt_hci_cp_read_rssi* cp = net_buf_add(buf, sizeof *cp);
    cp->handle = sys_cpu_to_le16(h);
    struct net_buf* rsp = NULL;
    int rc = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
    if (rc) return st(rc);
    int rssi = ((struct bt_hci_rp_read_rssi*)rsp->data)->rssi;
    net_buf_unref(rsp);
    return rssi;
}
static int z_conn_update(void* ctx, int conn, int mn, int mx, int lat, int to) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    struct bt_le_conn_param p = BT_LE_CONN_PARAM_INIT(mn, mx, lat, to);
    return st(bt_conn_le_param_update(c, &p));
}
#endif

/* ---------------------------------------------------------------- central + GATT client */
#if defined(CONFIG_BT_CENTRAL) && defined(CONFIG_BT_GATT_CLIENT)
static int z_connect(void* ctx, const mcs_ble_addr_t* peer, uint32_t timeout_ms) {
    (void)ctx;
    if (g_pending) return MCS_HAL_EBUSY;
    bt_addr_le_t a; addr_out(&a, peer);
    bt_le_scan_stop();
    k_work_cancel_delayable(&g_scan_work);
    struct bt_conn_le_create_param cp = BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_INTERVAL);
    cp.timeout = (uint16_t)(timeout_ms / 10 > 0xFFFF ? 0xFFFF : timeout_ms / 10);
    return st(bt_conn_le_create(&a, &cp, BT_LE_CONN_PARAM_DEFAULT, &g_pending));
}
static int z_connect_cancel(void* ctx) {
    (void)ctx;
    if (!g_pending) return 0;
    int rc = bt_conn_disconnect(g_pending, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    return st(rc);
}
static struct bt_gatt_discover_params z_disc;
static uint8_t disc_cb(struct bt_conn* conn, const struct bt_gatt_attr* attr, struct bt_gatt_discover_params* p) {
    (void)conn;
    if (!attr) { mcs_ble_op_done(0, 0); return BT_GATT_ITER_STOP; }
    mcs_ble_attr_t a;
    memset(&a, 0, sizeof a);
    if (p->type == BT_GATT_DISCOVER_PRIMARY) {
        const struct bt_gatt_service_val* sv = attr->user_data;
        a.kind = MCS_BLE_DISC_SERVICES; uuid_in(&a.uuid, sv->uuid); a.handle = attr->handle; a.end = sv->end_handle;
    } else if (p->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        const struct bt_gatt_chrc* c = attr->user_data;
        a.kind = MCS_BLE_DISC_CHARACTERISTICS; uuid_in(&a.uuid, c->uuid); a.props = c->properties;
        a.handle = c->value_handle; a.end = attr->handle;
    } else {
        a.kind = MCS_BLE_DISC_DESCRIPTORS; uuid_in(&a.uuid, attr->uuid); a.handle = attr->handle;
    }
    mcs_ble_op_attr(&a);
    return BT_GATT_ITER_CONTINUE;
}
static int z_discover(void* ctx, int conn, int kind, uint16_t start, uint16_t end) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    memset(&z_disc, 0, sizeof z_disc);
    z_disc.func = disc_cb;
    z_disc.start_handle = kind == MCS_BLE_DISC_SERVICES ? BT_ATT_FIRST_ATTRIBUTE_HANDLE : start;
    z_disc.end_handle = kind == MCS_BLE_DISC_SERVICES ? BT_ATT_LAST_ATTRIBUTE_HANDLE : end;
    z_disc.type = kind == MCS_BLE_DISC_SERVICES ? BT_GATT_DISCOVER_PRIMARY
                : kind == MCS_BLE_DISC_CHARACTERISTICS ? BT_GATT_DISCOVER_CHARACTERISTIC : BT_GATT_DISCOVER_DESCRIPTOR;
    return st(bt_gatt_discover(c, &z_disc));
}
static struct bt_gatt_read_params z_rd;
static uint8_t read_cb(struct bt_conn* conn, uint8_t err, struct bt_gatt_read_params* p, const void* data, uint16_t len) {
    (void)conn; (void)p;
    if (err) { mcs_ble_op_done(att_st(err), 0); return BT_GATT_ITER_STOP; }
    if (!data) { mcs_ble_op_done(0, 0); return BT_GATT_ITER_STOP; }
    mcs_ble_op_data(data, len);
    return BT_GATT_ITER_CONTINUE;
}
static int z_read(void* ctx, int conn, uint16_t h) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    memset(&z_rd, 0, sizeof z_rd);
    z_rd.func = read_cb; z_rd.handle_count = 1; z_rd.single.handle = h; z_rd.single.offset = 0;
    return st(bt_gatt_read(c, &z_rd));
}
static struct bt_gatt_write_params z_wr;
static uint8_t z_wbuf[MCS_BLE_MAX_VALUE];      /* long writes send it in parts later */
static void write_cb(struct bt_conn* conn, uint8_t err, struct bt_gatt_write_params* p) { (void)conn; (void)p; mcs_ble_op_done(att_st(err), 0); }
static int z_write(void* ctx, int conn, uint16_t h, const uint8_t* data, size_t n, int with_response) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    if (n > sizeof z_wbuf) return MCS_HAL_EINVAL;
    if (!with_response) {
        int rc = bt_gatt_write_without_response(c, h, data, (uint16_t)n, false);
        if (rc == 0) mcs_ble_op_done(0, 0);
        return st(rc);
    }
    memcpy(z_wbuf, data, n);
    memset(&z_wr, 0, sizeof z_wr);
    z_wr.func = write_cb; z_wr.handle = h; z_wr.offset = 0; z_wr.data = z_wbuf; z_wr.length = (uint16_t)n;
    return st(bt_gatt_write(c, &z_wr));
}
static struct { struct bt_gatt_subscribe_params p; int conn; bool used; } z_sub[MCS_BLE_MAX_SUBS];
static uint8_t notify_cb(struct bt_conn* conn, struct bt_gatt_subscribe_params* p, const void* data, uint16_t len) {
    for (int i = 0; i < MCS_BLE_MAX_SUBS; i++)
        if (&z_sub[i].p == p && !data) { z_sub[i].used = false; mcs_ble_op_done(0, 0); return BT_GATT_ITER_STOP; }
    if (!data) return BT_GATT_ITER_STOP;
    mcs_ble_on_notify(connh(conn), p->value_handle, data, len, p->value == BT_GATT_CCC_INDICATE);
    return BT_GATT_ITER_CONTINUE;
}
static void sub_cb(struct bt_conn* conn, uint8_t err, struct bt_gatt_subscribe_params* p) { (void)conn; (void)p; mcs_ble_op_done(att_st(err), 0); }
static int z_subscribe(void* ctx, int conn, uint16_t vh, uint16_t cccd, int flags) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    int f = -1;
    for (int i = 0; i < MCS_BLE_MAX_SUBS; i++) if (z_sub[i].used && z_sub[i].conn == conn && z_sub[i].p.value_handle == vh) f = i;
    if (!flags) {
        if (f < 0) { mcs_ble_op_done(0, 0); return 0; }
        return st(bt_gatt_unsubscribe(c, &z_sub[f].p));
    }
    if (f >= 0) { mcs_ble_op_done(0, 0); return 0; }
    for (int i = 0; i < MCS_BLE_MAX_SUBS && f < 0; i++) if (!z_sub[i].used) f = i;
    if (f < 0) return MCS_HAL_EBUSY;
    memset(&z_sub[f].p, 0, sizeof z_sub[f].p);
    z_sub[f].p.notify = notify_cb; z_sub[f].p.subscribe = sub_cb;
    z_sub[f].p.value_handle = vh; z_sub[f].p.ccc_handle = cccd;
    z_sub[f].p.value = (flags & MCS_BLE_SUB_INDICATE) ? BT_GATT_CCC_INDICATE : BT_GATT_CCC_NOTIFY;
    z_sub[f].conn = conn; z_sub[f].used = true;
    int rc = bt_gatt_subscribe(c, &z_sub[f].p);
    if (rc == -EALREADY) { mcs_ble_op_done(0, 0); return 0; }
    if (rc) z_sub[f].used = false;
    return st(rc);
}
static struct bt_gatt_exchange_params z_xchg;
static void xchg_cb(struct bt_conn* conn, uint8_t err, struct bt_gatt_exchange_params* p) { (void)p; mcs_ble_op_done(att_st(err), bt_gatt_get_mtu(conn)); }
static int z_exchange_mtu(void* ctx, int conn) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    z_xchg.func = xchg_cb;
    int rc = bt_gatt_exchange_mtu(c, &z_xchg);
    if (rc == -EALREADY) { mcs_ble_op_done(0, bt_gatt_get_mtu(c)); return 0; }
    return st(rc);
}
#endif

/* ---------------------------------------------------------------- security */
#if defined(CONFIG_BT_SMP)
static void a_display(struct bt_conn* c, unsigned int key) { mcs_ble_on_passkey(connh(c), MCS_BLE_PASSKEY_DISPLAY, (int32_t)key); }
static void a_entry(struct bt_conn* c) { mcs_ble_on_passkey(connh(c), MCS_BLE_PASSKEY_ENTRY, 0); }
static void a_confirm(struct bt_conn* c, unsigned int key) { mcs_ble_on_passkey(connh(c), MCS_BLE_PASSKEY_COMPARE, (int32_t)key); }
static void a_pairing(struct bt_conn* c) { mcs_ble_on_passkey(connh(c), MCS_BLE_PASSKEY_CONFIRM, 0); }
static void a_cancel(struct bt_conn* c) { (void)c; }
static const struct bt_conn_auth_cb g_auth[5] = {
    [MCS_BLE_IO_DISPLAY_ONLY] = { .passkey_display = a_display, .cancel = a_cancel, .pairing_confirm = a_pairing },
    [MCS_BLE_IO_DISPLAY_YESNO] = { .passkey_display = a_display, .passkey_confirm = a_confirm, .cancel = a_cancel, .pairing_confirm = a_pairing },
    [MCS_BLE_IO_KEYBOARD_ONLY] = { .passkey_entry = a_entry, .cancel = a_cancel, .pairing_confirm = a_pairing },
    [MCS_BLE_IO_NONE] = { .cancel = a_cancel, .pairing_confirm = a_pairing },
    [MCS_BLE_IO_KEYBOARD_DISPLAY] = { .passkey_display = a_display, .passkey_entry = a_entry, .passkey_confirm = a_confirm, .cancel = a_cancel, .pairing_confirm = a_pairing },
};
static void p_complete(struct bt_conn* c, bool bonded) { (void)bonded; mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, connh(c), 0); }
static void p_failed(struct bt_conn* c, enum bt_security_err reason) { mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, connh(c), reason ? reason : 1); }
static struct bt_conn_auth_info_cb g_auth_info = { .pairing_complete = p_complete, .pairing_failed = p_failed };
static void apply_auth(void) {
    static bool info;
    if (!info) { info = true; bt_conn_auth_info_cb_register(&g_auth_info); }
    bt_conn_auth_cb_register(NULL);
    bt_conn_auth_cb_register(&g_auth[g_opt[MCS_BLE_OPT_IO_CAP]]);
    bt_set_bondable(g_opt[MCS_BLE_OPT_BONDING] != 0);
}
static int z_pair(void* ctx, int conn) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    bt_security_t lvl = !g_opt[MCS_BLE_OPT_MITM] ? BT_SECURITY_L2 : g_opt[MCS_BLE_OPT_SC] && IS_ENABLED(CONFIG_BT_SMP_SC_ONLY) ? BT_SECURITY_L4 : BT_SECURITY_L3;
    return st(bt_conn_set_security(c, lvl));
}
static int z_passkey_reply(void* ctx, int conn, int kind, int32_t value) {
    (void)ctx;
    struct bt_conn* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    switch (kind) {
    case MCS_BLE_PASSKEY_ENTRY: return st(bt_conn_auth_passkey_entry(c, (unsigned int)value));
    case MCS_BLE_PASSKEY_COMPARE: return st(value ? bt_conn_auth_passkey_confirm(c) : bt_conn_auth_cancel(c));
    case MCS_BLE_PASSKEY_CONFIRM: return st(value ? bt_conn_auth_pairing_confirm(c) : bt_conn_auth_cancel(c));
    default: return 0;
    }
}
static int z_bonds(void* ctx, int op, mcs_ble_addr_t* addrs, int max) {
    (void)ctx;
    if (op == MCS_BLE_BONDS_CLEAR) return st(bt_unpair(BT_ID_DEFAULT, NULL));
    if (op == MCS_BLE_BONDS_DELETE) { bt_addr_le_t a; addr_out(&a, addrs); return st(bt_unpair(BT_ID_DEFAULT, &a)); }
    struct bond_q q = { .out = op == MCS_BLE_BONDS_LIST ? addrs : NULL, .max = max };
    bt_foreach_bond(BT_ID_DEFAULT, bond_cb, &q);
    return q.n;
}
#else
static void apply_auth(void) {}
#endif

static mcs_ble_t g_ble = {
    .name = "zephyr",
    .enable = z_enable, .address = z_address, .set_name = z_set_name, .opt_get = z_opt_get, .opt_set = z_opt_set,
    .lock = z_lock, .unlock = z_unlock,
#if defined(CONFIG_BT_BROADCASTER)
    .adv_start = z_adv_start, .adv_stop = z_adv_stop,
#endif
#if defined(CONFIG_BT_OBSERVER)
    .scan_start = z_scan_start, .scan_stop = z_scan_stop,
#endif
#if defined(CONFIG_BT_GATT_DYNAMIC_DB) && defined(CONFIG_BT_PERIPHERAL)
    .gatts_set = z_gatts_set, .gatts_notify = z_gatts_notify,
#endif
#if defined(CONFIG_BT_CONN)
    .disconnect = z_disconnect, .conn_info = z_conn_info, .conn_rssi = z_conn_rssi, .conn_update = z_conn_update,
#endif
#if defined(CONFIG_BT_CENTRAL) && defined(CONFIG_BT_GATT_CLIENT)
    .connect = z_connect, .connect_cancel = z_connect_cancel, .discover = z_discover, .read = z_read, .write = z_write,
    .subscribe = z_subscribe, .exchange_mtu = z_exchange_mtu,
#endif
#if defined(CONFIG_BT_SMP)
    .pair = z_pair, .passkey_reply = z_passkey_reply, .bonds = z_bonds,
#endif
};
const mcs_ble_t* mcs_zephyr_ble(void) {
    static bool init;
    if (!init) {
        init = true;
        g_opt[MCS_BLE_OPT_IO_CAP] = MCS_BLE_IO_NONE;
        g_opt[MCS_BLE_OPT_BONDING] = 1;
        g_opt[MCS_BLE_OPT_SC] = 1;
        g_opt[MCS_BLE_OPT_PASSKEY] = -1;
        g_opt[MCS_BLE_OPT_PHY] = 1;
#if defined(CONFIG_BT_CONN)
        bt_gatt_cb_register(&g_gatt_cb);
#endif
    }
    return &g_ble;
}
#endif
