/*
 * MicroCS - ESP32 Bluetooth LE backend on the NimBLE host (ESP-IDF 5.x).
 *
 * menuconfig: Component config > Bluetooth (CONFIG_BT_ENABLED, host: NimBLE) and
 * MicroCS > Bluetooth LE (CONFIG_MICROCS_BLE). Legacy advertising only (keep
 * CONFIG_BT_NIMBLE_EXT_ADV off). See docs/BLE.md.
 *
 * Threads: the front end calls these functions on the VM task; NimBLE's host task
 * delivers GAP / GATT events to the callbacks below, which forward them through
 * mcs_ble_on_*() (a spinlock guards the shared queue). The local GATT database is
 * rebuilt with ble_gatts_reset() + ble_gatts_add_svcs() + ble_gatts_start(), which
 * NimBLE allows while nothing is connected and nothing advertises or scans.
 */
#include "mcs_port_esp32.h"
#if MCS_ESP32_BLE
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_bt.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "mcs_ble.h"
#include "mcs_hal.h"

#if MYNEWT_VAL(BLE_EXT_ADV)
#error "MicroCS BLE uses legacy advertising: turn CONFIG_BT_NIMBLE_EXT_ADV off"
#endif

void ble_store_config_init(void);    /* NimBLE's key store (NVS with CONFIG_BT_NIMBLE_NVS_PERSIST) */

static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t g_on, g_synced;
static uint8_t g_own_type = BLE_OWN_ADDR_PUBLIC, g_want_random;
static char g_name[32] = MCS_BLE_NAME;
static int32_t g_opt[MCS_BLE_OPT__COUNT];

/* ---------------------------------------------------------------- helpers */
static int st(int rc) {
    if (rc == 0) return 0;
    if (rc == BLE_HS_ETIMEOUT || rc == BLE_HS_ETIMEOUT_HCI) return MCS_HAL_ETIMEOUT;
    if (rc == BLE_HS_ENOTCONN) return MCS_HAL_ENODEV;
    if (rc == BLE_HS_EBUSY || rc == BLE_HS_EALREADY) return MCS_HAL_EBUSY;
    if (rc == BLE_HS_EINVAL) return MCS_HAL_EINVAL;
    if (rc == BLE_HS_ENOTSUP) return MCS_HAL_ENOTSUP;
    if (rc > BLE_HS_ERR_ATT_BASE && rc < BLE_HS_ERR_ATT_BASE + 0x100) return MCS_BLE_ATT_ERR(rc - BLE_HS_ERR_ATT_BASE);
    return MCS_HAL_ERR;
}
static int hci_reason(int r) { return r >= BLE_HS_ERR_HCI_BASE && r < BLE_HS_ERR_HCI_BASE + 0x100 ? r - BLE_HS_ERR_HCI_BASE : r; }
static void addr_in(mcs_ble_addr_t* o, const ble_addr_t* a) {
    o->type = a->type;
    for (int i = 0; i < 6; i++) o->a[i] = a->val[5 - i];
}
static void addr_out(ble_addr_t* o, const mcs_ble_addr_t* a) {
    o->type = a->type;
    for (int i = 0; i < 6; i++) o->val[i] = a->a[5 - i];
}
static void uuid_in(mcs_ble_uuid_t* o, const ble_uuid_t* u) {
    memset(o, 0, sizeof *o);
    if (u->type == BLE_UUID_TYPE_16) mcs_ble_uuid16(o, BLE_UUID16(u)->value);
    else if (u->type == BLE_UUID_TYPE_32) { uint32_t v = BLE_UUID32(u)->value; o->len = 4; for (int i = 0; i < 4; i++) o->b[i] = (uint8_t)(v >> (8 * i)); }
    else { o->len = 16; memcpy(o->b, BLE_UUID128(u)->value, 16); }
}
static void uuid_out(ble_uuid_any_t* o, const mcs_ble_uuid_t* u) {
    memset(o, 0, sizeof *o);
    if (u->len == 2) { o->u16.u.type = BLE_UUID_TYPE_16; o->u16.value = (uint16_t)(u->b[0] | u->b[1] << 8); }
    else if (u->len == 4) { o->u32.u.type = BLE_UUID_TYPE_32; o->u32.value = (uint32_t)u->b[0] | (uint32_t)u->b[1] << 8 | (uint32_t)u->b[2] << 16 | (uint32_t)u->b[3] << 24; }
    else { o->u128.u.type = BLE_UUID_TYPE_128; memcpy(o->u128.value, u->b, 16); }
}

