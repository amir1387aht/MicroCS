/*
 * MicroCS - simulated Bluetooth LE radio for the host CLI (`build/mcs_ble --sim`), the tests
 * and Studio (MCS_BLE_SIM, docs/BLE.md#simulator). Everything happens at once on the VM's
 * thread; mcs_ble_sim_tick() (called from the HAL poll) sends the periodic notifications.
 *
 *   C0:FF:EE:00:00:01  "MicroCS-HRM"  heart rate 180D (2A37 notify every second), battery 180F,
 *                                     device information 180A
 *   C0:FF:EE:00:00:02  "UART-Echo"    Nordic UART: every write to RX comes back on TX
 *   12:34:56:78:9A:BC  iBeacon        E2C56DB5-DFFB-48D2-B060-D0F5A71096E0, major 1, minor 42
 *   DE:AD:BE:EF:00:04  Eddystone-URL  https://github.com
 *   C0:FF:EE:00:00:05  "Thermo"       environmental sensing 181A: temperature 2A6E (read, indicate
 *                                     every 2 s), humidity 2A6F (needs an encrypted link: Pair() first)
 *   4C:57:A1:00:00:01  "Sim Phone"    connects to the board's connectable advertising, raises the
 *                                     MTU to 185, subscribes to every notify / indicate
 *                                     characteristic, writes "hello" to the first writable one and
 *                                     answers BleUart (Nordic UART TX) lines in upper case
 */
#include "mcs_ble.h"
#if MCS_ENABLE_BLE && MCS_BLE_SIM
#include "mcs_hal.h"
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------- the peers' GATT servers */
enum { K_SVC = 1, K_CHR, K_DSC };
typedef struct {
    uint8_t dev, kind, props, enc;     /* enc: reading needs an encrypted link */
    uint16_t handle, end;              /* service: last handle; characteristic: value handle - 1 = declaration */
    mcs_ble_uuid_t uuid;
    uint8_t len, val[32];
} sattr_t;
#define NATTR 40
static sattr_t g_db[NATTR];
static int g_ndb;
static uint16_t g_next_handle[6];

typedef struct {
    mcs_ble_addr_t addr;
    int8_t rssi;
    uint8_t type, adv_len, rsp_len;
    uint8_t adv[31], rsp[31];
} sdev_t;
#define NDEV 5
static sdev_t g_dev[NDEV];