/* ---------------------------------------------------------------- local GATT database */
#define N_CHR (MCS_BLE_MAX_ATTRS + MCS_BLE_MAX_SERVICES)
static struct ble_gatt_svc_def n_svc[MCS_BLE_MAX_SERVICES + 1];
static struct ble_gatt_chr_def n_chr[N_CHR];
static struct ble_gatt_dsc_def n_dsc[MCS_BLE_MAX_ATTRS * 2];
static ble_uuid_any_t n_uuid[MCS_BLE_MAX_SERVICES + MCS_BLE_MAX_ATTRS * 2];
static uint16_t n_valh[N_CHR];
static mcs_ble_svc_def_t* g_svcs;
static int g_nsvcs;
static mcs_ble_dsc_def_t* n_dsc_map[MCS_BLE_MAX_ATTRS * 2];
static uint8_t g_tmp[MCS_BLE_MAX_VALUE];   /* host task only */

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)attr;
    uint16_t id = (uint16_t)(uintptr_t)arg;
    int rc;
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
    case BLE_GATT_ACCESS_OP_READ_DSC:
        rc = mcs_ble_attr_read(id, conn, 0, g_tmp, sizeof g_tmp, NULL);
        if (rc < 0) return MCS_BLE_IS_ATT_ERR(rc) ? MCS_BLE_ATT_CODE(rc) : BLE_ATT_ERR_UNLIKELY;
        return os_mbuf_append(ctxt->om, g_tmp, (uint16_t)rc) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
        uint16_t n = OS_MBUF_PKTLEN(ctxt->om);
        if (n > sizeof g_tmp) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, g_tmp, sizeof g_tmp, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
        rc = mcs_ble_attr_write(id, conn, 0, g_tmp, n);
        if (rc < 0) return MCS_BLE_IS_ATT_ERR(rc) ? MCS_BLE_ATT_CODE(rc) : BLE_ATT_ERR_UNLIKELY;
        return 0;
    }
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static void register_cb(struct ble_gatt_register_ctxt* ctxt, void* arg) {
    (void)arg;
    if (ctxt->op == BLE_GATT_REGISTER_OP_SVC) {
        int i = (int)(ctxt->svc.svc_def - n_svc);
        if (ctxt->svc.svc_def >= n_svc && i < g_nsvcs) g_svcs[i].handle = ctxt->svc.handle;
    } else if (ctxt->op == BLE_GATT_REGISTER_OP_DSC) {
        int i = (int)(ctxt->dsc.dsc_def - n_dsc);
        if (ctxt->dsc.dsc_def >= n_dsc && i < (int)(sizeof n_dsc_map / sizeof n_dsc_map[0]) && n_dsc_map[i]) n_dsc_map[i]->handle = ctxt->dsc.handle;
    }
}
static uint16_t chr_flags(uint16_t p) {
    uint16_t f = p & 0x7F;
    bool rd = p & MCS_BLE_PROP_READ, wr = p & (MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR);
    if (p & (MCS_BLE_PERM_ENCRYPTED | MCS_BLE_PERM_AUTHENTICATED)) {
        if (rd) f |= BLE_GATT_CHR_F_READ_ENC;
        if (wr) f |= BLE_GATT_CHR_F_WRITE_ENC;
    }
    if (p & MCS_BLE_PERM_AUTHENTICATED) {
        if (rd) f |= BLE_GATT_CHR_F_READ_AUTHEN;
        if (wr) f |= BLE_GATT_CHR_F_WRITE_AUTHEN;
    }
    return f;
}
static uint8_t dsc_flags(uint16_t p) {
    uint8_t f = 0;
    if (p & MCS_BLE_DSC_READ) f |= BLE_ATT_F_READ;
    if (p & MCS_BLE_DSC_WRITE) f |= BLE_ATT_F_WRITE;
    if (p & (MCS_BLE_PERM_ENCRYPTED | MCS_BLE_PERM_AUTHENTICATED)) {
        if (f & BLE_ATT_F_READ) f |= BLE_ATT_F_READ_ENC;
        if (f & BLE_ATT_F_WRITE) f |= BLE_ATT_F_WRITE_ENC;
    }
    if (p & MCS_BLE_PERM_AUTHENTICATED) {
        if (f & BLE_ATT_F_READ) f |= BLE_ATT_F_READ_AUTHEN;
        if (f & BLE_ATT_F_WRITE) f |= BLE_ATT_F_WRITE_AUTHEN;
    }
    return f;
}
static int e_gatts_set(void* ctx, mcs_ble_svc_def_t* svcs, int n) {
    (void)ctx;
    if (!g_synced) return MCS_HAL_EBUSY;
    int rc = ble_gatts_reset();
    if (rc) return st(rc);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(g_name);
    if (g_opt[MCS_BLE_OPT_APPEARANCE]) ble_svc_gap_device_appearance_set((uint16_t)g_opt[MCS_BLE_OPT_APPEARANCE]);
    memset(n_svc, 0, sizeof n_svc); memset(n_chr, 0, sizeof n_chr); memset(n_dsc, 0, sizeof n_dsc);
    memset(n_dsc_map, 0, sizeof n_dsc_map);
    g_svcs = svcs; g_nsvcs = n;
    int ci = 0, di = 0, ui = 0;
    for (int s = 0; s < n && s < MCS_BLE_MAX_SERVICES; s++) {
        mcs_ble_svc_def_t* sd = &svcs[s];
        if (ci + sd->nchr + 1 > N_CHR) return MCS_HAL_EINVAL;
        uuid_out(&n_uuid[ui], &sd->uuid);
        n_svc[s].type = sd->primary ? BLE_GATT_SVC_TYPE_PRIMARY : BLE_GATT_SVC_TYPE_SECONDARY;
        n_svc[s].uuid = &n_uuid[ui++].u;
        n_svc[s].characteristics = &n_chr[ci];
        for (int c = 0; c < sd->nchr; c++, ci++) {
            mcs_ble_chr_def_t* cd = &sd->chr[c];
            uuid_out(&n_uuid[ui], &cd->uuid);
            n_chr[ci].uuid = &n_uuid[ui++].u;
            n_chr[ci].access_cb = access_cb;
            n_chr[ci].arg = (void*)(uintptr_t)cd->id;
            n_chr[ci].flags = chr_flags(cd->props);
            n_chr[ci].val_handle = &n_valh[ci];
            if (cd->ndsc) {
                n_chr[ci].descriptors = &n_dsc[di];
                for (int d = 0; d < cd->ndsc; d++) {
                    mcs_ble_dsc_def_t* dd = &cd->dsc[d];
                    if (mcs_ble_uuid_short(&dd->uuid) == 0x2902) continue;   /* NimBLE adds the CCCD itself */
                    if (di + 2 > (int)(sizeof n_dsc / sizeof n_dsc[0])) return MCS_HAL_EINVAL;
                    uuid_out(&n_uuid[ui], &dd->uuid);
                    n_dsc[di].uuid = &n_uuid[ui++].u;
                    n_dsc[di].att_flags = dsc_flags(dd->flags);
                    n_dsc[di].access_cb = access_cb;
                    n_dsc[di].arg = (void*)(uintptr_t)dd->id;
                    n_dsc_map[di++] = dd;
                }
                di++;                                      /* terminator */
            }
        }
        ci++;                                              /* terminator */
    }
    if (n > 0) {
        rc = ble_gatts_count_cfg(n_svc);
        if (!rc) rc = ble_gatts_add_svcs(n_svc);
        if (rc) return st(rc);
    }
    rc = ble_gatts_start();
    if (rc) return st(rc);
    /* value handles (the CCCD follows the value in NimBLE's layout) */
    ci = 0;
    for (int s = 0; s < n && s < MCS_BLE_MAX_SERVICES; s++, ci++)
        for (int c = 0; c < svcs[s].nchr; c++, ci++) {
            mcs_ble_chr_def_t* cd = &svcs[s].chr[c];
            cd->handle = n_valh[ci];
            cd->cccd_handle = cd->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE) ? (uint16_t)(n_valh[ci] + 1) : 0;
        }
    return 0;
}
static int id_of_handle(uint16_t h) {
    for (int s = 0; s < g_nsvcs; s++)
        for (int c = 0; c < g_svcs[s].nchr; c++) if (g_svcs[s].chr[c].handle == h) return g_svcs[s].chr[c].id;
    return -1;
}
static int e_gatts_notify(void* ctx, int conn, const mcs_ble_chr_def_t* chr, const uint8_t* data, size_t n, int indicate) {
    (void)ctx;
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)n);
    if (!om) return MCS_HAL_EBUSY;
    int rc = indicate ? ble_gatts_indicate_custom((uint16_t)conn, chr->handle, om) : ble_gatts_notify_custom((uint16_t)conn, chr->handle, om);
    return st(rc);
}