static const uint8_t NUS[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
static mcs_ble_uuid_t u16(uint16_t v) { mcs_ble_uuid_t u; mcs_ble_uuid16(&u, v); return u; }
static mcs_ble_uuid_t nus(int k) { mcs_ble_uuid_t u; u.len = 16; memcpy(u.b, NUS, 16); u.b[12] = (uint8_t)k; return u; }
static int svc_at;
static void add_svc(int dev, mcs_ble_uuid_t u) {
    sattr_t* a = &g_db[g_ndb];
    memset(a, 0, sizeof *a);
    a->dev = (uint8_t)dev; a->kind = K_SVC; a->uuid = u; a->handle = ++g_next_handle[dev]; a->end = a->handle;
    svc_at = g_ndb++;
}
static int add_chr(int dev, mcs_ble_uuid_t u, int props, const void* v, int n) {
    g_next_handle[dev]++;                       /* declaration */
    sattr_t* a = &g_db[g_ndb];
    memset(a, 0, sizeof *a);
    a->dev = (uint8_t)dev; a->kind = K_CHR; a->uuid = u; a->props = (uint8_t)props; a->handle = ++g_next_handle[dev];
    a->end = (uint16_t)(a->handle - 1);
    a->len = (uint8_t)n; memcpy(a->val, v, (size_t)n);
    int i = g_ndb++;
    if (props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) {
        sattr_t* d = &g_db[g_ndb++];
        memset(d, 0, sizeof *d);
        d->dev = (uint8_t)dev; d->kind = K_DSC; d->uuid = u16(0x2902); d->handle = ++g_next_handle[dev]; d->len = 2;
    }
    g_db[svc_at].end = g_next_handle[dev];
    return i;
}
static void add_dsc(int dev, uint16_t uuid, const char* text) {
    sattr_t* d = &g_db[g_ndb++];
    memset(d, 0, sizeof *d);
    d->dev = (uint8_t)dev; d->kind = K_DSC; d->uuid = u16(uuid); d->handle = ++g_next_handle[dev];
    d->len = (uint8_t)strlen(text); memcpy(d->val, text, d->len);
    g_db[svc_at].end = g_next_handle[dev];
}
static int i_hr, i_batt, i_rx, i_tx, i_temp, i_hum;

static void ad(sdev_t* d, int rsp, int type, const void* p, int n) {
    uint8_t* b = rsp ? d->rsp : d->adv;
    uint8_t* len = rsp ? &d->rsp_len : &d->adv_len;
    b[(*len)++] = (uint8_t)(n + 1); b[(*len)++] = (uint8_t)type;
    memcpy(b + *len, p, (size_t)n); *len = (uint8_t)(*len + n);
}
static void devices_init(void) {
    static const uint8_t flags = 0x06;
    memset(g_dev, 0, sizeof g_dev); memset(g_db, 0, sizeof g_db); g_ndb = 0; memset(g_next_handle, 0, sizeof g_next_handle);
    /* 0: heart-rate sensor */
    sdev_t* d = &g_dev[0];
    d->addr = (mcs_ble_addr_t){ MCS_BLE_ADDR_RANDOM, { 0xC0, 0xFF, 0xEE, 0x00, 0x00, 0x01 } }; d->rssi = -48; d->type = MCS_BLE_ADV_IND;
    ad(d, 0, 0x01, &flags, 1);
    { static const uint8_t u[4] = { 0x0D, 0x18, 0x0F, 0x18 }; ad(d, 0, 0x03, u, 4); }
    { static const uint8_t a[2] = { 0x41, 0x03 }; ad(d, 0, 0x19, a, 2); }
    ad(d, 0, 0x09, "MicroCS-HRM", 11);
    { static const uint8_t tx = 4; ad(d, 1, 0x0A, &tx, 1); }
    add_svc(0, u16(0x1800)); add_chr(0, u16(0x2A00), MCS_BLE_PROP_READ, "MicroCS-HRM", 11);
    add_svc(0, u16(0x180D));
    { static const uint8_t hr[2] = { 0x00, 72 }, loc = 1;
      i_hr = add_chr(0, u16(0x2A37), MCS_BLE_PROP_NOTIFY, hr, 2); add_chr(0, u16(0x2A38), MCS_BLE_PROP_READ, &loc, 1); }
    add_svc(0, u16(0x180F));
    { static const uint8_t lvl = 87; i_batt = add_chr(0, u16(0x2A19), MCS_BLE_PROP_READ | MCS_BLE_PROP_NOTIFY, &lvl, 1); }
    add_svc(0, u16(0x180A));
    add_chr(0, u16(0x2A29), MCS_BLE_PROP_READ, "MicroCS Labs", 12); add_chr(0, u16(0x2A24), MCS_BLE_PROP_READ, "HRM-1", 5);
    /* 1: Nordic UART echo */
    d = &g_dev[1];
    d->addr = (mcs_ble_addr_t){ MCS_BLE_ADDR_RANDOM, { 0xC0, 0xFF, 0xEE, 0x00, 0x00, 0x02 } }; d->rssi = -60; d->type = MCS_BLE_ADV_IND;
    ad(d, 0, 0x01, &flags, 1); ad(d, 0, 0x07, NUS, 16); ad(d, 1, 0x09, "UART-Echo", 9);
    add_svc(1, nus(1));
    i_rx = add_chr(1, nus(2), MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR, "", 0);
    i_tx = add_chr(1, nus(3), MCS_BLE_PROP_NOTIFY, "", 0);
    /* 2: iBeacon */
    d = &g_dev[2];
    d->addr = (mcs_ble_addr_t){ MCS_BLE_ADDR_PUBLIC, { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC } }; d->rssi = -70; d->type = MCS_BLE_ADV_NONCONN_IND;
    ad(d, 0, 0x01, &flags, 1);
    { static const uint8_t m[25] = { 0x4C, 0x00, 0x02, 0x15, 0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2, 0xB0, 0x60,
                                     0xD0, 0xF5, 0xA7, 0x10, 0x96, 0xE0, 0x00, 0x01, 0x00, 0x2A, 0xC5 };
      ad(d, 0, 0xFF, m, 25); }
    /* 3: Eddystone-URL */
    d = &g_dev[3];
    d->addr = (mcs_ble_addr_t){ MCS_BLE_ADDR_RANDOM, { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x04 } }; d->rssi = -75; d->type = MCS_BLE_ADV_NONCONN_IND;
    ad(d, 0, 0x01, &flags, 1);
    { static const uint8_t u[2] = { 0xAA, 0xFE }; ad(d, 0, 0x03, u, 2); }
    { static const uint8_t s[12] = { 0xAA, 0xFE, 0x10, 0xEC, 0x03, 'g', 'i', 't', 'h', 'u', 'b', 0x07 }; ad(d, 0, 0x16, s, 12); }
    /* 4: thermometer */
    d = &g_dev[4];
    d->addr = (mcs_ble_addr_t){ MCS_BLE_ADDR_RANDOM, { 0xC0, 0xFF, 0xEE, 0x00, 0x00, 0x05 } }; d->rssi = -82; d->type = MCS_BLE_ADV_IND;
    ad(d, 0, 0x01, &flags, 1);
    { static const uint8_t u[2] = { 0x1A, 0x18 }; ad(d, 0, 0x03, u, 2); }
    { static const uint8_t s[4] = { 0x1A, 0x18, 0x6E, 0x08 }; ad(d, 0, 0x16, s, 4); }
    ad(d, 0, 0x09, "Thermo", 6);
    add_svc(4, u16(0x181A));
    { static const uint8_t t[2] = { 0x6E, 0x08 }, h[2] = { 0x94, 0x11 };
      i_temp = add_chr(4, u16(0x2A6E), MCS_BLE_PROP_READ | MCS_BLE_PROP_INDICATE, t, 2); add_dsc(4, 0x2901, "Temperature");
      i_hum = add_chr(4, u16(0x2A6F), MCS_BLE_PROP_READ, h, 2); g_db[i_hum].enc = 1; }
}

/* ---------------------------------------------------------------- the radio */
#define PHONE_CONN 64
static const mcs_ble_addr_t PHONE = { MCS_BLE_ADDR_RANDOM, { 0x4C, 0x57, 0xA1, 0x00, 0x00, 0x01 } };
typedef struct {
    uint8_t used, role, enc, auth, bonded, pairing;
    int8_t dev;                        /* peer device index, -1 = the phone */
    int handle;
    uint16_t mtu, interval;
    uint8_t subs[NATTR];               /* our subscriptions on the peer (central role) */
    int pair_kind;
} sconn_t;
static struct {
    int on;
    char name[30];
    int32_t opt[MCS_BLE_OPT__COUNT];
    int advertising, adv_conn;
    int scanning, scan_active, scan_dup;
    uint32_t scan_start, scan_dur, last_report, now;
    sconn_t conn[4];
    int next_handle;
    mcs_ble_svc_def_t* svcs; int nsvc;
    mcs_ble_addr_t bonds[8]; int nbonds;
    uint32_t last_hr, last_temp, last_batt;
    uint8_t bpm, batt, started;
    uint32_t rounds;
} g;

static sconn_t* conn_of(int h) { for (int i = 0; i < 4; i++) if (g.conn[i].used && g.conn[i].handle == h) return &g.conn[i]; return NULL; }
static sconn_t* conn_new(int h, int dev, int role) {
    for (int i = 0; i < 4; i++) if (!g.conn[i].used) {
        sconn_t* c = &g.conn[i];
        memset(c, 0, sizeof *c);
        c->used = 1; c->handle = h; c->dev = (int8_t)dev; c->role = (uint8_t)role; c->mtu = 23; c->interval = 24;
        return c;
    }
    return NULL;
}

static int s_enable(void* ctx, int on) {
    (void)ctx;
    if (!on) {
        for (int i = 0; i < 4; i++) g.conn[i].used = 0;
        g.advertising = g.scanning = 0;
    }
    g.on = on;
    return 0;
}
static int s_address(void* ctx, mcs_ble_addr_t* out) {
    (void)ctx;
    *out = (mcs_ble_addr_t){ (uint8_t)g.opt[MCS_BLE_OPT_ADDR_TYPE], { 0x24, 0x0A, 0xC4, 0xBE, 0xEE, 0x01 } };
    if (out->type == MCS_BLE_ADDR_RANDOM) out->a[0] = 0xE4;   /* static random: top bits 11 */
    return 0;
}
static int s_set_name(void* ctx, const char* n) { (void)ctx; snprintf(g.name, sizeof g.name, "%s", n); return 0; }
static int s_opt_get(void* ctx, int opt, int32_t* v) {
    (void)ctx;
    if (opt <= 0 || opt >= MCS_BLE_OPT__COUNT) return MCS_HAL_ENOTSUP;
    *v = g.opt[opt];
    return 0;
}
static int s_opt_set(void* ctx, int opt, int32_t v) {
    (void)ctx;
    switch (opt) {
    case MCS_BLE_OPT_TX_POWER: if (v < -24 || v > 20) return MCS_HAL_EINVAL; break;
    case MCS_BLE_OPT_MTU: if (v < 23 || v > 517) return MCS_HAL_EINVAL; break;
    case MCS_BLE_OPT_IO_CAP: if (v < 0 || v > 4) return MCS_HAL_EINVAL; break;
    case MCS_BLE_OPT_MAX_CONN: return MCS_HAL_ENOTSUP;
    default: if (opt <= 0 || opt >= MCS_BLE_OPT__COUNT) return MCS_HAL_ENOTSUP;
    }
    g.opt[opt] = v;
    return 0;
}

/* ---- the phone: a central that finds the board's advertising */
static void phone_connect(void) {
    if (conn_of(PHONE_CONN)) return;
    sconn_t* c = conn_new(PHONE_CONN, -1, MCS_BLE_ROLE_PERIPHERAL);
    if (!c) return;
    g.advertising = 0;               /* legacy advertising stops on a connection */
    mcs_ble_on_connect(PHONE_CONN, &PHONE, MCS_BLE_ROLE_PERIPHERAL);
    c->mtu = 185;
    mcs_ble_on_event(MCS_BLE_EV_MTU, PHONE_CONN, 185);
    int wrote = 0;
    for (int i = 0; i < g.nsvc; i++)
        for (int k = 0; k < g.svcs[i].nchr; k++) {
            const mcs_ble_chr_def_t* ch = &g.svcs[i].chr[k];
            if (ch->props & MCS_BLE_PROP_NOTIFY) mcs_ble_on_subscribe(PHONE_CONN, ch->id, MCS_BLE_SUB_NOTIFY);
            else if (ch->props & MCS_BLE_PROP_INDICATE) mcs_ble_on_subscribe(PHONE_CONN, ch->id, MCS_BLE_SUB_INDICATE);
            if (!wrote && (ch->props & (MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR))
                && !(ch->props & (MCS_BLE_PERM_ENCRYPTED | MCS_BLE_PERM_AUTHENTICATED))) {
                mcs_ble_attr_write(ch->id, PHONE_CONN, 0, (const uint8_t*)"hello", 5);
                wrote = 1;
            }
        }
}
static int s_adv_start(void* ctx, const mcs_ble_adv_t* a) {
    (void)ctx;
    if (!g.on) return MCS_HAL_EINVAL;
    if (a->len > 31 || a->rsp_len > 31) return MCS_HAL_EINVAL;
    g.advertising = 1; g.adv_conn = a->connectable;
    if (a->connectable) phone_connect();
    return 0;
}
static int s_adv_stop(void* ctx) { (void)ctx; g.advertising = 0; return 0; }

/* ---- observer */
static void report_all(int jitter) {
    for (int i = 0; i < NDEV; i++) {
        sdev_t* d = &g_dev[i];
        int rssi = d->rssi + (jitter ? (int)((g.rounds + (uint32_t)i) % 5) - 2 : 0);
        mcs_ble_on_report(&d->addr, rssi, d->type, d->adv, d->adv_len);
        if (g.scan_active && d->rsp_len && d->type != MCS_BLE_ADV_NONCONN_IND)
            mcs_ble_on_report(&d->addr, rssi, MCS_BLE_ADV_SCAN_RSP, d->rsp, d->rsp_len);
    }
}
static int s_scan_start(void* ctx, const mcs_ble_scan_t* p) {
    (void)ctx;
    if (!g.on) return MCS_HAL_EINVAL;
    g.scanning = 1; g.scan_active = p->active; g.scan_dup = !p->filter_duplicates;
    g.scan_start = g.last_report = g.now; g.scan_dur = p->duration_ms;
    report_all(0);
    return 0;
}
static int s_scan_stop(void* ctx) { (void)ctx; g.scanning = 0; return 0; }

/* ---- GATT server: handles in declaration order from 0x0010 */
static int s_gatts_set(void* ctx, mcs_ble_svc_def_t* svcs, int n) {
    (void)ctx;
    for (int i = 0; i < 4; i++) if (g.conn[i].used) return MCS_HAL_EBUSY;
    uint16_t h = 0x0010;
    for (int i = 0; i < n; i++) {
        svcs[i].handle = h++;
        for (int k = 0; k < svcs[i].nchr; k++) {
            mcs_ble_chr_def_t* c = &svcs[i].chr[k];
            h++;                               /* declaration */
            c->handle = h++;
            c->cccd_handle = (c->props & (MCS_BLE_PROP_NOTIFY | MCS_BLE_PROP_INDICATE)) ? h++ : 0;
            for (int j = 0; j < c->ndsc; j++) c->dsc[j].handle = h++;
        }
    }
    g.svcs = svcs; g.nsvc = n;
    return 0;
}
static int s_gatts_notify(void* ctx, int conn, const mcs_ble_chr_def_t* chr, const uint8_t* data, size_t n, int indicate) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    if (indicate) mcs_ble_on_event(MCS_BLE_EV_INDICATE_DONE, conn, 0);
    /* the phone answers Nordic UART lines in upper case */
    if (conn == PHONE_CONN && chr->uuid.len == 16 && !memcmp(chr->uuid.b, NUS, 12) && chr->uuid.b[12] == 3) {
        for (int i = 0; i < g.nsvc; i++)
            for (int k = 0; k < g.svcs[i].nchr; k++) {
                const mcs_ble_chr_def_t* rx = &g.svcs[i].chr[k];
                if (rx->uuid.len != 16 || memcmp(rx->uuid.b, NUS, 12) || rx->uuid.b[12] != 2) continue;
                uint8_t up[256]; size_t m = n > sizeof up ? sizeof up : n;
                for (size_t j = 0; j < m; j++) up[j] = (uint8_t)(data[j] >= 'a' && data[j] <= 'z' ? data[j] - 32 : data[j]);
                mcs_ble_attr_write(rx->id, conn, 0, up, m);
            }
    }
    return 0;
}