/* ---------------------------------------------------------------- GAP events */
static int gap_cb(struct ble_gap_event* ev, void* arg);
static void report_enc(uint16_t conn) {
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) == 0 && d.sec_state.encrypted)
        mcs_ble_on_event(MCS_BLE_EV_ENCRYPTED, conn, 1 + d.sec_state.authenticated + d.sec_state.bonded);
}
static int gap_cb(struct ble_gap_event* ev, void* arg) {
    (void)arg;
    struct ble_gap_conn_desc d;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0 && ble_gap_conn_find(ev->connect.conn_handle, &d) == 0) {
            mcs_ble_addr_t a; addr_in(&a, &d.peer_id_addr);
            int role = d.role == BLE_GAP_ROLE_MASTER ? MCS_BLE_ROLE_CENTRAL : MCS_BLE_ROLE_PERIPHERAL;
            mcs_ble_on_connect(ev->connect.conn_handle, &a, role);
        } else if (ev->connect.status != 0) {
            mcs_ble_on_event(MCS_BLE_EV_CONNECT_FAILED, -1, hci_reason(ev->connect.status));
            mcs_ble_op_done(st(ev->connect.status) ? st(ev->connect.status) : MCS_HAL_ERR, 0);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        mcs_ble_on_disconnect(ev->disconnect.conn.conn_handle, hci_reason(ev->disconnect.reason));
        return 0;
    case BLE_GAP_EVENT_DISC: {
        mcs_ble_addr_t a; addr_in(&a, &ev->disc.addr);
        mcs_ble_on_report(&a, ev->disc.rssi, ev->disc.event_type, ev->disc.data, ev->disc.length_data);
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE: mcs_ble_on_event(MCS_BLE_EV_SCAN_DONE, -1, 0); return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE: mcs_ble_on_event(MCS_BLE_EV_ADV_DONE, -1, 0); return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0) mcs_ble_on_event(MCS_BLE_EV_CONN_UPDATE, ev->conn_update.conn_handle, d.conn_itvl);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0) report_enc(ev->enc_change.conn_handle);
        else mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, ev->enc_change.conn_handle, hci_reason(ev->enc_change.status));
        return 0;
    case BLE_GAP_EVENT_PARING_COMPLETE:
        mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, ev->pairing_complete.conn_handle, ev->pairing_complete.status);
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        uint16_t c = ev->passkey.conn_handle;
        switch (ev->passkey.params.action) {
        case BLE_SM_IOACT_DISP: {
            struct ble_sm_io io; memset(&io, 0, sizeof io);
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = g_opt[MCS_BLE_OPT_PASSKEY] >= 0 ? (uint32_t)g_opt[MCS_BLE_OPT_PASSKEY] % 1000000 : esp_random() % 1000000;
            ble_sm_inject_io(c, &io);
            mcs_ble_on_passkey(c, MCS_BLE_PASSKEY_DISPLAY, (int32_t)io.passkey);
            break;
        }
        case BLE_SM_IOACT_INPUT: mcs_ble_on_passkey(c, MCS_BLE_PASSKEY_ENTRY, 0); break;
        case BLE_SM_IOACT_NUMCMP: mcs_ble_on_passkey(c, MCS_BLE_PASSKEY_COMPARE, (int32_t)ev->passkey.params.numcmp); break;
        default: break;
        }
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:            /* the peer lost its keys: forget ours and pair again */
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint16_t n = OS_MBUF_PKTLEN(ev->notify_rx.om);
        if (n > sizeof g_tmp) n = sizeof g_tmp;
        if (ble_hs_mbuf_to_flat(ev->notify_rx.om, g_tmp, n, &n) == 0)
            mcs_ble_on_notify(ev->notify_rx.conn_handle, ev->notify_rx.attr_handle, g_tmp, n, ev->notify_rx.indication);
        return 0;                                  /* NimBLE frees the mbuf */
    }
    case BLE_GAP_EVENT_NOTIFY_TX:
        if (ev->notify_tx.indication && ev->notify_tx.status != 0)
            mcs_ble_on_event(MCS_BLE_EV_INDICATE_DONE, ev->notify_tx.conn_handle, ev->notify_tx.status == BLE_HS_EDONE ? 0 : st(ev->notify_tx.status));
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        int id = id_of_handle(ev->subscribe.attr_handle);
        if (id >= 0) mcs_ble_on_subscribe(ev->subscribe.conn_handle, (uint16_t)id, ev->subscribe.cur_notify | ev->subscribe.cur_indicate << 1);
        return 0;
    }
    case BLE_GAP_EVENT_MTU: mcs_ble_on_event(MCS_BLE_EV_MTU, ev->mtu.conn_handle, ev->mtu.value); return 0;
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        if (ev->phy_updated.status == 0) mcs_ble_on_event(MCS_BLE_EV_PHY, ev->phy_updated.conn_handle, ev->phy_updated.tx_phy << 4 | ev->phy_updated.rx_phy);
        return 0;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- controller */
static void host_task(void* p) {
    (void)p;
    nimble_port_run();                         /* returns after nimble_port_stop() */
    nimble_port_freertos_deinit();
}
static void own_addr(void) {
    if (g_want_random) {
        ble_addr_t a;
        if (ble_hs_id_gen_rnd(0, &a) == 0 && ble_hs_id_set_rnd(a.val) == 0) { g_own_type = BLE_OWN_ADDR_RANDOM; return; }
    }
    if (ble_hs_id_infer_auto(0, &g_own_type) != 0) g_own_type = BLE_OWN_ADDR_PUBLIC;
}
static void on_sync(void) { ble_hs_util_ensure_addr(0); own_addr(); g_synced = 1; }
static void on_reset(int reason) { (void)reason; g_synced = 0; }
static void apply_sm(void) {
    ble_hs_cfg.sm_io_cap = (uint8_t)g_opt[MCS_BLE_OPT_IO_CAP];
    ble_hs_cfg.sm_bonding = g_opt[MCS_BLE_OPT_BONDING] != 0;
    ble_hs_cfg.sm_mitm = g_opt[MCS_BLE_OPT_MITM] != 0;
    ble_hs_cfg.sm_sc = g_opt[MCS_BLE_OPT_SC] != 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
}
static int tx_level(int dbm) {               /* ESP_PWR_LVL_*: 3 dB steps around 0 dBm */
    int l = ESP_PWR_LVL_N0 + (dbm >= 0 ? dbm + 1 : dbm - 1) / 3;
#if CONFIG_IDF_TARGET_ESP32
    int hi = ESP_PWR_LVL_P9;
#else
    int hi = 15;
#endif
    return l < 0 ? 0 : l > hi ? hi : l;
}
static int e_enable(void* ctx, int on) {
    (void)ctx;
    if (on && g_on) return 0;
    if (!on) {
        if (!g_on) return 0;
        nimble_port_stop();
        nimble_port_deinit();
        g_on = g_synced = 0;
        return 0;
    }
    esp_err_t e = nvs_flash_init();          /* bonds live in NVS (CONFIG_BT_NIMBLE_NVS_PERSIST) */
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    if (nimble_port_init() != ESP_OK) return MCS_HAL_ENODEV;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.gatts_register_cb = register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    apply_sm();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(g_name);
    if (g_opt[MCS_BLE_OPT_APPEARANCE]) ble_svc_gap_device_appearance_set((uint16_t)g_opt[MCS_BLE_OPT_APPEARANCE]);
    ble_store_config_init();
    g_svcs = NULL; g_nsvcs = 0;
    g_on = 1;
    nimble_port_freertos_init(host_task);
    for (int i = 0; i < 300 && !g_synced; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (!g_synced) return MCS_HAL_ETIMEOUT;
    if (g_opt[MCS_BLE_OPT_MTU]) ble_att_set_preferred_mtu((uint16_t)g_opt[MCS_BLE_OPT_MTU]);
    if (g_opt[MCS_BLE_OPT_TX_POWER] != 0x7F) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, (esp_power_level_t)tx_level(g_opt[MCS_BLE_OPT_TX_POWER]));
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)tx_level(g_opt[MCS_BLE_OPT_TX_POWER]));
    }
    return 0;
}
static int e_address(void* ctx, mcs_ble_addr_t* out) {
    (void)ctx;
    if (!g_synced) return MCS_HAL_EBUSY;
    uint8_t v[6];
    if (ble_hs_id_copy_addr(g_own_type, v, NULL) != 0) return MCS_HAL_ERR;
    out->type = g_own_type == BLE_OWN_ADDR_RANDOM ? MCS_BLE_ADDR_RANDOM : MCS_BLE_ADDR_PUBLIC;
    for (int i = 0; i < 6; i++) out->a[i] = v[5 - i];
    return 0;
}
static int e_set_name(void* ctx, const char* name) {
    (void)ctx;
    strncpy(g_name, name, sizeof g_name - 1); g_name[sizeof g_name - 1] = 0;
    return g_on ? st(ble_svc_gap_device_name_set(g_name)) : 0;
}
static int e_opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    if (opt <= 0 || opt >= MCS_BLE_OPT__COUNT) return MCS_HAL_ENOTSUP;
    switch (opt) {
    case MCS_BLE_OPT_TX_POWER:
        if (g_on) { int l = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV); if (l >= 0) { *v = (l - ESP_PWR_LVL_N0) * 3; return 0; } }
        *v = g_opt[opt] == 0x7F ? 0 : g_opt[opt]; return 0;
    case MCS_BLE_OPT_MTU: *v = g_opt[opt] ? g_opt[opt] : MYNEWT_VAL(BLE_ATT_PREFERRED_MTU); return 0;
    case MCS_BLE_OPT_MAX_CONN: *v = MYNEWT_VAL(BLE_MAX_CONNECTIONS); return 0;
    case MCS_BLE_OPT_ADDR_TYPE: *v = g_want_random ? MCS_BLE_ADDR_RANDOM : MCS_BLE_ADDR_PUBLIC; return 0;
    default: *v = g_opt[opt]; return 0;
    }
}
static int e_opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_BLE_OPT_TX_POWER:
        g_opt[opt] = v;
        if (g_on) {
            esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, (esp_power_level_t)tx_level(v));
            return esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)tx_level(v)) == ESP_OK ? 0 : MCS_HAL_EINVAL;
        }
        return 0;
    case MCS_BLE_OPT_MTU:
        if (v < 23 || v > 517) return MCS_HAL_EINVAL;
        g_opt[opt] = v;
        return g_on ? st(ble_att_set_preferred_mtu((uint16_t)v)) : 0;
    case MCS_BLE_OPT_APPEARANCE:
        g_opt[opt] = v;
        return g_on ? st(ble_svc_gap_device_appearance_set((uint16_t)v)) : 0;
    case MCS_BLE_OPT_IO_CAP: if (v < 0 || v > 4) return MCS_HAL_EINVAL; g_opt[opt] = v; apply_sm(); return 0;
    case MCS_BLE_OPT_BONDING: case MCS_BLE_OPT_MITM: case MCS_BLE_OPT_SC: g_opt[opt] = v != 0; apply_sm(); return 0;
    case MCS_BLE_OPT_PASSKEY: if (v > 999999) return MCS_HAL_EINVAL; g_opt[opt] = v; return 0;
    case MCS_BLE_OPT_ADDR_TYPE:
        if (v != MCS_BLE_ADDR_PUBLIC && v != MCS_BLE_ADDR_RANDOM) return MCS_HAL_EINVAL;
        g_want_random = v == MCS_BLE_ADDR_RANDOM;
        if (g_synced) own_addr();
        return 0;
    case MCS_BLE_OPT_PHY:
        if (v <= 0 || v > 7) return MCS_HAL_EINVAL;
        g_opt[opt] = v;
        return g_on ? st(ble_gap_set_prefered_default_le_phy((uint8_t)v, (uint8_t)v)) : 0;
    default: return MCS_HAL_ENOTSUP;
    }
}
static void e_lock(void* ctx) { (void)ctx; taskENTER_CRITICAL(&g_mux); }
static void e_unlock(void* ctx) { (void)ctx; taskEXIT_CRITICAL(&g_mux); }