/* ---- connections */
static int s_disconnect(void* ctx, int conn) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    c->used = 0;
    mcs_ble_on_disconnect(conn, 0x16);   /* connection terminated by local host */
    return 0;
}
static int s_conn_info(void* ctx, int conn, mcs_ble_conn_info_t* o) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    o->peer = c->dev < 0 ? PHONE : g_dev[c->dev].addr;
    o->role = c->role; o->encrypted = c->enc; o->authenticated = c->auth; o->bonded = c->bonded;
    o->mtu = c->mtu; o->interval = c->interval; o->latency = 0; o->timeout = 400;
    return 0;
}
static int s_conn_rssi(void* ctx, int conn) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    return c->dev < 0 ? -40 : g_dev[c->dev].rssi + 2;
}
static int s_conn_update(void* ctx, int conn, int mn, int mx, int lat, int to) {
    (void)ctx; (void)mn; (void)lat; (void)to;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    c->interval = (uint16_t)mx;
    mcs_ble_on_event(MCS_BLE_EV_CONN_UPDATE, conn, mx);
    return 0;
}

/* ---- central + GATT client */
static int s_connect(void* ctx, const mcs_ble_addr_t* peer, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!g.on) return MCS_HAL_EINVAL;
    for (int i = 0; i < NDEV; i++) {
        if (memcmp(g_dev[i].addr.a, peer->a, 6)) continue;
        if (g_dev[i].type != MCS_BLE_ADV_IND) { mcs_ble_op_done(MCS_HAL_ETIMEOUT, 0); return 0; }   /* a beacon never answers */
        int h = ++g.next_handle;
        sconn_t* c = conn_new(h, i, MCS_BLE_ROLE_CENTRAL);
        if (!c) return MCS_HAL_EBUSY;
        mcs_ble_on_connect(h, &g_dev[i].addr, MCS_BLE_ROLE_CENTRAL);
        return 0;
    }
    mcs_ble_op_done(MCS_HAL_ETIMEOUT, 0);   /* nobody there */
    return 0;
}
static int s_connect_cancel(void* ctx) { (void)ctx; return 0; }
static int s_discover(void* ctx, int conn, int kind, uint16_t start, uint16_t end) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c || c->dev < 0) return MCS_HAL_ENODEV;
    for (int i = 0; i < g_ndb; i++) {
        sattr_t* a = &g_db[i];
        if (a->dev != c->dev || a->handle < start || a->handle > end) continue;
        mcs_ble_attr_t o; memset(&o, 0, sizeof o);
        o.uuid = a->uuid; o.handle = a->handle;
        if (kind == MCS_BLE_DISC_SERVICES && a->kind == K_SVC) { o.kind = MCS_BLE_DISC_SERVICES; o.end = a->end; }
        else if (kind == MCS_BLE_DISC_CHARACTERISTICS && a->kind == K_CHR) { o.kind = MCS_BLE_DISC_CHARACTERISTICS; o.props = a->props; o.end = a->end; }
        else if (kind == MCS_BLE_DISC_DESCRIPTORS && a->kind == K_DSC) o.kind = MCS_BLE_DISC_DESCRIPTORS;
        else continue;
        mcs_ble_op_attr(&o);
    }
    mcs_ble_op_done(0, 0);
    return 0;
}
static sattr_t* find(sconn_t* c, uint16_t handle) {
    for (int i = 0; i < g_ndb; i++) if (g_db[i].dev == c->dev && g_db[i].handle == handle && g_db[i].kind != K_SVC) return &g_db[i];
    return NULL;
}
static int s_read(void* ctx, int conn, uint16_t handle) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c || c->dev < 0) return MCS_HAL_ENODEV;
    sattr_t* a = find(c, handle);
    if (!a) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x01), 0); return 0; }
    if (a->kind == K_CHR && !(a->props & MCS_BLE_PROP_READ)) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x02), 0); return 0; }
    if (a->enc && !c->enc) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x0F), 0); return 0; }
    mcs_ble_op_data(a->val, a->len);
    mcs_ble_op_done(0, 0);
    return 0;
}
static void notify_peer(sconn_t* c, int idx) {
    sattr_t* a = &g_db[idx];
    if (c->subs[idx] & MCS_BLE_SUB_NOTIFY) mcs_ble_on_notify(c->handle, a->handle, a->val, a->len, 0);
    else if (c->subs[idx] & MCS_BLE_SUB_INDICATE) mcs_ble_on_notify(c->handle, a->handle, a->val, a->len, 1);
}
static int s_write(void* ctx, int conn, uint16_t handle, const uint8_t* d, size_t n, int resp) {
    (void)ctx; (void)resp;
    sconn_t* c = conn_of(conn);
    if (!c || c->dev < 0) return MCS_HAL_ENODEV;
    sattr_t* a = find(c, handle);
    if (!a) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x01), 0); return 0; }
    if (a->kind == K_CHR && !(a->props & (MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR))) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x03), 0); return 0; }
    if (n > sizeof a->val) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x0D), 0); return 0; }
    memcpy(a->val, d, n); a->len = (uint8_t)n;
    mcs_ble_op_done(0, 0);
    if (a == &g_db[i_rx]) {          /* UART-Echo */
        memcpy(g_db[i_tx].val, d, n); g_db[i_tx].len = (uint8_t)n;
        notify_peer(c, i_tx);
    }
    return 0;
}
static int s_subscribe(void* ctx, int conn, uint16_t vh, uint16_t cccd, int flags) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c || c->dev < 0) return MCS_HAL_ENODEV;
    sattr_t* d = find(c, cccd);
    if (!d || mcs_ble_uuid_short(&d->uuid) != 0x2902) { mcs_ble_op_done(MCS_BLE_ATT_ERR(0x01), 0); return 0; }
    for (int i = 0; i < g_ndb; i++) if (g_db[i].dev == c->dev && g_db[i].handle == vh) c->subs[i] = (uint8_t)flags;
    d->val[0] = (uint8_t)flags; d->val[1] = 0;
    mcs_ble_op_done(0, 0);
    return 0;
}
static int s_exchange_mtu(void* ctx, int conn) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    int want = g.opt[MCS_BLE_OPT_MTU];
    c->mtu = (uint16_t)(want < 247 ? want : 247);
    mcs_ble_on_event(MCS_BLE_EV_MTU, conn, c->mtu);
    mcs_ble_op_done(0, c->mtu);
    return 0;
}