/* ---------------------------------------------------------------- advertising / scanning */
static int e_adv_start(void* ctx, const mcs_ble_adv_t* a) {
    (void)ctx;
    if (!g_synced) return MCS_HAL_EBUSY;
    int rc = ble_gap_adv_set_data(a->data, a->len);
    if (!rc) rc = ble_gap_adv_rsp_set_data(a->rsp, a->rsp_len);
    if (rc) return st(rc);
    struct ble_gap_adv_params p; memset(&p, 0, sizeof p);
    p.conn_mode = a->connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    p.disc_mode = a->connectable || a->scannable ? BLE_GAP_DISC_MODE_GEN : BLE_GAP_DISC_MODE_NON;
    uint32_t ms = a->interval_ms ? a->interval_ms : 100;
    p.itvl_min = p.itvl_max = (uint16_t)(ms * 8 / 5);   /* 0.625 ms units */
    return st(ble_gap_adv_start(g_own_type, NULL, a->duration_ms ? (int32_t)a->duration_ms : BLE_HS_FOREVER, &p, gap_cb, NULL));
}
static int e_adv_stop(void* ctx) {
    (void)ctx;
    int rc = ble_gap_adv_stop();
    return rc == BLE_HS_EALREADY ? 0 : st(rc);
}
static int e_scan_start(void* ctx, const mcs_ble_scan_t* s) {
    (void)ctx;
    if (!g_synced) return MCS_HAL_EBUSY;
    struct ble_gap_disc_params p; memset(&p, 0, sizeof p);
    p.itvl = (uint16_t)(s->interval_ms * 8 / 5);
    p.window = (uint16_t)(s->window_ms * 8 / 5);
    p.passive = !s->active;
    p.filter_duplicates = s->filter_duplicates != 0;
    return st(ble_gap_disc(g_own_type, s->duration_ms ? (int32_t)s->duration_ms : BLE_HS_FOREVER, &p, gap_cb, NULL));
}
static int e_scan_stop(void* ctx) {
    (void)ctx;
    int rc = ble_gap_disc_cancel();
    return rc == BLE_HS_EALREADY ? 0 : st(rc);
}