/* ---- security: passkey 123456 for every peer */
static void pair_done(sconn_t* c, int ok) {
    c->pairing = 0;
    if (!ok) { mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, c->handle, c->pair_kind == MCS_BLE_PASSKEY_ENTRY ? 0x01 : 0x04); return; }
    c->enc = 1; c->auth = c->pair_kind == MCS_BLE_PASSKEY_COMPARE || c->pair_kind == MCS_BLE_PASSKEY_ENTRY || c->pair_kind == MCS_BLE_PASSKEY_DISPLAY;
    c->bonded = g.opt[MCS_BLE_OPT_BONDING] != 0;
    if (c->bonded && g.nbonds < 8) {
        mcs_ble_addr_t peer = c->dev < 0 ? PHONE : g_dev[c->dev].addr;
        int k = 0;
        for (; k < g.nbonds; k++) if (!memcmp(g.bonds[k].a, peer.a, 6)) break;
        if (k == g.nbonds) g.bonds[g.nbonds++] = peer;
    }
    mcs_ble_on_event(MCS_BLE_EV_ENCRYPTED, c->handle, 1 + c->auth + c->bonded);
    mcs_ble_on_event(MCS_BLE_EV_PAIRING_DONE, c->handle, 0);
}
static int s_pair(void* ctx, int conn) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c) return MCS_HAL_ENODEV;
    if (c->enc) { mcs_ble_on_event(MCS_BLE_EV_ENCRYPTED, conn, 1 + c->auth + c->bonded); return 0; }
    int io = g.opt[MCS_BLE_OPT_IO_CAP];
    c->pairing = 1;
    if (!g.opt[MCS_BLE_OPT_MITM] || io == MCS_BLE_IO_NONE) { c->pair_kind = 0; pair_done(c, 1); return 0; }
    c->pair_kind = io == MCS_BLE_IO_DISPLAY_ONLY ? MCS_BLE_PASSKEY_DISPLAY : io == MCS_BLE_IO_KEYBOARD_ONLY ? MCS_BLE_PASSKEY_ENTRY : MCS_BLE_PASSKEY_COMPARE;
    int32_t key = c->pair_kind == MCS_BLE_PASSKEY_DISPLAY && g.opt[MCS_BLE_OPT_PASSKEY] >= 0 ? g.opt[MCS_BLE_OPT_PASSKEY] : 123456;
    mcs_ble_on_passkey(conn, c->pair_kind, c->pair_kind == MCS_BLE_PASSKEY_ENTRY ? 0 : key);
    if (c->pair_kind == MCS_BLE_PASSKEY_DISPLAY) pair_done(c, 1);   /* the peer typed it */
    return 0;
}
static int s_passkey_reply(void* ctx, int conn, int kind, int32_t value) {
    (void)ctx;
    sconn_t* c = conn_of(conn);
    if (!c || !c->pairing) return MCS_HAL_EINVAL;
    pair_done(c, kind == MCS_BLE_PASSKEY_ENTRY ? value == 123456 : value != 0);
    return 0;
}
static int s_bonds(void* ctx, int op, mcs_ble_addr_t* a, int max) {
    (void)ctx;
    switch (op) {
    case MCS_BLE_BONDS_COUNT: return g.nbonds;
    case MCS_BLE_BONDS_LIST: { int n = g.nbonds < max ? g.nbonds : max; memcpy(a, g.bonds, sizeof(mcs_ble_addr_t) * (size_t)n); return n; }
    case MCS_BLE_BONDS_DELETE:
        for (int i = 0; i < g.nbonds; i++)
            if (!memcmp(g.bonds[i].a, a->a, 6)) { g.bonds[i] = g.bonds[--g.nbonds]; return 0; }
        return MCS_HAL_ENODEV;
    case MCS_BLE_BONDS_CLEAR: g.nbonds = 0; return 0;
    }
    return MCS_HAL_EINVAL;
}

/* ---------------------------------------------------------------- time */
void mcs_ble_sim_tick(uint32_t now) {
    g.now = now;
    if (!g.started) { g.started = 1; g.last_hr = g.last_temp = g.last_batt = now; }
    if (!g.on) return;
    if (g.scanning) {
        if (g.scan_dur && now - g.scan_start >= g.scan_dur) { g.scanning = 0; mcs_ble_on_event(MCS_BLE_EV_SCAN_DONE, 0, 0); }
        else if (g.scan_dup && now - g.last_report >= 1000) { g.last_report = now; g.rounds++; report_all(1); }
    }
    bool hr = now - g.last_hr >= 1000, tp = now - g.last_temp >= 2000, bt = now - g.last_batt >= 5000;
    if (hr) { g.last_hr = now; g.bpm = (uint8_t)(g.bpm >= 79 ? 70 : g.bpm + 1); g_db[i_hr].val[1] = g.bpm; }
    if (tp) { g.last_temp = now; int t = g_db[i_temp].val[0] | g_db[i_temp].val[1] << 8; t = t >= 2200 ? 2100 : t + 5; g_db[i_temp].val[0] = (uint8_t)t; g_db[i_temp].val[1] = (uint8_t)(t >> 8); }
    if (bt) { g.last_batt = now; g.batt = (uint8_t)(g.batt <= 10 ? 100 : g.batt - 1); g_db[i_batt].val[0] = g.batt; }
    for (int i = 0; i < 4; i++) {
        sconn_t* c = &g.conn[i];
        if (!c->used || c->dev < 0) continue;
        if (hr && c->dev == 0) notify_peer(c, i_hr);
        if (bt && c->dev == 0) notify_peer(c, i_batt);
        if (tp && c->dev == 4) notify_peer(c, i_temp);
    }
}