/* ---------------------------------------------------------------- connections */
static int e_disconnect(void* ctx, int conn) { (void)ctx; return st(ble_gap_terminate((uint16_t)conn, BLE_ERR_REM_USER_CONN_TERM)); }
static int e_conn_info(void* ctx, int conn, mcs_ble_conn_info_t* o) {
    (void)ctx;
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find((uint16_t)conn, &d) != 0) return MCS_HAL_ENODEV;
    addr_in(&o->peer, &d.peer_id_addr);
    o->role = d.role == BLE_GAP_ROLE_MASTER ? MCS_BLE_ROLE_CENTRAL : MCS_BLE_ROLE_PERIPHERAL;
    o->encrypted = d.sec_state.encrypted; o->authenticated = d.sec_state.authenticated; o->bonded = d.sec_state.bonded;
    o->mtu = ble_att_mtu((uint16_t)conn);
    o->interval = d.conn_itvl; o->latency = d.conn_latency; o->timeout = d.supervision_timeout;
    return 0;
}
static int e_conn_rssi(void* ctx, int conn) {
    (void)ctx;
    int8_t r;
    int rc = ble_gap_conn_rssi((uint16_t)conn, &r);
    return rc ? st(rc) : r;
}
static int e_conn_update(void* ctx, int conn, int mn, int mx, int lat, int to) {
    (void)ctx;
    struct ble_gap_upd_params p; memset(&p, 0, sizeof p);
    p.itvl_min = (uint16_t)mn; p.itvl_max = (uint16_t)mx; p.latency = (uint16_t)lat; p.supervision_timeout = (uint16_t)to;
    return st(ble_gap_update_params((uint16_t)conn, &p));
}