void mcs_ble_sim_init(mcs_ble_t* b) {
    memset(&g, 0, sizeof g);
    devices_init();
    snprintf(g.name, sizeof g.name, "MicroCS");
    g.opt[MCS_BLE_OPT_TX_POWER] = 3; g.opt[MCS_BLE_OPT_MTU] = 247; g.opt[MCS_BLE_OPT_APPEARANCE] = 0;
    g.opt[MCS_BLE_OPT_IO_CAP] = MCS_BLE_IO_NONE; g.opt[MCS_BLE_OPT_BONDING] = 1; g.opt[MCS_BLE_OPT_MITM] = 0;
    g.opt[MCS_BLE_OPT_SC] = 1; g.opt[MCS_BLE_OPT_PASSKEY] = -1; g.opt[MCS_BLE_OPT_ADDR_TYPE] = MCS_BLE_ADDR_PUBLIC;
    g.opt[MCS_BLE_OPT_MAX_CONN] = 4; g.opt[MCS_BLE_OPT_PHY] = 3;
    g.bpm = 72; g.batt = 87;
    memset(b, 0, sizeof *b);
    b->name = "sim"; b->ctx = &g;
    b->enable = s_enable; b->address = s_address; b->set_name = s_set_name; b->opt_get = s_opt_get; b->opt_set = s_opt_set;
    b->adv_start = s_adv_start; b->adv_stop = s_adv_stop; b->scan_start = s_scan_start; b->scan_stop = s_scan_stop;
    b->gatts_set = s_gatts_set; b->gatts_notify = s_gatts_notify;
    b->disconnect = s_disconnect; b->conn_info = s_conn_info; b->conn_rssi = s_conn_rssi; b->conn_update = s_conn_update;
    b->connect = s_connect; b->connect_cancel = s_connect_cancel; b->discover = s_discover; b->read = s_read; b->write = s_write;
    b->subscribe = s_subscribe; b->exchange_mtu = s_exchange_mtu;
    b->pair = s_pair; b->passkey_reply = s_passkey_reply; b->bonds = s_bonds;
}
#endif