/* ---------------------------------------------------------------- central + GATT client */
static int e_connect(void* ctx, const mcs_ble_addr_t* peer, uint32_t timeout_ms) {
    (void)ctx;
    if (!g_synced) return MCS_HAL_EBUSY;
    ble_addr_t a; addr_out(&a, peer);
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    return st(ble_gap_connect(g_own_type, &a, timeout_ms ? (int32_t)timeout_ms : BLE_HS_FOREVER, NULL, gap_cb, NULL));
}
static int e_connect_cancel(void* ctx) { (void)ctx; int rc = ble_gap_conn_cancel(); return rc == BLE_HS_EALREADY ? 0 : st(rc); }
static int done_err(const struct ble_gatt_error* e) {
    if (e->status == BLE_HS_EDONE) { mcs_ble_op_done(0, 0); return 0; }
    mcs_ble_op_done(st(e->status), 0);
    return 0;
}
static int svc_cb(uint16_t conn, const struct ble_gatt_error* e, const struct ble_gatt_svc* s, void* arg) {
    (void)conn; (void)arg;
    if (e->status != 0) return done_err(e);
    mcs_ble_attr_t a; memset(&a, 0, sizeof a);
    a.kind = MCS_BLE_DISC_SERVICES; uuid_in(&a.uuid, &s->uuid.u); a.handle = s->start_handle; a.end = s->end_handle;
    mcs_ble_op_attr(&a);
    return 0;
}
static int chr_cb(uint16_t conn, const struct ble_gatt_error* e, const struct ble_gatt_chr* c, void* arg) {
    (void)conn; (void)arg;
    if (e->status != 0) return done_err(e);
    mcs_ble_attr_t a; memset(&a, 0, sizeof a);
    a.kind = MCS_BLE_DISC_CHARACTERISTICS; a.props = c->properties; uuid_in(&a.uuid, &c->uuid.u);
    a.handle = c->val_handle; a.end = c->def_handle;
    mcs_ble_op_attr(&a);
    return 0;
}
static int dsc_cb(uint16_t conn, const struct ble_gatt_error* e, uint16_t chr, const struct ble_gatt_dsc* d, void* arg) {
    (void)conn; (void)chr; (void)arg;
    if (e->status != 0) return done_err(e);
    mcs_ble_attr_t a; memset(&a, 0, sizeof a);
    a.kind = MCS_BLE_DISC_DESCRIPTORS; uuid_in(&a.uuid, &d->uuid.u); a.handle = d->handle;
    mcs_ble_op_attr(&a);
    return 0;
}
static int e_discover(void* ctx, int conn, int kind, uint16_t start, uint16_t end) {
    (void)ctx;
    switch (kind) {
    case MCS_BLE_DISC_SERVICES: return st(ble_gattc_disc_all_svcs((uint16_t)conn, svc_cb, NULL));
    case MCS_BLE_DISC_CHARACTERISTICS: return st(ble_gattc_disc_all_chrs((uint16_t)conn, start, end, chr_cb, NULL));
    case MCS_BLE_DISC_DESCRIPTORS:          /* NimBLE starts after the characteristic value handle */
        return st(ble_gattc_disc_all_dscs((uint16_t)conn, (uint16_t)(start - 1), end, dsc_cb, NULL));
    default: return MCS_HAL_EINVAL;
    }
}
static int read_cb(uint16_t conn, const struct ble_gatt_error* e, struct ble_gatt_attr* at, void* arg) {
    (void)conn; (void)arg;
    if (e->status != 0) return done_err(e);
    if (at && at->om) {
        uint8_t buf[64];
        uint16_t n = OS_MBUF_PKTLEN(at->om), off = 0;
        while (off < n) {
            uint16_t k = n - off > sizeof buf ? sizeof buf : n - off;
            if (os_mbuf_copydata(at->om, off, k, buf) != 0) break;
            mcs_ble_op_data(buf, k);
            off += k;
        }
    }
    return 0;
}
static int e_read(void* ctx, int conn, uint16_t h) { (void)ctx; return st(ble_gattc_read_long((uint16_t)conn, h, 0, read_cb, NULL)); }
static int write_cb(uint16_t conn, const struct ble_gatt_error* e, struct ble_gatt_attr* at, void* arg) {
    (void)conn; (void)at; (void)arg;
    mcs_ble_op_done(e->status == 0 || e->status == BLE_HS_EDONE ? 0 : st(e->status), 0);
    return 0;
}
static int e_write(void* ctx, int conn, uint16_t h, const uint8_t* data, size_t n, int with_response) {
    (void)ctx;
    if (!with_response) {
        int rc = ble_gattc_write_no_rsp_flat((uint16_t)conn, h, data, (uint16_t)n);
        if (rc == 0) mcs_ble_op_done(0, 0);
        return st(rc);
    }
    if (n + 3 <= ble_att_mtu((uint16_t)conn)) return st(ble_gattc_write_flat((uint16_t)conn, h, data, (uint16_t)n, write_cb, NULL));
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)n);
    if (!om) return MCS_HAL_EBUSY;
    return st(ble_gattc_write_long((uint16_t)conn, h, 0, om, write_cb, NULL));
}
static int e_subscribe(void* ctx, int conn, uint16_t vh, uint16_t cccd, int flags) {
    (void)ctx; (void)vh;
    uint8_t v[2] = { (uint8_t)(flags & 3), 0 };
    return st(ble_gattc_write_flat((uint16_t)conn, cccd, v, 2, write_cb, NULL));
}
static int mtu_cb(uint16_t conn, const struct ble_gatt_error* e, uint16_t mtu, void* arg) {
    (void)conn; (void)arg;
    mcs_ble_op_done(e->status == 0 ? 0 : st(e->status), mtu);
    return 0;
}
static int e_exchange_mtu(void* ctx, int conn) {
    (void)ctx;
    int rc = ble_gattc_exchange_mtu((uint16_t)conn, mtu_cb, NULL);
    if (rc == BLE_HS_EALREADY) { mcs_ble_op_done(0, ble_att_mtu((uint16_t)conn)); return 0; }
    return st(rc);
}

/* ---------------------------------------------------------------- security */
static int e_pair(void* ctx, int conn) {
    (void)ctx;
    int rc = ble_gap_security_initiate((uint16_t)conn);
    if (rc == BLE_HS_EALREADY) { report_enc((uint16_t)conn); return 0; }
    return st(rc);
}
static int e_passkey_reply(void* ctx, int conn, int kind, int32_t value) {
    (void)ctx;
    struct ble_sm_io io; memset(&io, 0, sizeof io);
    if (kind == MCS_BLE_PASSKEY_ENTRY) { io.action = BLE_SM_IOACT_INPUT; io.passkey = (uint32_t)value; }
    else if (kind == MCS_BLE_PASSKEY_COMPARE) { io.action = BLE_SM_IOACT_NUMCMP; io.numcmp_accept = value != 0; }
    else return 0;                            /* display / just works: nothing to answer */
    return st(ble_sm_inject_io((uint16_t)conn, &io));
}
static int e_bonds(void* ctx, int op, mcs_ble_addr_t* addrs, int max) {
    (void)ctx;
    if (op == MCS_BLE_BONDS_CLEAR) return st(ble_store_clear());
    if (op == MCS_BLE_BONDS_DELETE) { ble_addr_t a; addr_out(&a, addrs); return st(ble_store_util_delete_peer(&a)); }
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int n = 0;
    int rc = ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS));
    if (rc) return st(rc);
    if (op == MCS_BLE_BONDS_LIST) for (int i = 0; i < n && i < max; i++) addr_in(&addrs[i], &peers[i]);
    return n;
}

static mcs_ble_t g_ble = {
    .name = "nimble",
    .enable = e_enable, .address = e_address, .set_name = e_set_name, .opt_get = e_opt_get, .opt_set = e_opt_set,
    .lock = e_lock, .unlock = e_unlock,
    .adv_start = e_adv_start, .adv_stop = e_adv_stop, .scan_start = e_scan_start, .scan_stop = e_scan_stop,
    .gatts_set = e_gatts_set, .gatts_notify = e_gatts_notify,
    .disconnect = e_disconnect, .conn_info = e_conn_info, .conn_rssi = e_conn_rssi, .conn_update = e_conn_update,
    .connect = e_connect, .connect_cancel = e_connect_cancel, .discover = e_discover, .read = e_read, .write = e_write,
    .subscribe = e_subscribe, .exchange_mtu = e_exchange_mtu,
    .pair = e_pair, .passkey_reply = e_passkey_reply, .bonds = e_bonds,
};
const mcs_ble_t* mcs_esp32_ble(void) {
    static uint8_t init;
    if (!init) {
        init = 1;
        g_opt[MCS_BLE_OPT_TX_POWER] = 0x7F;   /* not set: the controller default */
        g_opt[MCS_BLE_OPT_IO_CAP] = MCS_BLE_IO_NONE;
        g_opt[MCS_BLE_OPT_BONDING] = 1;
        g_opt[MCS_BLE_OPT_SC] = 1;
        g_opt[MCS_BLE_OPT_PASSKEY] = -1;
        g_opt[MCS_BLE_OPT_PHY] = 1;
    }
    return &g_ble;
}
#endif
