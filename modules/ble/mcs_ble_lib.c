/*
 * MicroCS - C# Bluetooth Low Energy front end (MCS_ENABLE_BLE, include/mcs_ble.h, docs/BLE.md).
 *
 *   foreach (var d in Ble.Scan(3000)) Console.WriteLine(d);           observer
 *   var svc = new BleService("180F");                                 peripheral / GATT server
 *   var level = svc.AddCharacteristic("2A19", BleProperty.Read | BleProperty.Notify, new byte[] { 90 });
 *   Ble.AddService(svc); Ble.StartAdvertising();
 *   level.Notify(new byte[] { 89 });
 *   var peer = Ble.Connect("C0:FF:EE:00:00:01");                      central / GATT client
 *   peer.Characteristic("180D", "2A37").Subscribe(data => Console.WriteLine(data[1]));
 *
 * The radio is the port's mcs_ble_t table. Its stack reports through the mcs_ble_on_*()
 * functions into one byte queue (process-wide, like the HAL event ring) which the VM drains
 * when it handles the MCS_HAL_EV_BLE event, in Ble.Poll() and in every blocking BLE call.
 */
#include "mcs_ble.h"
#if MCS_ENABLE_BLE
#include "mcs_hal.h"
#include <string.h>
#include <stdio.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define CHECK() do { if (mcs_has_exception(vm)) return mcs_null(); } while (0)
#define INT_ARG(i, var) mcs_int_t var = mcs_to_int(vm, argv[i]); CHECK()
#define OPT_INT(i, var, def) mcs_int_t var = argc > (i) && !mcs_is_null(argv[i]) ? mcs_to_int(vm, argv[i]) : (mcs_int_t)(def); CHECK()
#define STR_ARG(i, var) const char* var = mcs_to_cstr(vm, argv[i]); CHECK()

static const mcs_ble_t* g_ble;
void mcs_ble_set(const mcs_ble_t* ble) { g_ble = ble; }
const mcs_ble_t* mcs_ble_get(void) { return g_ble; }
static bool (*g_poster)(int type, int source, int32_t value);
void mcs_ble_set_poster(bool (*post)(int type, int source, int32_t value)) { g_poster = post; }

static void lock(void) { if (g_ble && g_ble->lock) g_ble->lock(g_ble->ctx); }
static void unlock(void) { if (g_ble && g_ble->unlock) g_ble->unlock(g_ble->ctx); }

/* ============================================================ UUIDs */
/* Bluetooth base UUID 00000000-0000-1000-8000-00805F9B34FB, little-endian */
static const uint8_t g_base[16] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0, 0, 0, 0 };
void mcs_ble_uuid16(mcs_ble_uuid_t* u, uint16_t v) {
    memset(u, 0, sizeof *u);
    u->len = 2; u->b[0] = (uint8_t)v; u->b[1] = (uint8_t)(v >> 8);
}
static void uuid_full(const mcs_ble_uuid_t* u, uint8_t out[16]) {
    if (u->len == 16) { memcpy(out, u->b, 16); return; }
    memcpy(out, g_base, 16);
    out[12] = u->b[0]; out[13] = u->b[1];
    if (u->len == 4) { out[14] = u->b[2]; out[15] = u->b[3]; }
}
bool mcs_ble_uuid_eq(const mcs_ble_uuid_t* a, const mcs_ble_uuid_t* b) {
    uint8_t x[16], y[16];
    uuid_full(a, x); uuid_full(b, y);
    return memcmp(x, y, 16) == 0;
}
/* the shortest form: 128-bit UUIDs on the base become 16 / 32-bit */
static void uuid_shrink(mcs_ble_uuid_t* u) {
    if (u->len != 16 || memcmp(u->b, g_base, 12)) return;
    uint8_t v[4] = { u->b[12], u->b[13], u->b[14], u->b[15] };
    memset(u->b, 0, 16);
    memcpy(u->b, v, 4);
    u->len = (v[2] || v[3]) ? 4 : 2;
}
uint16_t mcs_ble_uuid_short(const mcs_ble_uuid_t* u) {
    mcs_ble_uuid_t c = *u;
    uuid_shrink(&c);
    return c.len == 2 ? (uint16_t)(c.b[0] | c.b[1] << 8) : 0;
}
int mcs_ble_uuid_str(const mcs_ble_uuid_t* u, char out[37]) {
    mcs_ble_uuid_t c = *u;
    uuid_shrink(&c);
    if (c.len == 2) return snprintf(out, 37, "%04X", (unsigned)(c.b[0] | c.b[1] << 8));
    if (c.len == 4) return snprintf(out, 37, "%08lX", (unsigned long)((uint32_t)c.b[0] | (uint32_t)c.b[1] << 8 | (uint32_t)c.b[2] << 16 | (uint32_t)c.b[3] << 24));
    int k = 0;
    for (int i = 15; i >= 0; i--) {
        k += snprintf(out + k, (size_t)(37 - k), "%02X", c.b[i]);
        if (i == 12 || i == 10 || i == 8 || i == 6) out[k++] = '-';
    }
    out[k] = 0;
    return k;
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
bool mcs_ble_uuid_parse(const char* s, mcs_ble_uuid_t* out) {
    if (!s) return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint8_t d[32]; int n = 0;
    for (; *s; s++) {
        if (*s == '-') continue;
        int v = hexv(*s);
        if (v < 0 || n >= 32) return false;
        d[n++] = (uint8_t)v;
    }
    memset(out, 0, sizeof *out);
    if (n == 4 || n == 8) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v = v << 4 | d[i];
        out->len = (uint8_t)(n / 2);
        for (int i = 0; i < out->len; i++) out->b[i] = (uint8_t)(v >> (8 * i));
        return true;
    }
    if (n != 32) return false;
    out->len = 16;
    for (int i = 0; i < 16; i++) out->b[15 - i] = (uint8_t)(d[2 * i] << 4 | d[2 * i + 1]);
    uuid_shrink(out);
    return true;
}
/* well-known names for ToString and BleUuid.Name */
static const struct { uint16_t v; const char* name; } g_names[] = {
    { 0x1800, "Generic Access" }, { 0x1801, "Generic Attribute" }, { 0x180A, "Device Information" },
    { 0x180D, "Heart Rate" }, { 0x180F, "Battery" }, { 0x1809, "Health Thermometer" },
    { 0x1812, "Human Interface Device" }, { 0x1816, "Cycling Speed and Cadence" }, { 0x1818, "Cycling Power" },
    { 0x1819, "Location and Navigation" }, { 0x181A, "Environmental Sensing" }, { 0x181C, "User Data" },
    { 0x1805, "Current Time" }, { 0x1802, "Immediate Alert" }, { 0x1803, "Link Loss" }, { 0x1804, "Tx Power" },
    { 0x1810, "Blood Pressure" }, { 0x1814, "Running Speed and Cadence" }, { 0x181D, "Weight Scale" },
    { 0xFEAA, "Eddystone" }, { 0xFE59, "Nordic DFU" },
    { 0x2A00, "Device Name" }, { 0x2A01, "Appearance" }, { 0x2A05, "Service Changed" }, { 0x2A19, "Battery Level" },
    { 0x2A23, "System ID" }, { 0x2A24, "Model Number" }, { 0x2A25, "Serial Number" }, { 0x2A26, "Firmware Revision" },
    { 0x2A27, "Hardware Revision" }, { 0x2A28, "Software Revision" }, { 0x2A29, "Manufacturer Name" },
    { 0x2A37, "Heart Rate Measurement" }, { 0x2A38, "Body Sensor Location" }, { 0x2A39, "Heart Rate Control Point" },
    { 0x2A1C, "Temperature Measurement" }, { 0x2A6E, "Temperature" }, { 0x2A6F, "Humidity" }, { 0x2A6D, "Pressure" },
    { 0x2A2B, "Current Time" }, { 0x2A06, "Alert Level" }, { 0x2A07, "Tx Power Level" },
    { 0x2900, "Characteristic Extended Properties" }, { 0x2901, "Characteristic User Description" },
    { 0x2902, "Client Characteristic Configuration" }, { 0x2903, "Server Characteristic Configuration" },
    { 0x2904, "Characteristic Presentation Format" },
};
static const uint8_t g_nus[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
static const char* uuid_name(const mcs_ble_uuid_t* u) {
    uint16_t v = mcs_ble_uuid_short(u);
    if (v) {
        for (size_t i = 0; i < sizeof g_names / sizeof g_names[0]; i++) if (g_names[i].v == v) return g_names[i].name;
        return NULL;
    }
    if (u->len == 16 && !memcmp(u->b, g_nus, 12) && u->b[14] == 0x40 && u->b[15] == 0x6E && u->b[13] == 0) {
        if (u->b[12] == 1) return "Nordic UART";
        if (u->b[12] == 2) return "Nordic UART RX";
        if (u->b[12] == 3) return "Nordic UART TX";
    }
    return NULL;
}
static void nus_uuid(mcs_ble_uuid_t* u, int k) { u->len = 16; memcpy(u->b, g_nus, 16); u->b[12] = (uint8_t)k; }

/* ============================================================ the queue (stack task -> VM) */
typedef struct {
    uint8_t ev, kind;                /* MCS_BLE_EV_*, report type / passkey kind / flags */
    uint16_t len;                    /* data bytes that follow */
    int32_t conn;
    int32_t value;
    uint16_t id;                     /* attribute id / remote handle */
    int8_t rssi;
    mcs_ble_addr_t addr;
} qrec_t;
static uint8_t g_q[MCS_BLE_QUEUE];
static uint32_t g_qhead, g_qused;    /* read position, bytes in use */
static uint32_t g_dropped;
static volatile uint8_t g_wake;      /* an MCS_HAL_EV_BLE event is on its way */

static void q_copy_in(const void* p, uint32_t n) {
    uint32_t w = (g_qhead + g_qused) % MCS_BLE_QUEUE;
    const uint8_t* s = (const uint8_t*)p;
    uint32_t a = MCS_BLE_QUEUE - w < n ? MCS_BLE_QUEUE - w : n;
    memcpy(g_q + w, s, a);
    memcpy(g_q, s + a, n - a);
    g_qused += n;
}
static void q_copy_out(void* p, uint32_t n) {
    uint8_t* d = (uint8_t*)p;
    uint32_t a = MCS_BLE_QUEUE - g_qhead < n ? MCS_BLE_QUEUE - g_qhead : n;
    if (d) { memcpy(d, g_q + g_qhead, a); memcpy(d + a, g_q, n - a); }
    g_qhead = (g_qhead + n) % MCS_BLE_QUEUE;
    g_qused -= n;
}
static void q_push(qrec_t* r, const uint8_t* data, size_t n) {
    if (n > MCS_BLE_MAX_VALUE) n = MCS_BLE_MAX_VALUE;
    r->len = (uint16_t)n;
    bool post = false;
    lock();
    if (g_qused + sizeof *r + n > MCS_BLE_QUEUE) g_dropped++;
    else {
        q_copy_in(r, sizeof *r);
        if (n) q_copy_in(data, (uint32_t)n);
        if (!g_wake) { g_wake = 1; post = true; }
    }
    unlock();
    if (post) {
        bool ok = g_poster ? g_poster(MCS_HAL_EV_BLE, r->ev, 0)
#if MCS_ENABLE_HAL
                           : mcs_hal_post(MCS_HAL_EV_BLE, r->ev, 0);
#else
                           : false;
#endif
        if (!ok) g_wake = 0;         /* drained by the next Ble call / poll instead */
    }
}
/* next record; data truncated to cap (the rest skipped) */
static bool q_pop(qrec_t* r, uint8_t* buf, size_t cap) {
    lock();
    if (g_qused < sizeof *r) { g_wake = 0; unlock(); return false; }
    q_copy_out(r, sizeof *r);
    uint32_t take = r->len < cap ? r->len : (uint32_t)cap;
    q_copy_out(buf, take);
    if (r->len > take) q_copy_out(NULL, r->len - take);
    r->len = (uint16_t)take;
    unlock();
    return true;
}
static void q_clear(void) { lock(); g_qhead = g_qused = 0; g_wake = 0; unlock(); }

void mcs_ble_on_event(int ev, int conn, int32_t value) {
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = (uint8_t)ev; r.conn = conn; r.value = value;
    q_push(&r, NULL, 0);
}

/* ============================================================ local GATT database (static) */
enum { A_FREE = 0, A_CHR = 1, A_DSC = 2 };
typedef struct {
    uint8_t kind, svc;               /* A_*, service slot */
    uint8_t uart;                    /* the BleUart RX characteristic */
    uint16_t parent;                 /* descriptors: their characteristic */
    uint16_t props;                  /* MCS_BLE_PROP_* | MCS_BLE_PERM_* (descriptors: MCS_BLE_DSC_* | PERM) */
    uint16_t len, cap;
    uint32_t off;                    /* value bytes in g_pool */
    mcs_ble_uuid_t uuid;
    int16_t def;                     /* index in g_chr_defs / g_dsc_defs while published, -1 = not */
    uint8_t subs[MCS_BLE_MAX_CONN];  /* MCS_BLE_SUB_* per connection slot */
} attr_t;
typedef struct { uint8_t used, primary, added; mcs_ble_uuid_t uuid; uint16_t handle; } svc_t;
static attr_t g_attr[MCS_BLE_MAX_ATTRS];
static svc_t g_svc[MCS_BLE_MAX_SERVICES];
static uint8_t g_pool[MCS_BLE_VALUE_POOL];
static uint32_t g_pool_used;
static mcs_ble_svc_def_t g_svc_defs[MCS_BLE_MAX_SERVICES];
static mcs_ble_chr_def_t g_chr_defs[MCS_BLE_MAX_ATTRS];
static mcs_ble_dsc_def_t g_dsc_defs[MCS_BLE_MAX_ATTRS];
static int g_nsvc_defs;

int mcs_ble_attr_len(uint16_t id) {
    if (id >= MCS_BLE_MAX_ATTRS || g_attr[id].kind == A_FREE) return -1;
    return g_attr[id].len;
}
int mcs_ble_attr_read(uint16_t id, int conn, size_t offset, uint8_t* buf, size_t cap, size_t* total) {
    (void)conn;
    if (id >= MCS_BLE_MAX_ATTRS || g_attr[id].kind == A_FREE) return MCS_BLE_ATT_ERR(0x0A);   /* attribute not found */
    lock();
    attr_t* a = &g_attr[id];
    if (total) *total = a->len;
    if (offset > a->len) { unlock(); return MCS_BLE_ATT_ERR(0x07); }                        /* invalid offset */
    size_t n = a->len - offset;
    if (n > cap) n = cap;
    if (n) memcpy(buf, g_pool + a->off + offset, n);
    unlock();
    return (int)n;
}
int mcs_ble_attr_write(uint16_t id, int conn, size_t offset, const uint8_t* data, size_t n) {
    if (id >= MCS_BLE_MAX_ATTRS || g_attr[id].kind == A_FREE) return MCS_BLE_ATT_ERR(0x0A);
    attr_t* a = &g_attr[id];
    if (offset + n > a->cap) return MCS_BLE_ATT_ERR(offset > a->cap ? 0x07 : 0x0D);          /* invalid offset / length */
    lock();
    if (n) memcpy(g_pool + a->off + offset, data, n);
    a->len = (uint16_t)(offset + n);
    unlock();
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = MCS_BLE_EV_WRITE; r.conn = conn; r.id = id; r.value = (int32_t)offset;
    uint8_t tmp[MCS_BLE_MAX_VALUE];   /* the whole value after the write */
    size_t total = 0;
    int k = mcs_ble_attr_read(id, conn, 0, tmp, sizeof tmp, &total);
    q_push(&r, tmp, k > 0 ? (size_t)k : 0);
    return 0;
}

/* ============================================================ GATT client operation */
enum { OP_NONE = 0, OP_CONNECT, OP_DISCOVER, OP_READ, OP_WRITE, OP_SUBSCRIBE, OP_MTU };
static struct {
    volatile uint8_t kind, done;
    int conn;
    int status;
    int32_t value;
    uint16_t len;
    uint8_t overflow;
    uint8_t data[MCS_BLE_MAX_VALUE];
    int nattrs;
    mcs_ble_attr_t attrs[MCS_BLE_MAX_DISC];
} g_op;
void mcs_ble_op_attr(const mcs_ble_attr_t* a) {
    lock();
    if (g_op.kind == OP_DISCOVER && !g_op.done) {
        if (g_op.nattrs < MCS_BLE_MAX_DISC) g_op.attrs[g_op.nattrs++] = *a;
        else g_op.overflow = 1;
    }
    unlock();
}
void mcs_ble_op_data(const uint8_t* d, size_t n) {
    lock();
    if (g_op.kind == OP_READ && !g_op.done) {
        size_t room = sizeof g_op.data - g_op.len;
        if (n > room) { n = room; g_op.overflow = 1; }
        memcpy(g_op.data + g_op.len, d, n);
        g_op.len = (uint16_t)(g_op.len + n);
    }
    unlock();
}
void mcs_ble_op_done(int status, int32_t value) {
    lock();
    if (g_op.kind != OP_NONE && !g_op.done) { g_op.status = status; g_op.value = value; g_op.done = 1; }
    unlock();
}

void mcs_ble_on_connect(int conn, const mcs_ble_addr_t* peer, int role) {
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = MCS_BLE_EV_CONNECT; r.conn = conn; r.value = role;
    if (peer) r.addr = *peer;
    q_push(&r, NULL, 0);
    if (role == MCS_BLE_ROLE_CENTRAL) {
        lock();
        if (g_op.kind == OP_CONNECT && !g_op.done) { g_op.status = 0; g_op.value = conn; g_op.done = 1; }
        unlock();
    }
}
void mcs_ble_on_disconnect(int conn, int reason) {
    mcs_ble_on_event(MCS_BLE_EV_DISCONNECT, conn, reason);
    lock();
    if (g_op.kind != OP_NONE && g_op.kind != OP_CONNECT && !g_op.done && g_op.conn == conn) {
        g_op.status = MCS_HAL_ENODEV; g_op.done = 1;
    }
    unlock();
}
void mcs_ble_on_report(const mcs_ble_addr_t* addr, int rssi, int type, const uint8_t* data, size_t n) {
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = MCS_BLE_EV_REPORT; r.kind = (uint8_t)type; r.rssi = (int8_t)(rssi < -127 ? -127 : rssi > 20 ? 20 : rssi);
    if (addr) r.addr = *addr;
    q_push(&r, data, n > 31 ? 31 : n);
}
void mcs_ble_on_subscribe(int conn, uint16_t id, int flags) {
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = MCS_BLE_EV_SUBSCRIBE; r.conn = conn; r.id = id; r.value = (int32_t)id << 8 | (flags & 3); r.kind = (uint8_t)flags;
    q_push(&r, NULL, 0);
}
void mcs_ble_on_notify(int conn, uint16_t handle, const uint8_t* data, size_t n, int indication) {
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = MCS_BLE_EV_NOTIFY; r.conn = conn; r.id = handle; r.kind = (uint8_t)(indication != 0);
    q_push(&r, data, n);
}
void mcs_ble_on_passkey(int conn, int kind, int32_t value) {
    qrec_t r; memset(&r, 0, sizeof r);
    r.ev = MCS_BLE_EV_PASSKEY; r.conn = conn; r.kind = (uint8_t)kind; r.value = value;
    q_push(&r, NULL, 0);
}

const char* mcs_ble_event_name(int ev) {
    static const char* const n[MCS_BLE_EV__COUNT] = { "None", "Connect", "Disconnect", "ConnectFailed",
        "AdvertisingDone", "ScanDone", "Mtu", "Subscribe", "Write", "Notify", "Report", "Encrypted",
        "PairingDone", "Passkey", "ConnectionUpdate", "IndicateDone", "Phy" };
    return ev > 0 && ev < MCS_BLE_EV__COUNT ? n[ev] : "Unknown";
}

/* ============================================================ VM-side state */
/* pinned registry list: callbacks and the objects the C tables refer to */
enum { R_EVENT = 0, R_CONNECT, R_DISCONNECT, R_PASSKEY, R_SCAN, R_UART, R__FIXED = 8,
       R_ATTR = R__FIXED,                                  /* BleCharacteristic / BleDescriptor objects */
       R_WRITE = R_ATTR + MCS_BLE_MAX_ATTRS,               /* their OnWrite callbacks */
       R_SUBCB = R_WRITE + MCS_BLE_MAX_ATTRS,              /* OnSubscribe callbacks */
       R_SVC = R_SUBCB + MCS_BLE_MAX_ATTRS,                /* BleService objects */
       R_CONN = R_SVC + MCS_BLE_MAX_SERVICES,              /* BleConnection objects */
       R_CSUB = R_CONN + MCS_BLE_MAX_CONN,                 /* remote Subscribe callbacks */
       R__COUNT = R_CSUB + MCS_BLE_MAX_SUBS };

typedef struct { uint8_t used, role; uint16_t gen, mtu; int handle; mcs_ble_addr_t addr; } conn_t;
typedef struct { uint8_t used, slot; uint16_t gen, handle; } csub_t;
typedef struct {
    const mcs_ble_t* ble;
    int self_pin, reg_pin;
    uint8_t on, advertising, want_adv, scanning, db_dirty, auto_adv, draining;
    uint32_t timeout;                /* Ble.Timeout (ms) */
    char name[30];
    mcs_ble_adv_t adv;               /* the last advertising set (restarted after a disconnect) */
    int collecting;                  /* blocking Ble.Scan: merge reports into `found` */
    mcs_value_t found;               /* (rooted by the caller) */
    int32_t passkey;
} ble_state_t;
static conn_t g_conn[MCS_BLE_MAX_CONN];
static csub_t g_csub[MCS_BLE_MAX_SUBS];
static uint16_t g_gen;

static const mcs_class_def_t state_def = { "__BleState", sizeof(ble_state_t), NULL, NULL, NULL, NULL };
static ble_state_t* bstate(mcs_vm_t* vm) { return (ble_state_t*)mcs_get_ext(vm, MCS_EXT_BLE); }
#define ST() ble_state_t* s = bstate(vm); \
    if (!s) { mcs_raise(vm, "InvalidOperationException", "Bluetooth library not opened"); return mcs_null(); }
#define BLE() (s->ble)
#define NEED(fn, op) if (!BLE() || !BLE()->fn) return fail(vm, op, MCS_HAL_ENOTSUP)
#define CALL(op, expr) do { int rc_ = (expr); if (rc_ < 0) return fail(vm, op, rc_); } while (0)
#define ON(op) if (!need_on(vm, s, op)) return mcs_null()

static mcs_value_t reg_get(mcs_vm_t* vm, ble_state_t* s, int i) { return mcs_index(mcs_pinned(vm, s->reg_pin), (uint32_t)i); }
static void reg_set(mcs_vm_t* vm, ble_state_t* s, int i, mcs_value_t v) { mcs_set_index(mcs_pinned(vm, s->reg_pin), (uint32_t)i, v); }

static const char* att_error_name(int code) {
    switch (code) {
    case 0x01: return "invalid handle"; case 0x02: return "read not permitted"; case 0x03: return "write not permitted";
    case 0x05: return "insufficient authentication"; case 0x06: return "request not supported"; case 0x07: return "invalid offset";
    case 0x08: return "insufficient authorization"; case 0x0A: return "attribute not found"; case 0x0C: return "insufficient encryption key size";
    case 0x0D: return "invalid attribute value length"; case 0x0E: return "unlikely error"; case 0x0F: return "insufficient encryption";
    case 0x11: return "insufficient resources"; default: return "ATT error";
    }
}
static mcs_value_t fail(mcs_vm_t* vm, const char* op, int rc) {
    if (MCS_BLE_IS_ATT_ERR(rc)) {
        int c = MCS_BLE_ATT_CODE(rc);
        if (c == 0x05 || c == 0x08 || c == 0x0F || c == 0x0C) mcs_raise(vm, "UnauthorizedAccessException", "%s: %s (ATT 0x%02X)", op, att_error_name(c), c);
        else mcs_raise(vm, "IOException", "%s: %s (ATT 0x%02X)", op, att_error_name(c), c);
        return mcs_null();
    }
    const char* why = rc == MCS_HAL_ENOTSUP ? "not supported" : rc == MCS_HAL_ETIMEOUT ? "timeout"
                    : rc == MCS_HAL_ENODEV ? "not connected / not found" : rc == MCS_HAL_EBUSY ? "busy"
                    : rc == MCS_HAL_EINVAL ? "invalid argument" : "failed";
    if (rc == MCS_HAL_ETIMEOUT) mcs_raise(vm, "TimeoutException", "%s: %s", op, why);
    else if (rc == MCS_HAL_ENOTSUP) mcs_raise(vm, "NotSupportedException", "%s: %s", op, why);
    else if (rc == MCS_HAL_EINVAL) mcs_raise(vm, "ArgumentException", "%s: %s", op, why);
    else mcs_raise(vm, "IOException", "%s: %s (%d)", op, why, rc);
    return mcs_null();
}
static int call_cb(mcs_vm_t* vm, mcs_value_t fn, const mcs_value_t* args, int full, mcs_value_t* result) {
    int a = mcs_arity(fn);
    int argc = a < 0 || a >= full ? full : a;
    mcs_result_t r = mcs_call_value(vm, fn, argc, args, result);
    return r == MCS_OK ? 1 : -(int)r;
}
static void plain_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argv;
    if (argc) mcs_raise(vm, "InvalidOperationException", "created by the Bluetooth library");
}

/* ---- bytes <-> values */
static bool bytes_arg(mcs_vm_t* vm, mcs_value_t v, uint8_t* out, size_t cap, size_t* n, const char* what) {
    if (mcs_is_string(v)) {
        size_t l = mcs_strlen(v);
        if (l > cap) { mcs_raise(vm, "ArgumentException", "%s: at most %d bytes", what, (int)cap); return false; }
        memcpy(out, mcs_cstr(v), l); *n = l;
        return true;
    }
    int k = mcs_obj_kind(v);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "%s: expected a string or byte[]", what); return false; }
    size_t l = mcs_len(v);
    if (l > cap) { mcs_raise(vm, "ArgumentException", "%s: at most %d bytes", what, (int)cap); return false; }
    for (size_t i = 0; i < l; i++) {
        mcs_int_t x = mcs_to_int(vm, mcs_index(v, (uint32_t)i));
        if (mcs_has_exception(vm)) return false;
        out[i] = (uint8_t)x;
    }
    *n = l;
    return true;
}
static mcs_value_t bytes_new(mcs_vm_t* vm, const uint8_t* p, size_t n) {
    mcs_value_t a = mcs_new_array(vm, (uint32_t)n);
    for (size_t i = 0; i < n; i++) mcs_set_index(a, (uint32_t)i, mcs_int(p[i]));
    return a;
}
static bool uuid_arg(mcs_vm_t* vm, mcs_value_t v, mcs_ble_uuid_t* u) {
    if (mcs_is_int(v)) {
        mcs_int_t x = mcs_to_int(vm, v);
        if (x < 0 || x > 0xFFFF) { mcs_raise(vm, "ArgumentException", "a 16-bit UUID is 0..0xFFFF"); return false; }
        mcs_ble_uuid16(u, (uint16_t)x);
        return true;
    }
    const char* str = mcs_to_cstr(vm, v);
    if (mcs_has_exception(vm)) return false;
    if (!mcs_ble_uuid_parse(str, u)) { mcs_raise(vm, "ArgumentException", "'%s' is not a UUID (\"180D\" or \"6E400001-B5A3-F393-E0A9-E50E24DCCA9E\")", str); return false; }
    return true;
}
static mcs_value_t uuid_val(mcs_vm_t* vm, const mcs_ble_uuid_t* u) { char b[37]; mcs_ble_uuid_str(u, b); return mcs_string(vm, b); }
static bool addr_parse(const char* str, mcs_ble_addr_t* a) {
    if (!str) return false;
    for (int i = 0; i < 6; i++) {
        int h = hexv(str[0]), l = h < 0 ? -1 : hexv(str[1]);
        if (h < 0 || l < 0) return false;
        a->a[i] = (uint8_t)(h << 4 | l);
        str += 2;
        if (i < 5) { if (*str != ':' && *str != '-') return false; str++; }
    }
    return *str == 0;
}
static void addr_fmt(const mcs_ble_addr_t* a, char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a->a[0], a->a[1], a->a[2], a->a[3], a->a[4], a->a[5]);
}
static mcs_value_t addr_val(mcs_vm_t* vm, const mcs_ble_addr_t* a) { char b[18]; addr_fmt(a, b); return mcs_string(vm, b); }
static const char* addr_type_name(int t) { return t == MCS_BLE_ADDR_PUBLIC || t == MCS_BLE_ADDR_PUBLIC_ID ? "public" : "random"; }

/* ---- waiting: abortable slices; the stack's poll hook keeps stacks without a task going */
static bool ble_wait(mcs_vm_t* vm, uint32_t ms) {
    if (g_ble && g_ble->poll) g_ble->poll(g_ble->ctx);
    mcs_sleep(vm, ms);
    return !mcs_has_exception(vm) && mcs_safepoint(vm) == 0;
}
static int drain(mcs_vm_t* vm, ble_state_t* s);
/* run the operation started by `start` (rc of the port call) and wait for it */
static int op_begin(int kind, int conn) {
    lock();
    g_op.kind = (uint8_t)kind; g_op.done = 0; g_op.conn = conn; g_op.status = 0; g_op.value = 0;
    g_op.len = 0; g_op.nattrs = 0; g_op.overflow = 0;
    unlock();
    return 0;
}
static int op_wait(mcs_vm_t* vm, ble_state_t* s, int rc, uint32_t timeout) {
    if (rc < 0) { g_op.kind = OP_NONE; return rc; }
    uint32_t t0 = mcs_ticks(vm);
    while (!g_op.done) {
        if ((uint32_t)(mcs_ticks(vm) - t0) >= timeout) { lock(); g_op.kind = OP_NONE; unlock(); return MCS_HAL_ETIMEOUT; }
        if (!ble_wait(vm, 2)) { lock(); g_op.kind = OP_NONE; unlock(); return MCS_HAL_ERR; }
    }
    lock(); g_op.kind = OP_NONE; rc = g_op.status; unlock();
    (void)s;
    return rc;
}

/* ---- connections */
static const mcs_class_def_t conn_def;
typedef struct { int handle; uint8_t slot, role; uint16_t gen; mcs_ble_addr_t addr; } bconn_t;
static int conn_slot(int handle) {
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++) if (g_conn[i].used && g_conn[i].handle == handle) return i;
    return -1;
}
/* the BleConnection of `handle`, created when new; null without a free slot */
static mcs_value_t conn_add(mcs_vm_t* vm, ble_state_t* s, int handle, const mcs_ble_addr_t* peer, int role) {
    int i = conn_slot(handle);
    if (i >= 0) return reg_get(vm, s, R_CONN + i);
    for (i = 0; i < MCS_BLE_MAX_CONN && g_conn[i].used; i++) {}
    if (i == MCS_BLE_MAX_CONN) return mcs_null();
    mcs_value_t o;
    if (mcs_new_object(vm, "BleConnection", 0, NULL, &o) != MCS_OK) return mcs_null();
    conn_t* c = &g_conn[i];
    memset(c, 0, sizeof *c);
    c->used = 1; c->handle = handle; c->role = (uint8_t)role; c->gen = ++g_gen; c->mtu = 23;
    if (peer) c->addr = *peer;
    bconn_t* b = (bconn_t*)mcs_userdata(o);
    b->handle = handle; b->slot = (uint8_t)i; b->role = (uint8_t)role; b->gen = c->gen; b->addr = c->addr;
    reg_set(vm, s, R_CONN + i, o);
    for (int k = 0; k < MCS_BLE_MAX_ATTRS; k++) g_attr[k].subs[i] = 0;
    return o;
}
static void conn_remove(mcs_vm_t* vm, ble_state_t* s, int slot) {
    g_conn[slot].used = 0;
    for (int k = 0; k < MCS_BLE_MAX_ATTRS; k++) g_attr[k].subs[slot] = 0;
    for (int k = 0; k < MCS_BLE_MAX_SUBS; k++)
        if (g_csub[k].used && g_csub[k].slot == slot) { g_csub[k].used = 0; reg_set(vm, s, R_CSUB + k, mcs_null()); }
    reg_set(vm, s, R_CONN + slot, mcs_null());
}
static int nconn(void) { int n = 0; for (int i = 0; i < MCS_BLE_MAX_CONN; i++) n += g_conn[i].used; return n; }

/* ============================================================ BleDevice (advertising reports) */
typedef struct {
    mcs_ble_addr_t addr;
    int8_t rssi;
    uint8_t type, connectable, adv_len, rsp_len, has_adv;
    uint8_t adv[31], rsp[31];
} bdev_t;
static const mcs_class_def_t dev_def;
#define DEV() bdev_t* d = (bdev_t*)mcs_check_userdata(vm, self, &dev_def); if (!d) return mcs_null()
/* the k-th AD structure of `type` in the advertisement, then the scan response */
static bool ad_find(const bdev_t* d, int type, int k, const uint8_t** p, size_t* n) {
    for (int part = 0; part < 2; part++) {
        const uint8_t* b = part ? d->rsp : d->adv;
        size_t len = part ? d->rsp_len : d->adv_len;
        for (size_t i = 0; i + 1 < len;) {
            size_t l = b[i];
            if (l == 0 || i + 1 + l > len) break;
            if (b[i + 1] == type && k-- == 0) { *p = b + i + 2; *n = l - 1; return true; }
            i += 1 + l;
        }
    }
    return false;
}
static mcs_value_t dev_name_v(mcs_vm_t* vm, const bdev_t* d) {
    const uint8_t* p; size_t n;
    if (ad_find(d, 0x09, 0, &p, &n) || ad_find(d, 0x08, 0, &p, &n)) return mcs_string_n(vm, (const char*)p, n);
    return mcs_null();
}
NATIVE(dev_address) { DEV(); return addr_val(vm, &d->addr); }
NATIVE(dev_addrtype) { DEV(); return mcs_string(vm, addr_type_name(d->addr.type)); }
NATIVE(dev_name) { DEV(); return dev_name_v(vm, d); }
NATIVE(dev_rssi) { DEV(); return mcs_int(d->rssi); }
NATIVE(dev_connectable) { DEV(); return mcs_bool(d->connectable); }
NATIVE(dev_txpower) { DEV(); const uint8_t* p; size_t n; return ad_find(d, 0x0A, 0, &p, &n) && n >= 1 ? mcs_int((int8_t)p[0]) : mcs_null(); }
NATIVE(dev_appearance) { DEV(); const uint8_t* p; size_t n; return ad_find(d, 0x19, 0, &p, &n) && n >= 2 ? mcs_int(p[0] | p[1] << 8) : mcs_int(-1); }
NATIVE(dev_flags) { DEV(); const uint8_t* p; size_t n; return ad_find(d, 0x01, 0, &p, &n) && n >= 1 ? mcs_int(p[0]) : mcs_int(0); }
static int dev_uuids(const bdev_t* d, mcs_ble_uuid_t* out, int max) {
    static const uint8_t types[6] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    int c = 0;
    for (int t = 0; t < 6; t++) {
        int w = t < 2 ? 2 : t < 4 ? 4 : 16;
        for (int k = 0;; k++) {
            const uint8_t* p; size_t n;
            if (!ad_find(d, types[t], k, &p, &n)) break;
            for (size_t i = 0; i + (size_t)w <= n && c < max; i += (size_t)w) {
                memset(&out[c], 0, sizeof out[c]);
                out[c].len = (uint8_t)w; memcpy(out[c].b, p + i, (size_t)w);
                uuid_shrink(&out[c]);
                c++;
            }
        }
    }
    return c;
}
NATIVE(dev_services) {
    DEV();
    mcs_ble_uuid_t u[16];
    int n = dev_uuids(d, u, 16);
    mcs_value_t l = mcs_new_list(vm);
    mcs_push_root(vm, l);
    for (int i = 0; i < n; i++) { mcs_value_t v = uuid_val(vm, &u[i]); mcs_list_add(vm, l, v); }
    mcs_pop_root(vm, 1);
    return l;
}
NATIVE(dev_hasservice) {
    DEV();
    mcs_ble_uuid_t want, u[16];
    if (!uuid_arg(vm, argv[0], &want)) return mcs_null();
    int n = dev_uuids(d, u, 16);
    for (int i = 0; i < n; i++) if (mcs_ble_uuid_eq(&u[i], &want)) return mcs_bool(true);
    return mcs_bool(false);
}
NATIVE(dev_mfg) { DEV(); const uint8_t* p; size_t n; return ad_find(d, 0xFF, 0, &p, &n) && n >= 2 ? bytes_new(vm, p + 2, n - 2) : mcs_null(); }
NATIVE(dev_company) { DEV(); const uint8_t* p; size_t n; return ad_find(d, 0xFF, 0, &p, &n) && n >= 2 ? mcs_int(p[0] | p[1] << 8) : mcs_int(-1); }
static bool dev_sdata(const bdev_t* d, const mcs_ble_uuid_t* want, const uint8_t** data, size_t* dn) {
    static const uint8_t types[3] = { 0x16, 0x20, 0x21 }, widths[3] = { 2, 4, 16 };
    for (int t = 0; t < 3; t++)
        for (int k = 0;; k++) {
            const uint8_t* p; size_t n;
            if (!ad_find(d, types[t], k, &p, &n)) break;
            if (n < widths[t]) continue;
            mcs_ble_uuid_t u; memset(&u, 0, sizeof u);
            u.len = widths[t]; memcpy(u.b, p, widths[t]);
            if (mcs_ble_uuid_eq(&u, want)) { *data = p + widths[t]; *dn = n - widths[t]; return true; }
        }
    return false;
}
NATIVE(dev_servicedata) {
    DEV();
    mcs_ble_uuid_t want;
    if (!uuid_arg(vm, argv[0], &want)) return mcs_null();
    const uint8_t* p; size_t n;
    return dev_sdata(d, &want, &p, &n) ? bytes_new(vm, p, n) : mcs_null();
}
NATIVE(dev_data) { DEV(); return bytes_new(vm, d->adv, d->adv_len); }
NATIVE(dev_rsp) { DEV(); return bytes_new(vm, d->rsp, d->rsp_len); }
/* iBeacon: Apple manufacturer data 4C 00 02 15 <uuid 16> <major> <minor> <power> */
static const uint8_t* ibeacon(const bdev_t* d) {
    const uint8_t* p; size_t n;
    if (!ad_find(d, 0xFF, 0, &p, &n) || n < 25 || p[0] != 0x4C || p[1] != 0x00 || p[2] != 0x02 || p[3] != 0x15) return NULL;
    return p + 4;
}
NATIVE(dev_isibeacon) { DEV(); return mcs_bool(ibeacon(d) != NULL); }
NATIVE(dev_buuid) {
    DEV(); const uint8_t* b = ibeacon(d);
    if (!b) return mcs_null();
    mcs_ble_uuid_t u; u.len = 16;
    for (int i = 0; i < 16; i++) u.b[15 - i] = b[i];
    char s[37]; mcs_ble_uuid_t c = u; (void)c;
    int k = 0;   /* always the full 128-bit form for beacons */
    for (int i = 15; i >= 0; i--) { k += snprintf(s + k, sizeof s - (size_t)k, "%02X", u.b[i]); if (i == 12 || i == 10 || i == 8 || i == 6) s[k++] = '-'; }
    s[k] = 0;
    return mcs_string(vm, s);
}
NATIVE(dev_bmajor) { DEV(); const uint8_t* b = ibeacon(d); return b ? mcs_int(b[16] << 8 | b[17]) : mcs_int(-1); }
NATIVE(dev_bminor) { DEV(); const uint8_t* b = ibeacon(d); return b ? mcs_int(b[18] << 8 | b[19]) : mcs_int(-1); }
NATIVE(dev_bpower) { DEV(); const uint8_t* b = ibeacon(d); return b ? mcs_int((int8_t)b[20]) : mcs_null(); }
static const char* const g_url_scheme[4] = { "http://www.", "https://www.", "http://", "https://" };
static const char* const g_url_codes[14] = { ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
                                            ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov" };
NATIVE(dev_eddyurl) {
    DEV();
    mcs_ble_uuid_t ed; mcs_ble_uuid16(&ed, 0xFEAA);
    const uint8_t* p; size_t n;
    if (!dev_sdata(d, &ed, &p, &n) || n < 3 || p[0] != 0x10 || p[2] > 3) return mcs_null();
    char out[96]; size_t k = (size_t)snprintf(out, sizeof out, "%s", g_url_scheme[p[2]]);
    for (size_t i = 3; i < n && k < sizeof out - 8; i++) {
        if (p[i] < 14) k += (size_t)snprintf(out + k, sizeof out - k, "%s", g_url_codes[p[i]]);
        else if (p[i] > 0x20 && p[i] < 0x7F) out[k++] = (char)p[i];
    }
    out[k] = 0;
    return mcs_string(vm, out);
}
NATIVE(dev_tostring) {
    DEV(); char a[18], b[120];
    addr_fmt(&d->addr, a);
    mcs_value_t nm = dev_name_v(vm, d);
    const char* name = mcs_is_null(nm) ? NULL : mcs_cstr(nm);
    snprintf(b, sizeof b, "%s %s (%d dBm%s)", a, name ? name : (ibeacon(d) ? "<iBeacon>" : "<no name>"), d->rssi, d->connectable ? ", connectable" : "");
    return mcs_string(vm, b);
}
static mcs_value_t ble_connect_to(mcs_vm_t* vm, ble_state_t* s, const mcs_ble_addr_t* a, uint32_t timeout);
NATIVE(dev_connect) {
    DEV(); ble_state_t* s = bstate(vm);
    if (!s) return mcs_null();
    OPT_INT(0, t, MCS_BLE_CONNECT_TIMEOUT);
    mcs_ble_addr_t a = d->addr;
    return ble_connect_to(vm, s, &a, (uint32_t)(t < 1 ? 1 : t));
}
static const mcs_reg_t dev_members[] = {
    MCS_GET("Address", dev_address), MCS_GET("AddressType", dev_addrtype), MCS_GET("Name", dev_name),
    MCS_GET("Rssi", dev_rssi), MCS_GET("Connectable", dev_connectable), MCS_GET("TxPower", dev_txpower),
    MCS_GET("Appearance", dev_appearance), MCS_GET("Flags", dev_flags), MCS_GET("ServiceUuids", dev_services),
    MCS_FN("HasService", dev_hasservice, 1), MCS_GET("ManufacturerData", dev_mfg), MCS_GET("CompanyId", dev_company),
    MCS_FN("ServiceData", dev_servicedata, 1), MCS_GET("Data", dev_data), MCS_GET("ScanResponse", dev_rsp),
    MCS_GET("IsIBeacon", dev_isibeacon), MCS_GET("BeaconUuid", dev_buuid), MCS_GET("BeaconMajor", dev_bmajor),
    MCS_GET("BeaconMinor", dev_bminor), MCS_GET("BeaconPower", dev_bpower), MCS_GET("EddystoneUrl", dev_eddyurl),
    MCS_FN("Connect", dev_connect, -1), MCS_FN("ToString", dev_tostring, 0), MCS_REG_END };
static const mcs_class_def_t dev_def = { "BleDevice", sizeof(bdev_t), plain_ctor, NULL, dev_members, NULL };

/* recent advertisements, so scan responses reach StartScan callbacks with the advertising data */
static struct { mcs_ble_addr_t a; uint8_t len, type, data[31]; } g_recent[8];
static int g_recent_next;
static void dev_fill(bdev_t* d, const qrec_t* r, const uint8_t* data) {
    if (r->kind == MCS_BLE_ADV_SCAN_RSP) { memcpy(d->rsp, data, r->len); d->rsp_len = (uint8_t)r->len; }
    else {
        memcpy(d->adv, data, r->len); d->adv_len = (uint8_t)r->len; d->has_adv = 1;
        d->type = r->kind;
        d->connectable = r->kind == MCS_BLE_ADV_IND || r->kind == MCS_BLE_ADV_DIRECT_IND;
    }
    d->rssi = r->rssi;
}
static mcs_value_t dev_from_report(mcs_vm_t* vm, const qrec_t* r, const uint8_t* data) {
    mcs_value_t o;
    if (mcs_new_object(vm, "BleDevice", 0, NULL, &o) != MCS_OK) return mcs_null();
    bdev_t* d = (bdev_t*)mcs_userdata(o);
    d->addr = r->addr;
    if (r->kind == MCS_BLE_ADV_SCAN_RSP) {
        for (int i = 0; i < 8; i++)
            if (g_recent[i].len && !memcmp(&g_recent[i].a, &r->addr, sizeof r->addr)) {
                qrec_t a = *r; a.kind = g_recent[i].type; a.len = g_recent[i].len;
                dev_fill(d, &a, g_recent[i].data);
            }
    } else {
        int i = 0;
        for (; i < 8; i++) if (g_recent[i].len && !memcmp(&g_recent[i].a, &r->addr, sizeof r->addr)) break;
        if (i == 8) { i = g_recent_next; g_recent_next = (g_recent_next + 1) % 8; }
        g_recent[i].a = r->addr; g_recent[i].len = (uint8_t)(r->len ? r->len : 1); g_recent[i].type = r->kind;
        memcpy(g_recent[i].data, data, r->len);
    }
    dev_fill(d, r, data);
    return o;
}
/* blocking scan: merge into s->found */
static void found_merge(mcs_vm_t* vm, ble_state_t* s, const qrec_t* r, const uint8_t* data) {
    uint32_t n = mcs_len(s->found);
    for (uint32_t i = 0; i < n; i++) {
        bdev_t* d = (bdev_t*)mcs_userdata(mcs_index(s->found, i));
        if (!memcmp(&d->addr, &r->addr, sizeof r->addr)) { dev_fill(d, r, data); return; }
    }
    if (n >= MCS_BLE_SCAN_MAX) return;
    mcs_value_t o;
    if (mcs_new_object(vm, "BleDevice", 0, NULL, &o) != MCS_OK) return;
    bdev_t* d = (bdev_t*)mcs_userdata(o);
    d->addr = r->addr;
    dev_fill(d, r, data);
    mcs_list_add(vm, s->found, o);
}

/* ============================================================ BleAdvertisement */
typedef struct {
    char name[30];
    uint8_t name_set;                /* 0 = Ble.Name, 1 = name (empty: no name) */
    uint8_t connectable, flags, tx_power, nuuid, mfg_len, nsd;
    int16_t raw_len, rsp_len;        /* -1 = built from the fields */
    int32_t appearance;              /* -1 = none */
    uint16_t interval;
    uint32_t duration;
    mcs_ble_uuid_t uuids[4];
    uint8_t mfg[27];                 /* company id (little-endian) + data */
    struct { mcs_ble_uuid_t u; uint8_t n; uint8_t d[24]; } sd[2];
    uint8_t raw[31], rsp[31];
} badv_t;
static const mcs_class_def_t adv_def;
#define ADV() badv_t* v = (badv_t*)mcs_check_userdata(vm, self, &adv_def); if (!v) return mcs_null()
static void adv_defaults(badv_t* v) {
    memset(v, 0, sizeof *v);
    v->connectable = 1; v->flags = 0x06; v->raw_len = v->rsp_len = -1; v->appearance = -1; v->interval = 100;
}
static void adv_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    badv_t* v = (badv_t*)mcs_userdata(self);
    adv_defaults(v);
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "new BleAdvertisement([name])"); return; }
    if (argc == 1 && !mcs_is_null(argv[0])) {
        const char* n = mcs_to_cstr(vm, argv[0]);
        if (mcs_has_exception(vm)) return;
        snprintf(v->name, sizeof v->name, "%s", n);
        v->name_set = 1;
    }
}
/* AD structure writer: into adv, else the scan response */
typedef struct { uint8_t* b[2]; uint8_t n[2]; } adbuf_t;
static bool ad_put(adbuf_t* w, int prefer, int type, const uint8_t* p, size_t n) {
    for (int part = prefer; part < 2; part++) {
        if (w->n[part] + 2 + n > 31) continue;
        uint8_t* o = w->b[part] + w->n[part];
        o[0] = (uint8_t)(n + 1); o[1] = (uint8_t)type;
        memcpy(o + 2, p, n);
        w->n[part] = (uint8_t)(w->n[part] + 2 + n);
        return true;
    }
    return false;
}
/* 0 or an error message */
static const char* adv_build(ble_state_t* s, const badv_t* v, mcs_ble_adv_t* out) {
    memset(out, 0, sizeof *out);
    out->connectable = v->connectable;
    out->interval_ms = v->interval;
    out->duration_ms = v->duration;
    if (v->raw_len >= 0) {
        memcpy(out->data, v->raw, (size_t)v->raw_len); out->len = (uint8_t)v->raw_len;
        if (v->rsp_len >= 0) { memcpy(out->rsp, v->rsp, (size_t)v->rsp_len); out->rsp_len = (uint8_t)v->rsp_len; }
        out->scannable = out->rsp_len > 0 || v->connectable;
        return NULL;
    }
    adbuf_t w = { { out->data, out->rsp }, { 0, 0 } };
    if (v->flags) { uint8_t f = v->flags; ad_put(&w, 0, 0x01, &f, 1); }
    if (v->tx_power) {
        int32_t p = 0;
        if (s->ble && s->ble->opt_get) s->ble->opt_get(s->ble->ctx, MCS_BLE_OPT_TX_POWER, &p);
        uint8_t t = (uint8_t)(int8_t)p;
        if (!ad_put(&w, 0, 0x0A, &t, 1)) return "TX power does not fit";
    }
    if (v->appearance >= 0) {
        uint8_t a[2] = { (uint8_t)v->appearance, (uint8_t)(v->appearance >> 8) };
        if (!ad_put(&w, 0, 0x19, a, 2)) return "appearance does not fit";
    }
    static const uint8_t list_type[3] = { 0x03, 0x05, 0x07 };
    for (int wdt = 0; wdt < 3; wdt++) {
        int sz = wdt == 0 ? 2 : wdt == 1 ? 4 : 16;
        uint8_t tmp[29]; size_t n = 0;
        for (int i = 0; i < v->nuuid; i++) {
            mcs_ble_uuid_t u = v->uuids[i];
            uuid_shrink(&u);
            if (u.len != sz || n + (size_t)sz > sizeof tmp) continue;
            memcpy(tmp + n, u.b, (size_t)sz); n += (size_t)sz;
        }
        if (n && !ad_put(&w, 0, list_type[wdt], tmp, n)) return "service UUIDs do not fit in 31 + 31 bytes";
    }
    for (int i = 0; i < v->nsd; i++) {
        mcs_ble_uuid_t u = v->sd[i].u;
        uuid_shrink(&u);
        uint8_t tmp[29];
        if ((size_t)u.len + v->sd[i].n > sizeof tmp) return "service data too long";
        memcpy(tmp, u.b, u.len); memcpy(tmp + u.len, v->sd[i].d, v->sd[i].n);
        if (!ad_put(&w, 0, u.len == 2 ? 0x16 : u.len == 4 ? 0x20 : 0x21, tmp, (size_t)u.len + v->sd[i].n)) return "service data does not fit";
    }
    if (v->mfg_len && !ad_put(&w, 0, 0xFF, v->mfg, v->mfg_len)) return "manufacturer data does not fit";
    const char* name = v->name_set ? v->name : s->name;
    size_t nl = strlen(name);
    if (nl) {
        if (!ad_put(&w, 0, 0x09, (const uint8_t*)name, nl)) {     /* complete name: advertisement or scan response */
            int part = 31 - w.n[0] >= 3 ? 0 : 31 - w.n[1] >= 3 ? 1 : -1;   /* else shortened where room is left */
            if (part < 0) return "no room left for the name";
            ad_put(&w, part, 0x08, (const uint8_t*)name, (size_t)(31 - w.n[part] - 2));
        }
    }
    out->len = w.n[0]; out->rsp_len = w.n[1];
    if (v->rsp_len >= 0) { memcpy(out->rsp, v->rsp, (size_t)v->rsp_len); out->rsp_len = (uint8_t)v->rsp_len; }
    out->scannable = out->rsp_len > 0 || v->connectable;
    return NULL;
}
NATIVE(adv_getname) { ADV(); return v->name_set ? mcs_string(vm, v->name) : mcs_null(); }
NATIVE(adv_setname) {
    ADV();
    if (mcs_is_null(argv[0])) { v->name_set = 0; return mcs_null(); }
    STR_ARG(0, n);
    if (strlen(n) > 29) { mcs_raise(vm, "ArgumentException", "BleAdvertisement.Name: at most 29 characters"); return mcs_null(); }
    snprintf(v->name, sizeof v->name, "%s", n); v->name_set = 1;
    return mcs_null();
}
NATIVE(adv_getconn) { ADV(); return mcs_bool(v->connectable); }
NATIVE(adv_setconn) { ADV(); v->connectable = mcs_truthy(argv[0]); return mcs_null(); }
NATIVE(adv_getint) { ADV(); return mcs_int(v->interval); }
NATIVE(adv_setint) {
    ADV(); INT_ARG(0, ms);
    if (ms < 20 || ms > 10240) { mcs_raise(vm, "ArgumentOutOfRangeException", "Interval: 20..10240 ms"); return mcs_null(); }
    v->interval = (uint16_t)ms; return mcs_null();
}
NATIVE(adv_getdur) { ADV(); return mcs_int((mcs_int_t)v->duration); }
NATIVE(adv_setdur) { ADV(); INT_ARG(0, ms); v->duration = ms < 0 ? 0 : (uint32_t)ms; return mcs_null(); }
NATIVE(adv_gettx) { ADV(); return mcs_bool(v->tx_power); }
NATIVE(adv_settx) { ADV(); v->tx_power = mcs_truthy(argv[0]); return mcs_null(); }
NATIVE(adv_getapp) { ADV(); return mcs_int(v->appearance); }
NATIVE(adv_setapp) { ADV(); INT_ARG(0, a); v->appearance = a < 0 ? -1 : (int32_t)(a & 0xFFFF); return mcs_null(); }
NATIVE(adv_getflags) { ADV(); return mcs_int(v->flags); }
NATIVE(adv_setflags) { ADV(); INT_ARG(0, f); v->flags = (uint8_t)f; return mcs_null(); }
NATIVE(adv_adduuid) {
    ADV();
    if (v->nuuid >= 4) { mcs_raise(vm, "InvalidOperationException", "at most 4 service UUIDs"); return mcs_null(); }
    if (!uuid_arg(vm, argv[0], &v->uuids[v->nuuid])) return mcs_null();
    v->nuuid++;
    return self;
}
NATIVE(adv_setmfg) {     /* (companyId, data) */
    ADV(); INT_ARG(0, cid);
    size_t n = 0;
    if (!bytes_arg(vm, argv[1], v->mfg + 2, sizeof v->mfg - 2, &n, "ManufacturerData")) return mcs_null();
    v->mfg[0] = (uint8_t)cid; v->mfg[1] = (uint8_t)(cid >> 8); v->mfg_len = (uint8_t)(n + 2);
    return self;
}
NATIVE(adv_addsd) {      /* (uuid, data) */
    ADV();
    if (v->nsd >= 2) { mcs_raise(vm, "InvalidOperationException", "at most 2 service data entries"); return mcs_null(); }
    if (!uuid_arg(vm, argv[0], &v->sd[v->nsd].u)) return mcs_null();
    size_t n = 0;
    if (!bytes_arg(vm, argv[1], v->sd[v->nsd].d, sizeof v->sd[0].d, &n, "ServiceData")) return mcs_null();
    v->sd[v->nsd].n = (uint8_t)n; v->nsd++;
    return self;
}
NATIVE(adv_getraw) { ADV(); return v->raw_len < 0 ? mcs_null() : bytes_new(vm, v->raw, (size_t)v->raw_len); }
NATIVE(adv_setraw) {
    ADV();
    if (mcs_is_null(argv[0])) { v->raw_len = -1; return mcs_null(); }
    size_t n = 0;
    if (!bytes_arg(vm, argv[0], v->raw, 31, &n, "Raw")) return mcs_null();
    v->raw_len = (int16_t)n; return mcs_null();
}
NATIVE(adv_getrsp) { ADV(); return v->rsp_len < 0 ? mcs_null() : bytes_new(vm, v->rsp, (size_t)v->rsp_len); }
NATIVE(adv_setrsp) {
    ADV();
    if (mcs_is_null(argv[0])) { v->rsp_len = -1; return mcs_null(); }
    size_t n = 0;
    if (!bytes_arg(vm, argv[0], v->rsp, 31, &n, "ScanResponse")) return mcs_null();
    v->rsp_len = (int16_t)n; return mcs_null();
}
NATIVE(adv_build_n) {    /* Build() -> the advertising bytes (BuildScanResponse: the scan response) */
    ADV(); ble_state_t* s = bstate(vm);
    if (!s) return mcs_null();
    mcs_ble_adv_t a;
    const char* e = adv_build(s, v, &a);
    if (e) { mcs_raise(vm, "ArgumentException", "BleAdvertisement: %s", e); return mcs_null(); }
    return bytes_new(vm, a.data, a.len);
}
NATIVE(adv_build_r) {
    ADV(); ble_state_t* s = bstate(vm);
    if (!s) return mcs_null();
    mcs_ble_adv_t a;
    const char* e = adv_build(s, v, &a);
    if (e) { mcs_raise(vm, "ArgumentException", "BleAdvertisement: %s", e); return mcs_null(); }
    return bytes_new(vm, a.rsp, a.rsp_len);
}
static mcs_value_t adv_new(mcs_vm_t* vm, badv_t** out) {
    mcs_value_t o;
    if (mcs_new_object(vm, "BleAdvertisement", 0, NULL, &o) != MCS_OK) return mcs_null();
    *out = (badv_t*)mcs_userdata(o);
    return o;
}
NATIVE(adv_ibeacon) {    /* IBeacon(uuid, major, minor[, measuredPower = -59]) */
    if (argc < 3 || argc > 4) { mcs_raise(vm, "ArgumentException", "BleAdvertisement.IBeacon(uuid, major, minor[, measuredPower])"); return mcs_null(); }
    mcs_ble_uuid_t u;
    if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    INT_ARG(1, major); INT_ARG(2, minor); OPT_INT(3, power, -59);
    uint8_t full[16]; uuid_full(&u, full);
    badv_t* v; mcs_value_t o = adv_new(vm, &v);
    if (mcs_is_null(o)) return o;
    static const uint8_t head[9] = { 0x02, 0x01, 0x06, 0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15 };
    memcpy(v->raw, head, 9);
    for (int i = 0; i < 16; i++) v->raw[9 + i] = full[15 - i];
    v->raw[25] = (uint8_t)(major >> 8); v->raw[26] = (uint8_t)major;
    v->raw[27] = (uint8_t)(minor >> 8); v->raw[28] = (uint8_t)minor;
    v->raw[29] = (uint8_t)(int8_t)power;
    v->raw_len = 30; v->connectable = 0;
    return o;
}
NATIVE(adv_eddyurl) {    /* EddystoneUrl(url[, txPowerAt0m = -20]) */
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "BleAdvertisement.EddystoneUrl(url[, txPower])"); return mcs_null(); }
    STR_ARG(0, url); OPT_INT(1, power, -20);
    int scheme = -1;
    for (int i = 0; i < 4; i++) { size_t l = strlen(g_url_scheme[i]); if (!strncmp(url, g_url_scheme[i], l)) { scheme = i; url += l; break; } }
    if (scheme < 0) { mcs_raise(vm, "ArgumentException", "EddystoneUrl: the URL starts with http:// or https://"); return mcs_null(); }
    uint8_t enc[18]; size_t n = 0;
    while (*url) {
        int code = -1; size_t cl = 0;
        for (int c = 0; c < 14; c++) { size_t l = strlen(g_url_codes[c]); if (!strncmp(url, g_url_codes[c], l)) { code = c; cl = l; break; } }
        if (n >= 17) { mcs_raise(vm, "ArgumentException", "EddystoneUrl: URL too long after encoding (17 bytes)"); return mcs_null(); }
        if (code >= 0) { enc[n++] = (uint8_t)code; url += cl; } else enc[n++] = (uint8_t)*url++;
    }
    badv_t* v; mcs_value_t o = adv_new(vm, &v);
    if (mcs_is_null(o)) return o;
    uint8_t* r = v->raw; size_t k = 0;
    r[k++] = 0x02; r[k++] = 0x01; r[k++] = 0x06;
    r[k++] = 0x03; r[k++] = 0x03; r[k++] = 0xAA; r[k++] = 0xFE;
    r[k++] = (uint8_t)(6 + n); r[k++] = 0x16; r[k++] = 0xAA; r[k++] = 0xFE; r[k++] = 0x10; r[k++] = (uint8_t)(int8_t)power; r[k++] = (uint8_t)scheme;
    memcpy(r + k, enc, n); k += n;
    v->raw_len = (int16_t)k; v->connectable = 0;
    return o;
}
NATIVE(adv_eddyuid) {    /* EddystoneUid(namespace 10 bytes hex, instance 6 bytes hex[, txPower]) */
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "BleAdvertisement.EddystoneUid(namespaceHex, instanceHex[, txPower])"); return mcs_null(); }
    STR_ARG(0, ns); STR_ARG(1, inst); OPT_INT(2, power, -20);
    uint8_t id[16]; int k = 0;
    for (int part = 0; part < 2; part++) {
        const char* h = part ? inst : ns;
        int want = part ? 6 : 10;
        for (int i = 0; i < want; i++) {
            int a = hexv(h[0]), b = a < 0 ? -1 : hexv(h[1]);
            if (a < 0 || b < 0) { mcs_raise(vm, "ArgumentException", "EddystoneUid: namespace is 20 hex digits, instance 12"); return mcs_null(); }
            id[k++] = (uint8_t)(a << 4 | b); h += 2;
        }
        if (*h) { mcs_raise(vm, "ArgumentException", "EddystoneUid: namespace is 20 hex digits, instance 12"); return mcs_null(); }
    }
    badv_t* v; mcs_value_t o = adv_new(vm, &v);
    if (mcs_is_null(o)) return o;
    uint8_t* r = v->raw; size_t n = 0;
    r[n++] = 0x02; r[n++] = 0x01; r[n++] = 0x06;
    r[n++] = 0x03; r[n++] = 0x03; r[n++] = 0xAA; r[n++] = 0xFE;
    r[n++] = 0x15; r[n++] = 0x16; r[n++] = 0xAA; r[n++] = 0xFE; r[n++] = 0x00; r[n++] = (uint8_t)(int8_t)power;
    memcpy(r + n, id, 16); n += 16;
    r[n++] = 0; r[n++] = 0;
    v->raw_len = (int16_t)n; v->connectable = 0;
    return o;
}
static const mcs_reg_t adv_members[] = {
    MCS_GET("Name", adv_getname), MCS_SET("Name", adv_setname), MCS_GET("Connectable", adv_getconn), MCS_SET("Connectable", adv_setconn),
    MCS_GET("Interval", adv_getint), MCS_SET("Interval", adv_setint), MCS_GET("Duration", adv_getdur), MCS_SET("Duration", adv_setdur),
    MCS_GET("IncludeTxPower", adv_gettx), MCS_SET("IncludeTxPower", adv_settx), MCS_GET("Appearance", adv_getapp), MCS_SET("Appearance", adv_setapp),
    MCS_GET("Flags", adv_getflags), MCS_SET("Flags", adv_setflags), MCS_FN("AddServiceUuid", adv_adduuid, 1),
    MCS_FN("SetManufacturerData", adv_setmfg, 2), MCS_FN("AddServiceData", adv_addsd, 2),
    MCS_GET("Raw", adv_getraw), MCS_SET("Raw", adv_setraw), MCS_GET("ScanResponse", adv_getrsp), MCS_SET("ScanResponse", adv_setrsp),
    MCS_FN("Build", adv_build_n, 0), MCS_FN("BuildScanResponse", adv_build_r, 0), MCS_REG_END };
static const mcs_reg_t adv_statics[] = {
    MCS_FN("IBeacon", adv_ibeacon, -1), MCS_FN("EddystoneUrl", adv_eddyurl, -1), MCS_FN("EddystoneUid", adv_eddyuid, -1), MCS_REG_END };
static const mcs_class_def_t adv_def = { "BleAdvertisement", sizeof(badv_t), adv_ctor, NULL, adv_members, adv_statics };

/* ============================================================ local GATT database (C#) */
typedef struct { int slot; } bsvc_t;          /* g_svc index */
typedef struct { int id; } battr_t;           /* g_attr index */
static const mcs_class_def_t svc_def, chr_def, dsc_def;
static int attr_alloc(int kind, const mcs_ble_uuid_t* u, int props, size_t cap) {
    if (g_pool_used + cap > MCS_BLE_VALUE_POOL) return -2;
    for (int i = 0; i < MCS_BLE_MAX_ATTRS; i++) {
        if (g_attr[i].kind != A_FREE) continue;
        attr_t* a = &g_attr[i];
        memset(a, 0, sizeof *a);
        a->kind = (uint8_t)kind; a->uuid = *u; a->props = (uint16_t)props;
        a->cap = (uint16_t)cap; a->off = g_pool_used; a->def = -1;
        g_pool_used += (uint32_t)cap;
        return i;
    }
    return -1;
}
static bool attr_set(mcs_vm_t* vm, int id, mcs_value_t v) {
    attr_t* a = &g_attr[id];
    uint8_t tmp[MCS_BLE_MAX_VALUE];
    size_t n = 0;
    if (mcs_is_int(v)) {           /* an int: little-endian, as many bytes as the value needs (1, 2 or 4) */
        mcs_int_t x = mcs_to_int(vm, v);
        n = x >= -128 && x <= 255 ? 1 : x >= -32768 && x <= 65535 ? 2 : 4;
        for (size_t i = 0; i < n; i++) tmp[i] = (uint8_t)((uint32_t)x >> (8 * i));
    } else if (mcs_is_bool(v)) { tmp[0] = mcs_truthy(v); n = 1; }
    else if (!bytes_arg(vm, v, tmp, sizeof tmp, &n, "Value")) return false;
    if (n > a->cap) { mcs_raise(vm, "ArgumentException", "Value: %d bytes, MaxLength is %d", (int)n, a->cap); return false; }
    lock();
    memcpy(g_pool + a->off, tmp, n);
    a->len = (uint16_t)n;
    unlock();
    if (g_ble && g_ble->gatts_value) g_ble->gatts_value(g_ble->ctx, (uint16_t)id, tmp, n);
    return true;
}
static mcs_value_t attr_value(mcs_vm_t* vm, int id) {
    uint8_t tmp[MCS_BLE_MAX_VALUE];
    int n = mcs_ble_attr_read((uint16_t)id, -1, 0, tmp, sizeof tmp, NULL);
    return bytes_new(vm, tmp, n > 0 ? (size_t)n : 0);
}
static mcs_value_t attr_string(mcs_vm_t* vm, int id) {
    uint8_t tmp[MCS_BLE_MAX_VALUE];
    int n = mcs_ble_attr_read((uint16_t)id, -1, 0, tmp, sizeof tmp, NULL);
    return mcs_string_n(vm, (const char*)tmp, n > 0 ? (size_t)n : 0);
}

/* ---- BleService */
#define SVC() bsvc_t* sv = (bsvc_t*)mcs_check_userdata(vm, self, &svc_def); if (!sv) return mcs_null(); \
    ble_state_t* s = bstate(vm); if (!s) return mcs_null()
static void svc_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    bsvc_t* sv = (bsvc_t*)mcs_userdata(self);
    sv->slot = -1;
    ble_state_t* s = bstate(vm);
    if (!s) { mcs_raise(vm, "InvalidOperationException", "Bluetooth library not opened"); return; }
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "new BleService(uuid[, primary])"); return; }
    mcs_ble_uuid_t u;
    if (!uuid_arg(vm, argv[0], &u)) return;
    int i = 0;
    for (; i < MCS_BLE_MAX_SERVICES && g_svc[i].used; i++) {}
    if (i == MCS_BLE_MAX_SERVICES) { mcs_raise(vm, "InvalidOperationException", "too many services (MCS_BLE_MAX_SERVICES = %d; Ble.ClearServices() frees them)", MCS_BLE_MAX_SERVICES); return; }
    memset(&g_svc[i], 0, sizeof g_svc[i]);
    g_svc[i].used = 1; g_svc[i].uuid = u; g_svc[i].primary = argc < 2 || mcs_truthy(argv[1]);
    sv->slot = i;
    reg_set(vm, s, R_SVC + i, self);
}
static bool svc_ok(mcs_vm_t* vm, bsvc_t* sv) {
    if (sv->slot < 0 || !g_svc[sv->slot].used) { mcs_raise(vm, "InvalidOperationException", "the service was removed (Ble.ClearServices)"); return false; }
    return true;
}
static bool db_changeable(mcs_vm_t* vm, ble_state_t* s, const char* op) {
    if (s->advertising || nconn() > 0) {
        mcs_raise(vm, "InvalidOperationException", "%s: stop advertising and close the connections before changing the services", op);
        return false;
    }
    return true;
}
static mcs_value_t chr_create(mcs_vm_t* vm, ble_state_t* s, int svc, const mcs_ble_uuid_t* u, int props, mcs_value_t init, int max_len) {
    size_t cap = max_len > 0 ? (size_t)max_len : 0;
    if (!cap) {                  /* room for the first value, at least 20 bytes (one notification on a 23-byte MTU) */
        size_t l = mcs_is_null(init) ? 0 : mcs_is_string(init) ? mcs_strlen(init) : mcs_obj_kind(init) >= 0 ? mcs_len(init) : 4;
        cap = l > 20 ? l : 20;
    }
    if (cap > MCS_BLE_MAX_VALUE) { mcs_raise(vm, "ArgumentException", "maxLength: at most %d bytes (MCS_BLE_MAX_VALUE)", MCS_BLE_MAX_VALUE); return mcs_null(); }
    int id = attr_alloc(A_CHR, u, props, cap);
    if (id == -2) { mcs_raise(vm, "InvalidOperationException", "attribute values need more than MCS_BLE_VALUE_POOL = %d bytes", MCS_BLE_VALUE_POOL); return mcs_null(); }
    if (id < 0) { mcs_raise(vm, "InvalidOperationException", "too many characteristics and descriptors (MCS_BLE_MAX_ATTRS = %d)", MCS_BLE_MAX_ATTRS); return mcs_null(); }
    g_attr[id].svc = (uint8_t)svc;
    mcs_value_t o;
    if (mcs_new_object(vm, "BleCharacteristic", 0, NULL, &o) != MCS_OK) { g_attr[id].kind = A_FREE; return mcs_null(); }
    ((battr_t*)mcs_userdata(o))->id = id;
    reg_set(vm, s, R_ATTR + id, o);
    if (!mcs_is_null(init) && !attr_set(vm, id, init)) return mcs_null();
    s->db_dirty = 1;
    return o;
}
NATIVE(svc_addchr) {     /* AddCharacteristic(uuid, properties[, value[, maxLength]]) */
    SVC();
    if (!svc_ok(vm, sv)) return mcs_null();
    if (argc < 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "AddCharacteristic(uuid, properties[, value[, maxLength]])"); return mcs_null(); }
    if (g_svc[sv->slot].added && !db_changeable(vm, s, "AddCharacteristic")) return mcs_null();
    mcs_ble_uuid_t u;
    if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    INT_ARG(1, props); OPT_INT(3, maxlen, 0);
    if (!(props & 0xFF)) { mcs_raise(vm, "ArgumentException", "AddCharacteristic: give at least one property (BleProperty.Read, Write, Notify ...)"); return mcs_null(); }
    return chr_create(vm, s, sv->slot, &u, (int)props, argc > 2 ? argv[2] : mcs_null(), (int)maxlen);
}
NATIVE(svc_uuid) { SVC(); if (!svc_ok(vm, sv)) return mcs_null(); return uuid_val(vm, &g_svc[sv->slot].uuid); }
NATIVE(svc_primary) { SVC(); if (!svc_ok(vm, sv)) return mcs_null(); return mcs_bool(g_svc[sv->slot].primary); }
NATIVE(svc_handle) { SVC(); if (!svc_ok(vm, sv)) return mcs_null(); return mcs_int(g_svc[sv->slot].handle); }
NATIVE(svc_chars) {
    SVC();
    if (!svc_ok(vm, sv)) return mcs_null();
    mcs_value_t l = mcs_new_list(vm);
    mcs_push_root(vm, l);
    for (int i = 0; i < MCS_BLE_MAX_ATTRS; i++)
        if (g_attr[i].kind == A_CHR && g_attr[i].svc == sv->slot) mcs_list_add(vm, l, reg_get(vm, s, R_ATTR + i));
    mcs_pop_root(vm, 1);
    return l;
}
NATIVE(svc_tostring) {
    SVC();
    if (!svc_ok(vm, sv)) return mcs_string(vm, "BleService (removed)");
    char u[37], b[96]; mcs_ble_uuid_str(&g_svc[sv->slot].uuid, u);
    const char* nm = uuid_name(&g_svc[sv->slot].uuid);
    snprintf(b, sizeof b, "service %s%s%s%s", u, nm ? " (" : "", nm ? nm : "", nm ? ")" : "");
    return mcs_string(vm, b);
}
static const mcs_reg_t svc_members[] = {
    MCS_FN("AddCharacteristic", svc_addchr, -1), MCS_GET("Uuid", svc_uuid), MCS_GET("Primary", svc_primary),
    MCS_GET("Handle", svc_handle), MCS_GET("Characteristics", svc_chars), MCS_FN("ToString", svc_tostring, 0), MCS_REG_END };
static const mcs_class_def_t svc_def = { "BleService", sizeof(bsvc_t), svc_ctor, NULL, svc_members, NULL };

/* ---- BleCharacteristic */
#define CHR() battr_t* c = (battr_t*)mcs_check_userdata(vm, self, &chr_def); if (!c) return mcs_null(); \
    ble_state_t* s = bstate(vm); if (!s) return mcs_null(); \
    if (c->id < 0 || g_attr[c->id].kind != A_CHR) { mcs_raise(vm, "InvalidOperationException", "the characteristic was removed (Ble.ClearServices)"); return mcs_null(); } \
    attr_t* a = &g_attr[c->id]; (void)a
NATIVE(chr_uuid) { CHR(); return uuid_val(vm, &a->uuid); }
NATIVE(chr_props) { CHR(); return mcs_int(a->props); }
NATIVE(chr_handle) { CHR(); return mcs_int(a->def >= 0 ? g_chr_defs[a->def].handle : 0); }
NATIVE(chr_maxlen) { CHR(); return mcs_int(a->cap); }
NATIVE(chr_getvalue) { CHR(); return attr_value(vm, c->id); }
NATIVE(chr_setvalue) { CHR(); attr_set(vm, c->id, argv[0]); return mcs_null(); }
NATIVE(chr_string) { CHR(); return attr_string(vm, c->id); }
NATIVE(chr_subs) {
    CHR(); int n = 0;
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++) n += g_conn[i].used && a->subs[i];
    return mcs_int(n);
}
static int send_update(mcs_vm_t* vm, ble_state_t* s, int id, int indicate, int only_conn) {
    attr_t* a = &g_attr[id];
    if (a->def < 0 || !s->ble || !s->ble->gatts_notify) return 0;
    uint8_t tmp[MCS_BLE_MAX_VALUE];
    int n = mcs_ble_attr_read((uint16_t)id, -1, 0, tmp, sizeof tmp, NULL);
    if (n < 0) n = 0;
    int sent = 0, want = indicate ? MCS_BLE_SUB_INDICATE : MCS_BLE_SUB_NOTIFY;
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++) {
        if (!g_conn[i].used || !(a->subs[i] & want)) continue;
        if (only_conn >= 0 && g_conn[i].handle != only_conn) continue;
        int m = n, lim = g_conn[i].mtu > 3 ? g_conn[i].mtu - 3 : 20;
        if (m > lim) m = lim;
        int rc = s->ble->gatts_notify(s->ble->ctx, g_conn[i].handle, &g_chr_defs[a->def], tmp, (size_t)m, indicate);
        if (rc >= 0) sent++;
    }
    (void)vm;
    return sent;
}
static mcs_value_t chr_update(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, int indicate) {
    CHR();
    if (!(a->props & (indicate ? MCS_BLE_PROP_INDICATE : MCS_BLE_PROP_NOTIFY))) {
        mcs_raise(vm, "InvalidOperationException", "%s: the characteristic has no BleProperty.%s", indicate ? "Indicate" : "Notify", indicate ? "Indicate" : "Notify");
        return mcs_null();
    }
    if (argc > 0 && !mcs_is_null(argv[0]) && !attr_set(vm, c->id, argv[0])) return mcs_null();
    int only = -1;
    if (argc > 1 && !mcs_is_null(argv[1])) {
        bconn_t* b = (bconn_t*)mcs_userdata_of(argv[1], &conn_def);
        if (b) only = b->handle; else { INT_ARG(1, h); only = (int)h; }
    }
    return mcs_int(send_update(vm, s, c->id, indicate, only));
}
NATIVE(chr_notify) { return chr_update(vm, self, argc, argv, 0); }
NATIVE(chr_indicate) { return chr_update(vm, self, argc, argv, 1); }
NATIVE(chr_onwrite) { CHR(); reg_set(vm, s, R_WRITE + c->id, argv[0]); return mcs_null(); }
NATIVE(chr_onsub) { CHR(); reg_set(vm, s, R_SUBCB + c->id, argv[0]); return mcs_null(); }
NATIVE(chr_adddsc) {     /* AddDescriptor(uuid, value[, writable]) */
    CHR();
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "AddDescriptor(uuid, value[, writable])"); return mcs_null(); }
    if (g_svc[a->svc].added && !db_changeable(vm, s, "AddDescriptor")) return mcs_null();
    mcs_ble_uuid_t u;
    if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    uint16_t sh = mcs_ble_uuid_short(&u);
    if (sh == 0x2902 || sh == 0x2900) { mcs_raise(vm, "ArgumentException", "descriptor %04X is added by the stack", sh); return mcs_null(); }
    bool wr = argc > 2 && mcs_truthy(argv[2]);
    size_t l = mcs_is_string(argv[1]) ? mcs_strlen(argv[1]) : mcs_obj_kind(argv[1]) >= 0 ? mcs_len(argv[1]) : 4;
    int id = attr_alloc(A_DSC, &u, MCS_BLE_DSC_READ | (wr ? MCS_BLE_DSC_WRITE : 0) | (a->props & (MCS_BLE_PERM_ENCRYPTED | MCS_BLE_PERM_AUTHENTICATED)), l > 20 ? l : 20);
    if (id < 0) { mcs_raise(vm, "InvalidOperationException", "too many attributes or values (MCS_BLE_MAX_ATTRS / MCS_BLE_VALUE_POOL)"); return mcs_null(); }
    g_attr[id].svc = a->svc; g_attr[id].parent = (uint16_t)c->id;
    mcs_value_t o;
    if (mcs_new_object(vm, "BleDescriptor", 0, NULL, &o) != MCS_OK) { g_attr[id].kind = A_FREE; return mcs_null(); }
    ((battr_t*)mcs_userdata(o))->id = id;
    reg_set(vm, s, R_ATTR + id, o);
    if (!attr_set(vm, id, argv[1])) return mcs_null();
    s->db_dirty = 1;
    return o;
}
NATIVE(chr_setdesc) {    /* Description = "text": the 0x2901 user description descriptor */
    CHR();
    mcs_value_t args[2] = { mcs_int(0x2901), argv[0] };
    (void)a;
    return chr_adddsc(vm, self, 2, args);
}
NATIVE(chr_tostring) {
    CHR(); char u[37], b[110]; mcs_ble_uuid_str(&a->uuid, u);
    const char* nm = uuid_name(&a->uuid);
    snprintf(b, sizeof b, "characteristic %s%s%s%s [%s%s%s%s%s]", u, nm ? " (" : "", nm ? nm : "", nm ? ")" : "",
             a->props & MCS_BLE_PROP_READ ? " read" : "", a->props & MCS_BLE_PROP_WRITE ? " write" : "",
             a->props & MCS_BLE_PROP_WRITE_NR ? " write-nr" : "", a->props & MCS_BLE_PROP_NOTIFY ? " notify" : "",
             a->props & MCS_BLE_PROP_INDICATE ? " indicate" : "");
    return mcs_string(vm, b);
}
static const mcs_reg_t chr_members[] = {
    MCS_GET("Uuid", chr_uuid), MCS_GET("Properties", chr_props), MCS_GET("Handle", chr_handle), MCS_GET("MaxLength", chr_maxlen),
    MCS_GET("Value", chr_getvalue), MCS_SET("Value", chr_setvalue), MCS_FN("SetValue", chr_setvalue, 1), MCS_GET("StringValue", chr_string),
    MCS_GET("Subscribers", chr_subs), MCS_FN("Notify", chr_notify, -1), MCS_FN("Indicate", chr_indicate, -1),
    MCS_FN("OnWrite", chr_onwrite, 1), MCS_FN("OnSubscribe", chr_onsub, 1), MCS_FN("AddDescriptor", chr_adddsc, -1),
    MCS_SET("Description", chr_setdesc), MCS_FN("ToString", chr_tostring, 0), MCS_REG_END };
static const mcs_class_def_t chr_def = { "BleCharacteristic", sizeof(battr_t), plain_ctor, NULL, chr_members, NULL };

/* ---- BleDescriptor */
#define DSC() battr_t* c = (battr_t*)mcs_check_userdata(vm, self, &dsc_def); if (!c) return mcs_null(); \
    ble_state_t* s = bstate(vm); if (!s) return mcs_null(); \
    if (c->id < 0 || g_attr[c->id].kind != A_DSC) { mcs_raise(vm, "InvalidOperationException", "the descriptor was removed (Ble.ClearServices)"); return mcs_null(); } \
    attr_t* a = &g_attr[c->id]; (void)a
NATIVE(dsc_uuid) { DSC(); return uuid_val(vm, &a->uuid); }
NATIVE(dsc_handle) { DSC(); return mcs_int(a->def >= 0 ? g_dsc_defs[a->def].handle : 0); }
NATIVE(dsc_getvalue) { DSC(); return attr_value(vm, c->id); }
NATIVE(dsc_setvalue) { DSC(); attr_set(vm, c->id, argv[0]); return mcs_null(); }
NATIVE(dsc_string) { DSC(); return attr_string(vm, c->id); }
NATIVE(dsc_onwrite) { DSC(); reg_set(vm, s, R_WRITE + c->id, argv[0]); return mcs_null(); }
static const mcs_reg_t dsc_members[] = {
    MCS_GET("Uuid", dsc_uuid), MCS_GET("Handle", dsc_handle), MCS_GET("Value", dsc_getvalue), MCS_SET("Value", dsc_setvalue),
    MCS_GET("StringValue", dsc_string), MCS_FN("OnWrite", dsc_onwrite, 1), MCS_REG_END };
static const mcs_class_def_t dsc_def = { "BleDescriptor", sizeof(battr_t), plain_ctor, NULL, dsc_members, NULL };

/* hand the added services to the port */
static int publish(ble_state_t* s) {
    if (!s->db_dirty) return 0;
    if (!s->ble || !s->ble->gatts_set) {
        for (int i = 0; i < MCS_BLE_MAX_SERVICES; i++) if (g_svc[i].used && g_svc[i].added) return MCS_HAL_ENOTSUP;
        s->db_dirty = 0;
        return 0;
    }
    int ns = 0, nc = 0, nd = 0;
    for (int i = 0; i < MCS_BLE_MAX_ATTRS; i++) g_attr[i].def = -1;
    for (int i = 0; i < MCS_BLE_MAX_SERVICES; i++) {
        if (!g_svc[i].used || !g_svc[i].added) continue;
        mcs_ble_svc_def_t* sd = &g_svc_defs[ns++];
        memset(sd, 0, sizeof *sd);
        sd->uuid = g_svc[i].uuid; sd->primary = g_svc[i].primary; sd->chr = &g_chr_defs[nc];
        for (int k = 0; k < MCS_BLE_MAX_ATTRS; k++) {
            attr_t* a = &g_attr[k];
            if (a->kind != A_CHR || a->svc != i) continue;
            mcs_ble_chr_def_t* cd = &g_chr_defs[nc];
            memset(cd, 0, sizeof *cd);
            a->def = (int16_t)nc++;
            sd->nchr++;
            cd->uuid = a->uuid; cd->props = a->props; cd->id = (uint16_t)k; cd->max_len = a->cap;
            cd->dsc = &g_dsc_defs[nd];
            for (int j = 0; j < MCS_BLE_MAX_ATTRS; j++) {
                attr_t* d = &g_attr[j];
                if (d->kind != A_DSC || d->parent != k) continue;
                mcs_ble_dsc_def_t* dd = &g_dsc_defs[nd];
                memset(dd, 0, sizeof *dd);
                d->def = (int16_t)nd++;
                cd->ndsc++;
                dd->uuid = d->uuid; dd->flags = d->props; dd->id = (uint16_t)j;
            }
        }
    }
    g_nsvc_defs = ns;
    int rc = s->ble->gatts_set(s->ble->ctx, g_svc_defs, ns);
    if (rc < 0) return rc;
    for (int i = 0, k = 0; i < MCS_BLE_MAX_SERVICES; i++) if (g_svc[i].used && g_svc[i].added) g_svc[i].handle = g_svc_defs[k++].handle;
    s->db_dirty = 0;
    return 0;
}

/* ============================================================ connections + GATT client */
#define CONN() bconn_t* b = (bconn_t*)mcs_check_userdata(vm, self, &conn_def); if (!b) return mcs_null(); \
    ble_state_t* s = bstate(vm); if (!s) return mcs_null()
static bool conn_alive(const bconn_t* b) { return b->slot < MCS_BLE_MAX_CONN && g_conn[b->slot].used && g_conn[b->slot].gen == b->gen; }
static bool conn_need(mcs_vm_t* vm, const bconn_t* b, const char* op) {
    if (conn_alive(b)) return true;
    mcs_raise(vm, "InvalidOperationException", "%s: the connection is closed", op);
    return false;
}
#define LIVE(op) if (!conn_need(vm, b, op)) return mcs_null()
static int conn_info(ble_state_t* s, int handle, mcs_ble_conn_info_t* ci) {
    memset(ci, 0, sizeof *ci);
    return s->ble && s->ble->conn_info ? s->ble->conn_info(s->ble->ctx, handle, ci) : MCS_HAL_ENOTSUP;
}
NATIVE(cn_handle) { CONN(); return mcs_int(b->handle); }
NATIVE(cn_address) { CONN(); return addr_val(vm, &b->addr); }
NATIVE(cn_addrtype) { CONN(); return mcs_string(vm, addr_type_name(b->addr.type)); }
NATIVE(cn_role) { CONN(); return mcs_string(vm, b->role == MCS_BLE_ROLE_CENTRAL ? "central" : "peripheral"); }
NATIVE(cn_connected) { CONN(); return mcs_bool(conn_alive(b)); }
NATIVE(cn_mtu) {
    CONN(); LIVE("Mtu");
    mcs_ble_conn_info_t ci;
    if (conn_info(s, b->handle, &ci) >= 0 && ci.mtu) return mcs_int(ci.mtu);
    return mcs_int(g_conn[b->slot].mtu);
}
#define CI_FIELD(fn, expr) NATIVE(fn) { CONN(); LIVE("BleConnection"); mcs_ble_conn_info_t ci; \
    CALL("BleConnection", conn_info(s, b->handle, &ci)); return expr; }
CI_FIELD(cn_encrypted, mcs_bool(ci.encrypted))
CI_FIELD(cn_authenticated, mcs_bool(ci.authenticated))
CI_FIELD(cn_bonded, mcs_bool(ci.bonded))
CI_FIELD(cn_interval, mcs_int(ci.interval))
CI_FIELD(cn_latency, mcs_int(ci.latency))
CI_FIELD(cn_timeout, mcs_int(ci.timeout))
NATIVE(cn_rssi) {
    CONN(); LIVE("Rssi"); NEED(conn_rssi, "Rssi");
    int rc = BLE()->conn_rssi(BLE()->ctx, b->handle);
    if (rc > 0 || rc < -127) return fail(vm, "Rssi", rc > 0 ? MCS_HAL_ERR : rc);
    return mcs_int(rc);
}
NATIVE(cn_disconnect) {
    CONN();
    if (!conn_alive(b)) return mcs_null();
    NEED(disconnect, "Disconnect");
    CALL("Disconnect", BLE()->disconnect(BLE()->ctx, b->handle));
    /* wait for the link to go (the stack reports it) */
    uint32_t t0 = mcs_ticks(vm);
    while (conn_alive(b) && (uint32_t)(mcs_ticks(vm) - t0) < 2000) {
        int r = drain(vm, s);
        if (r < 0 || mcs_has_exception(vm)) return mcs_null();
        if (!conn_alive(b)) break;
        if (!ble_wait(vm, 5)) return mcs_null();
    }
    return mcs_null();
}
NATIVE(cn_pair) {        /* Pair() -> true when the link ended up encrypted */
    CONN(); LIVE("Pair"); NEED(pair, "Pair");
    CALL("Pair", BLE()->pair(BLE()->ctx, b->handle));
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        int r = drain(vm, s);
        if (r < 0 || mcs_has_exception(vm)) return mcs_null();
        mcs_ble_conn_info_t ci;
        if (!conn_alive(b)) return mcs_bool(false);
        if (conn_info(s, b->handle, &ci) >= 0 && ci.encrypted && s->draining == 0) {
            /* let PairingDone / Encrypted events through first */
            drain(vm, s);
            return mcs_bool(true);
        }
        if ((uint32_t)(mcs_ticks(vm) - t0) >= 30000) return mcs_bool(false);
        if (!ble_wait(vm, 5)) return mcs_null();
    }
}
NATIVE(cn_reqmtu) {      /* RequestMtu() -> the negotiated ATT MTU */
    CONN(); LIVE("RequestMtu"); NEED(exchange_mtu, "RequestMtu");
    op_begin(OP_MTU, b->handle);
    int rc = op_wait(vm, s, BLE()->exchange_mtu(BLE()->ctx, b->handle), s->timeout);
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) return fail(vm, "RequestMtu", rc);
    if (g_op.value > 0) g_conn[b->slot].mtu = (uint16_t)g_op.value;
    return mcs_int(g_conn[b->slot].mtu);
}
NATIVE(cn_update) {      /* UpdateParameters(minInterval, maxInterval, latency, timeout): 1.25 ms / 10 ms units */
    CONN(); LIVE("UpdateParameters"); NEED(conn_update, "UpdateParameters");
    INT_ARG(0, mn); INT_ARG(1, mx); INT_ARG(2, lat); INT_ARG(3, to);
    if (mn < 6 || mx > 3200 || mn > mx || lat < 0 || lat > 499 || to < 10 || to > 3200) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "UpdateParameters: interval 6..3200 (x 1.25 ms), latency 0..499, timeout 10..3200 (x 10 ms)");
        return mcs_null();
    }
    CALL("UpdateParameters", BLE()->conn_update(BLE()->ctx, b->handle, (int)mn, (int)mx, (int)lat, (int)to));
    return mcs_null();
}

/* ---- remote attributes */
typedef struct {
    uint8_t slot, kind, props;
    uint16_t gen, handle, end, cccd;   /* service: start..end; characteristic: value handle, last handle */
    int conn;
    mcs_ble_uuid_t uuid;
} rattr_t;
static const mcs_class_def_t rsvc_def, rchr_def, rdsc_def;
static int discover(mcs_vm_t* vm, ble_state_t* s, int conn, int kind, uint16_t start, uint16_t end) {
    if (!s->ble || !s->ble->discover) return MCS_HAL_ENOTSUP;
    op_begin(OP_DISCOVER, conn);
    return op_wait(vm, s, s->ble->discover(s->ble->ctx, conn, kind, start, end), s->timeout);
}
static mcs_value_t rattr_new(mcs_vm_t* vm, const char* cls, const bconn_t* b, const mcs_ble_attr_t* at, uint16_t end) {
    mcs_value_t o;
    if (mcs_new_object(vm, cls, 0, NULL, &o) != MCS_OK) return mcs_null();
    rattr_t* r = (rattr_t*)mcs_userdata(o);
    r->slot = b->slot; r->gen = b->gen; r->conn = b->handle; r->kind = at->kind; r->props = at->props;
    r->uuid = at->uuid; uuid_shrink(&r->uuid); r->handle = at->handle; r->end = end;
    return o;
}
/* services of the peer; filter: only this UUID (NULL = all) */
static mcs_value_t services_of(mcs_vm_t* vm, ble_state_t* s, const bconn_t* b, const mcs_ble_uuid_t* filter, bool first) {
    int rc = discover(vm, s, b->handle, MCS_BLE_DISC_SERVICES, 1, 0xFFFF);
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) return fail(vm, "Services", rc);
    int n = g_op.nattrs;
    mcs_ble_attr_t at[MCS_BLE_MAX_DISC];
    memcpy(at, g_op.attrs, sizeof(mcs_ble_attr_t) * (size_t)n);
    mcs_value_t l = first ? mcs_null() : mcs_new_list(vm);
    if (!first) mcs_push_root(vm, l);
    for (int i = 0; i < n; i++) {
        if (filter && !mcs_ble_uuid_eq(&at[i].uuid, filter)) continue;
        mcs_value_t o = rattr_new(vm, "BleRemoteService", b, &at[i], at[i].end);
        if (first) return o;
        mcs_list_add(vm, l, o);
    }
    if (!first) mcs_pop_root(vm, 1);
    return l;
}
static mcs_value_t chars_of(mcs_vm_t* vm, ble_state_t* s, const rattr_t* sv, const bconn_t* b, const mcs_ble_uuid_t* filter, bool first) {
    int rc = discover(vm, s, sv->conn, MCS_BLE_DISC_CHARACTERISTICS, sv->handle, sv->end);
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) return fail(vm, "Characteristics", rc);
    int n = g_op.nattrs;
    mcs_ble_attr_t at[MCS_BLE_MAX_DISC];
    memcpy(at, g_op.attrs, sizeof(mcs_ble_attr_t) * (size_t)n);
    mcs_value_t l = first ? mcs_null() : mcs_new_list(vm);
    if (!first) mcs_push_root(vm, l);
    for (int i = 0; i < n; i++) {
        /* a characteristic ends before the next declaration (or with its service) */
        uint16_t end = sv->end;
        for (int k = 0; k < n; k++) if (at[k].end > at[i].handle && (uint16_t)(at[k].end - 1) < end) end = (uint16_t)(at[k].end - 1);
        if (filter && !mcs_ble_uuid_eq(&at[i].uuid, filter)) continue;
        mcs_value_t o = rattr_new(vm, "BleRemoteCharacteristic", b, &at[i], end);
        if (first) return o;
        mcs_list_add(vm, l, o);
    }
    if (!first) mcs_pop_root(vm, 1);
    return l;
}
NATIVE(cn_services) { CONN(); LIVE("Services"); NEED(discover, "Services"); return services_of(vm, s, b, NULL, false); }
NATIVE(cn_service) {
    CONN(); LIVE("Service"); NEED(discover, "Service");
    mcs_ble_uuid_t u;
    if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    return services_of(vm, s, b, &u, true);
}
NATIVE(cn_characteristic) {      /* Characteristic(serviceUuid, characteristicUuid) -> null when missing */
    CONN(); LIVE("Characteristic"); NEED(discover, "Characteristic");
    mcs_ble_uuid_t su, cu;
    if (!uuid_arg(vm, argv[0], &su) || !uuid_arg(vm, argv[1], &cu)) return mcs_null();
    mcs_value_t sv = services_of(vm, s, b, &su, true);
    if (mcs_is_null(sv)) return sv;
    mcs_push_root(vm, sv);
    mcs_value_t r = chars_of(vm, s, (rattr_t*)mcs_userdata(sv), b, &cu, true);
    mcs_pop_root(vm, 1);
    return r;
}
NATIVE(cn_tostring) {
    CONN(); char a[18], t[80]; addr_fmt(&b->addr, a);
    snprintf(t, sizeof t, "connection %d to %s (%s%s)", b->handle, a, b->role == MCS_BLE_ROLE_CENTRAL ? "we are central" : "we are peripheral", conn_alive(b) ? "" : ", closed");
    return mcs_string(vm, t);
}
static const mcs_reg_t conn_members[] = {
    MCS_GET("Handle", cn_handle), MCS_GET("Address", cn_address), MCS_GET("AddressType", cn_addrtype), MCS_GET("Role", cn_role),
    MCS_GET("Connected", cn_connected), MCS_GET("Mtu", cn_mtu), MCS_GET("Rssi", cn_rssi), MCS_GET("Encrypted", cn_encrypted),
    MCS_GET("Authenticated", cn_authenticated), MCS_GET("Bonded", cn_bonded), MCS_GET("Interval", cn_interval),
    MCS_GET("Latency", cn_latency), MCS_GET("SupervisionTimeout", cn_timeout), MCS_FN("Disconnect", cn_disconnect, 0),
    MCS_FN("Pair", cn_pair, 0), MCS_FN("RequestMtu", cn_reqmtu, 0), MCS_FN("UpdateParameters", cn_update, 4),
    MCS_FN("Services", cn_services, 0), MCS_FN("Service", cn_service, 1), MCS_FN("Characteristic", cn_characteristic, 2),
    MCS_FN("ToString", cn_tostring, 0), MCS_REG_END };
static const mcs_class_def_t conn_def = { "BleConnection", sizeof(bconn_t), plain_ctor, NULL, conn_members, NULL };

#define RATTR(def, op) rattr_t* r = (rattr_t*)mcs_check_userdata(vm, self, &def); if (!r) return mcs_null(); \
    ble_state_t* s = bstate(vm); if (!s) return mcs_null(); \
    bconn_t bb = { r->conn, r->slot, 0, r->gen, { 0, { 0 } } }; bconn_t* b = &bb; (void)b
#define RLIVE(op) if (!conn_need(vm, b, op)) return mcs_null()
NATIVE(ra_uuid) {
    rattr_t* r = (rattr_t*)mcs_userdata(self);
    if (!r || mcs_obj_kind(self) != MCS_O_USERDATA) return mcs_null();
    return uuid_val(vm, &r->uuid);
}
NATIVE(ra_handle) { rattr_t* r = (rattr_t*)mcs_userdata(self); return r ? mcs_int(r->handle) : mcs_null(); }
NATIVE(ra_end) { rattr_t* r = (rattr_t*)mcs_userdata(self); return r ? mcs_int(r->end) : mcs_null(); }
NATIVE(ra_name) {
    rattr_t* r = (rattr_t*)mcs_userdata(self);
    const char* n = r ? uuid_name(&r->uuid) : NULL;
    return n ? mcs_string(vm, n) : mcs_null();
}
NATIVE(rs_chars) {
    RATTR(rsvc_def, "Characteristics"); RLIVE("Characteristics");
    return chars_of(vm, s, r, b, NULL, false);
}
NATIVE(rs_char) {
    RATTR(rsvc_def, "Characteristic"); RLIVE("Characteristic");
    mcs_ble_uuid_t u;
    if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    return chars_of(vm, s, r, b, &u, true);
}
NATIVE(rs_tostring) {
    rattr_t* r = (rattr_t*)mcs_check_userdata(vm, self, &rsvc_def);
    if (!r) return mcs_null();
    char u[37], t[100]; mcs_ble_uuid_str(&r->uuid, u);
    const char* nm = uuid_name(&r->uuid);
    snprintf(t, sizeof t, "service %s%s%s%s [%d..%d]", u, nm ? " (" : "", nm ? nm : "", nm ? ")" : "", r->handle, r->end);
    return mcs_string(vm, t);
}
static const mcs_reg_t rsvc_members[] = {
    MCS_GET("Uuid", ra_uuid), MCS_GET("Name", ra_name), MCS_GET("Handle", ra_handle), MCS_GET("EndHandle", ra_end),
    MCS_FN("Characteristics", rs_chars, 0), MCS_FN("Characteristic", rs_char, 1), MCS_FN("ToString", rs_tostring, 0), MCS_REG_END };
static const mcs_class_def_t rsvc_def = { "BleRemoteService", sizeof(rattr_t), plain_ctor, NULL, rsvc_members, NULL };

static mcs_value_t remote_read(mcs_vm_t* vm, ble_state_t* s, const bconn_t* b, uint16_t handle, bool as_string) {
    if (!s->ble || !s->ble->read) return fail(vm, "Read", MCS_HAL_ENOTSUP);
    op_begin(OP_READ, b->handle);
    int rc = op_wait(vm, s, s->ble->read(s->ble->ctx, b->handle, handle), s->timeout);
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) return fail(vm, "Read", rc);
    return as_string ? mcs_string_n(vm, (const char*)g_op.data, g_op.len) : bytes_new(vm, g_op.data, g_op.len);
}
static mcs_value_t remote_write(mcs_vm_t* vm, ble_state_t* s, const bconn_t* b, uint16_t handle, mcs_value_t v, int resp) {
    if (!s->ble || !s->ble->write) return fail(vm, "Write", MCS_HAL_ENOTSUP);
    uint8_t tmp[MCS_BLE_MAX_VALUE]; size_t n = 0;
    if (mcs_is_int(v)) { tmp[0] = (uint8_t)mcs_to_int(vm, v); n = 1; }
    else if (!bytes_arg(vm, v, tmp, sizeof tmp, &n, "Write")) return mcs_null();
    op_begin(OP_WRITE, b->handle);
    int rc = op_wait(vm, s, s->ble->write(s->ble->ctx, b->handle, handle, tmp, n, resp), s->timeout);
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) return fail(vm, "Write", rc);
    return mcs_null();
}
NATIVE(rc_props) { RATTR(rchr_def, "Properties"); return mcs_int(r->props); }
NATIVE(rc_canread) { RATTR(rchr_def, "CanRead"); return mcs_bool(r->props & MCS_BLE_PROP_READ); }
NATIVE(rc_canwrite) { RATTR(rchr_def, "CanWrite"); return mcs_bool(r->props & (MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR)); }
NATIVE(rc_cannotify) { RATTR(rchr_def, "CanNotify"); return mcs_bool(r->props & MCS_BLE_PROP_NOTIFY); }
NATIVE(rc_canindicate) { RATTR(rchr_def, "CanIndicate"); return mcs_bool(r->props & MCS_BLE_PROP_INDICATE); }
NATIVE(rc_read) { RATTR(rchr_def, "Read"); RLIVE("Read"); return remote_read(vm, s, b, r->handle, false); }
NATIVE(rc_readstr) { RATTR(rchr_def, "ReadString"); RLIVE("ReadString"); return remote_read(vm, s, b, r->handle, true); }
NATIVE(rc_write) {       /* Write(data[, withResponse]) - default: with response when the characteristic allows it */
    RATTR(rchr_def, "Write"); RLIVE("Write");
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Write(data[, withResponse])"); return mcs_null(); }
    int resp = argc > 1 ? mcs_truthy(argv[1]) : (r->props & MCS_BLE_PROP_WRITE) != 0 || !(r->props & MCS_BLE_PROP_WRITE_NR);
    return remote_write(vm, s, b, r->handle, argv[0], resp);
}
static int find_cccd(mcs_vm_t* vm, ble_state_t* s, rattr_t* r) {
    if (r->cccd) return r->cccd;
    if (r->end <= r->handle) return 0;
    int rc = discover(vm, s, r->conn, MCS_BLE_DISC_DESCRIPTORS, (uint16_t)(r->handle + 1), r->end);
    if (rc < 0) return rc;
    for (int i = 0; i < g_op.nattrs; i++) if (mcs_ble_uuid_short(&g_op.attrs[i].uuid) == 0x2902) { r->cccd = g_op.attrs[i].handle; break; }
    return r->cccd;
}
NATIVE(rc_subscribe) {   /* Subscribe(data => ...[, indicate]) */
    RATTR(rchr_def, "Subscribe"); RLIVE("Subscribe"); NEED(subscribe, "Subscribe");
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Subscribe(callback[, indicate])"); return mcs_null(); }
    int flags = argc > 1 && mcs_truthy(argv[1]) ? MCS_BLE_SUB_INDICATE
              : (r->props & MCS_BLE_PROP_NOTIFY) ? MCS_BLE_SUB_NOTIFY : MCS_BLE_SUB_INDICATE;
    if (!(r->props & (flags == MCS_BLE_SUB_NOTIFY ? MCS_BLE_PROP_NOTIFY : MCS_BLE_PROP_INDICATE))) {
        mcs_raise(vm, "InvalidOperationException", "Subscribe: the characteristic does not support %s", flags == MCS_BLE_SUB_NOTIFY ? "notifications" : "indications");
        return mcs_null();
    }
    int k = -1;
    for (int i = 0; i < MCS_BLE_MAX_SUBS; i++) if (g_csub[i].used && g_csub[i].slot == r->slot && g_csub[i].handle == r->handle) { k = i; break; }
    if (k < 0) for (int i = 0; i < MCS_BLE_MAX_SUBS; i++) if (!g_csub[i].used) { k = i; break; }
    if (k < 0) { mcs_raise(vm, "InvalidOperationException", "too many subscriptions (MCS_BLE_MAX_SUBS = %d)", MCS_BLE_MAX_SUBS); return mcs_null(); }
    int cccd = find_cccd(vm, s, r);
    if (mcs_has_exception(vm)) return mcs_null();
    if (cccd < 0) return fail(vm, "Subscribe", cccd);
    if (cccd == 0) { mcs_raise(vm, "InvalidOperationException", "Subscribe: the characteristic has no configuration descriptor (2902)"); return mcs_null(); }
    /* register first: the first notification may arrive before the write completes */
    g_csub[k].used = 1; g_csub[k].slot = r->slot; g_csub[k].gen = r->gen; g_csub[k].handle = r->handle;
    reg_set(vm, s, R_CSUB + k, argv[0]);
    op_begin(OP_SUBSCRIBE, r->conn);
    int rc = op_wait(vm, s, BLE()->subscribe(BLE()->ctx, r->conn, r->handle, (uint16_t)cccd, flags), s->timeout);
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) { g_csub[k].used = 0; reg_set(vm, s, R_CSUB + k, mcs_null()); return fail(vm, "Subscribe", rc); }
    return mcs_null();
}
NATIVE(rc_unsubscribe) {
    RATTR(rchr_def, "Unsubscribe");
    for (int i = 0; i < MCS_BLE_MAX_SUBS; i++)
        if (g_csub[i].used && g_csub[i].slot == r->slot && g_csub[i].handle == r->handle) { g_csub[i].used = 0; reg_set(vm, s, R_CSUB + i, mcs_null()); }
    if (!conn_alive(b) || !BLE() || !BLE()->subscribe) return mcs_null();
    int cccd = find_cccd(vm, s, r);
    if (cccd <= 0 || mcs_has_exception(vm)) return mcs_null();
    op_begin(OP_SUBSCRIBE, r->conn);
    int rc = op_wait(vm, s, BLE()->subscribe(BLE()->ctx, r->conn, r->handle, (uint16_t)cccd, 0), s->timeout);
    if (rc < 0 && !mcs_has_exception(vm)) return fail(vm, "Unsubscribe", rc);
    return mcs_null();
}
NATIVE(rc_descs) {
    RATTR(rchr_def, "Descriptors"); RLIVE("Descriptors");
    mcs_value_t l = mcs_new_list(vm);
    if (r->end <= r->handle) return l;
    mcs_push_root(vm, l);
    int rc = discover(vm, s, r->conn, MCS_BLE_DISC_DESCRIPTORS, (uint16_t)(r->handle + 1), r->end);
    if (mcs_has_exception(vm)) { mcs_pop_root(vm, 1); return mcs_null(); }
    if (rc < 0) { mcs_pop_root(vm, 1); return fail(vm, "Descriptors", rc); }
    int n = g_op.nattrs;
    mcs_ble_attr_t at[MCS_BLE_MAX_DISC];
    memcpy(at, g_op.attrs, sizeof(mcs_ble_attr_t) * (size_t)n);
    for (int i = 0; i < n; i++) {
        if (mcs_ble_uuid_short(&at[i].uuid) == 0x2902) r->cccd = at[i].handle;
        mcs_value_t o = rattr_new(vm, "BleRemoteDescriptor", b, &at[i], at[i].handle);
        mcs_list_add(vm, l, o);
    }
    mcs_pop_root(vm, 1);
    return l;
}
NATIVE(rc_tostring) {
    rattr_t* r = (rattr_t*)mcs_check_userdata(vm, self, &rchr_def);
    if (!r) return mcs_null();
    char u[37], t[110]; mcs_ble_uuid_str(&r->uuid, u);
    const char* nm = uuid_name(&r->uuid);
    snprintf(t, sizeof t, "characteristic %s%s%s%s @%d [%s%s%s%s%s]", u, nm ? " (" : "", nm ? nm : "", nm ? ")" : "", r->handle,
             r->props & MCS_BLE_PROP_READ ? " read" : "", r->props & MCS_BLE_PROP_WRITE ? " write" : "",
             r->props & MCS_BLE_PROP_WRITE_NR ? " write-nr" : "", r->props & MCS_BLE_PROP_NOTIFY ? " notify" : "",
             r->props & MCS_BLE_PROP_INDICATE ? " indicate" : "");
    return mcs_string(vm, t);
}
static const mcs_reg_t rchr_members[] = {
    MCS_GET("Uuid", ra_uuid), MCS_GET("Name", ra_name), MCS_GET("Handle", ra_handle), MCS_GET("Properties", rc_props),
    MCS_GET("CanRead", rc_canread), MCS_GET("CanWrite", rc_canwrite), MCS_GET("CanNotify", rc_cannotify),
    MCS_GET("CanIndicate", rc_canindicate), MCS_FN("Read", rc_read, 0), MCS_FN("ReadString", rc_readstr, 0),
    MCS_FN("Write", rc_write, -1), MCS_FN("Subscribe", rc_subscribe, -1), MCS_FN("Unsubscribe", rc_unsubscribe, 0),
    MCS_FN("Descriptors", rc_descs, 0), MCS_FN("ToString", rc_tostring, 0), MCS_REG_END };
static const mcs_class_def_t rchr_def = { "BleRemoteCharacteristic", sizeof(rattr_t), plain_ctor, NULL, rchr_members, NULL };

NATIVE(rd_read) { RATTR(rdsc_def, "Read"); RLIVE("Read"); return remote_read(vm, s, b, r->handle, false); }
NATIVE(rd_write) { RATTR(rdsc_def, "Write"); RLIVE("Write"); return remote_write(vm, s, b, r->handle, argv[0], 1); }
NATIVE(rd_tostring) {
    rattr_t* r = (rattr_t*)mcs_check_userdata(vm, self, &rdsc_def);
    if (!r) return mcs_null();
    char u[37], t[100]; mcs_ble_uuid_str(&r->uuid, u);
    const char* nm = uuid_name(&r->uuid);
    snprintf(t, sizeof t, "descriptor %s%s%s%s @%d", u, nm ? " (" : "", nm ? nm : "", nm ? ")" : "", r->handle);
    return mcs_string(vm, t);
}
static const mcs_reg_t rdsc_members[] = {
    MCS_GET("Uuid", ra_uuid), MCS_GET("Name", ra_name), MCS_GET("Handle", ra_handle), MCS_FN("Read", rd_read, 0),
    MCS_FN("Write", rd_write, 1), MCS_FN("ToString", rd_tostring, 0), MCS_REG_END };
static const mcs_class_def_t rdsc_def = { "BleRemoteDescriptor", sizeof(rattr_t), plain_ctor, NULL, rdsc_members, NULL };

/* ============================================================ BleUart (Nordic UART Service, peripheral) */
typedef struct {
    int rx, tx, svc;
    uint16_t head, n;
    uint8_t buf[MCS_BLE_UART_RX];
} buart_t;
static const mcs_class_def_t uart_def;
#define UART() buart_t* u = (buart_t*)mcs_check_userdata(vm, self, &uart_def); if (!u) return mcs_null(); \
    ble_state_t* s = bstate(vm); if (!s) return mcs_null(); \
    if (u->rx < 0 || g_attr[u->rx].kind != A_CHR) { mcs_raise(vm, "InvalidOperationException", "the BleUart service was removed (Ble.ClearServices)"); return mcs_null(); }
static void uart_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    buart_t* u = (buart_t*)mcs_userdata(self);
    u->rx = u->tx = u->svc = -1;
    ble_state_t* s = bstate(vm);
    if (!s) { mcs_raise(vm, "InvalidOperationException", "Bluetooth library not opened"); return; }
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "new BleUart([name])"); return; }
    if (!mcs_is_null(reg_get(vm, s, R_UART))) { mcs_raise(vm, "InvalidOperationException", "only one BleUart (Ble.ClearServices() removes it)"); return; }
    if (!db_changeable(vm, s, "new BleUart")) return;
    if (argc == 1 && !mcs_is_null(argv[0])) {
        const char* n = mcs_to_cstr(vm, argv[0]);
        if (mcs_has_exception(vm)) return;
        snprintf(s->name, sizeof s->name, "%s", n);
        if (s->on && s->ble->set_name) s->ble->set_name(s->ble->ctx, s->name);
    }
    mcs_value_t sv;
    mcs_value_t a0 = mcs_string(vm, "6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    if (mcs_new_object(vm, "BleService", 1, &a0, &sv) != MCS_OK || mcs_has_exception(vm)) return;
    mcs_push_root(vm, sv);
    int slot = ((bsvc_t*)mcs_userdata(sv))->slot;
    mcs_ble_uuid_t rx, tx; nus_uuid(&rx, 2); nus_uuid(&tx, 3);
    mcs_value_t r = chr_create(vm, s, slot, &rx, MCS_BLE_PROP_WRITE | MCS_BLE_PROP_WRITE_NR, mcs_null(), 244);
    if (mcs_is_null(r)) { mcs_pop_root(vm, 1); return; }
    mcs_value_t t = chr_create(vm, s, slot, &tx, MCS_BLE_PROP_NOTIFY, mcs_null(), 244);
    mcs_pop_root(vm, 1);
    if (mcs_is_null(t)) return;
    u->svc = slot; u->rx = ((battr_t*)mcs_userdata(r))->id; u->tx = ((battr_t*)mcs_userdata(t))->id;
    g_attr[u->rx].uart = 1;
    g_svc[slot].added = 1;
    s->db_dirty = 1;
    reg_set(vm, s, R_UART, self);
}
static void uart_push(buart_t* u, const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (u->n == MCS_BLE_UART_RX) { u->head = (uint16_t)((u->head + 1) % MCS_BLE_UART_RX); u->n--; }   /* keep the newest */
        u->buf[(u->head + u->n) % MCS_BLE_UART_RX] = d[i];
        u->n++;
    }
}
static int uart_send(mcs_vm_t* vm, ble_state_t* s, buart_t* u, const uint8_t* p, size_t n) {
    int lim = 244;
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++)
        if (g_conn[i].used && g_attr[u->tx].subs[i] && g_conn[i].mtu > 3 && g_conn[i].mtu - 3 < lim) lim = g_conn[i].mtu - 3;
    int sent = 0;
    for (size_t off = 0; off < n;) {
        size_t m = n - off > (size_t)lim ? (size_t)lim : n - off;
        lock(); memcpy(g_pool + g_attr[u->tx].off, p + off, m); g_attr[u->tx].len = (uint16_t)m; unlock();
        if (send_update(vm, s, u->tx, 0, -1) == 0) break;
        off += m; sent += (int)m;
    }
    return sent;
}
static mcs_value_t uart_write_v(mcs_vm_t* vm, mcs_value_t self, mcs_value_t v, bool nl) {
    UART();
    uint8_t tmp[MCS_BLE_MAX_VALUE + 1]; size_t n = 0;
    if (!mcs_is_string(v) && mcs_obj_kind(v) != MCS_O_ARRAY && mcs_obj_kind(v) != MCS_O_LIST) {
        mcs_value_t str = mcs_tostring(vm, v);
        CHECK();
        v = str;
    }
    if (!bytes_arg(vm, v, tmp, MCS_BLE_MAX_VALUE, &n, "BleUart.Write")) return mcs_null();
    if (nl) tmp[n++] = '\n';
    return mcs_int(uart_send(vm, s, u, tmp, n));
}
NATIVE(uart_write) { return uart_write_v(vm, self, argv[0], false); }
NATIVE(uart_writeline) { return uart_write_v(vm, self, argc ? argv[0] : mcs_string(vm, ""), true); }
NATIVE(uart_available) { UART(); drain(vm, s); return mcs_int(u->n); }
NATIVE(uart_read) {
    UART(); drain(vm, s);
    if (!u->n) return mcs_int(-1);
    int c = u->buf[u->head]; u->head = (uint16_t)((u->head + 1) % MCS_BLE_UART_RX); u->n--;
    return mcs_int(c);
}
NATIVE(uart_readstring) {
    UART(); drain(vm, s);
    char tmp[MCS_BLE_UART_RX]; size_t n = 0;
    while (u->n) { tmp[n++] = (char)u->buf[u->head]; u->head = (uint16_t)((u->head + 1) % MCS_BLE_UART_RX); u->n--; }
    return mcs_string_n(vm, tmp, n);
}
NATIVE(uart_readline) {  /* a whole line without "\n" / "\r\n", or null */
    UART(); drain(vm, s);
    size_t i = 0;
    for (; i < u->n && u->buf[(u->head + i) % MCS_BLE_UART_RX] != '\n'; i++) {}
    if (i == u->n) return mcs_null();
    char tmp[MCS_BLE_UART_RX]; size_t n = 0;
    for (size_t k = 0; k <= i; k++) { tmp[n++] = (char)u->buf[u->head]; u->head = (uint16_t)((u->head + 1) % MCS_BLE_UART_RX); u->n--; }
    n--;
    if (n && tmp[n - 1] == '\r') n--;
    return mcs_string_n(vm, tmp, n);
}
NATIVE(uart_onreceive) { UART(); reg_set(vm, s, R_WRITE + u->rx, argv[0]); return mcs_null(); }
NATIVE(uart_connected) {
    UART(); drain(vm, s);
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++) if (g_conn[i].used && g_attr[u->tx].subs[i]) return mcs_bool(true);
    return mcs_bool(false);
}
static mcs_value_t ble_start_adv(mcs_vm_t* vm, ble_state_t* s, mcs_value_t arg);
NATIVE(uart_start) {     /* advertise the UART service: Start([name]) */
    UART();
    if (argc > 0 && !mcs_is_null(argv[0])) {
        STR_ARG(0, n);
        snprintf(s->name, sizeof s->name, "%s", n);
        if (s->on && s->ble->set_name) s->ble->set_name(s->ble->ctx, s->name);
    }
    return ble_start_adv(vm, s, mcs_null());
}
static const mcs_reg_t uart_members[] = {
    MCS_FN("Write", uart_write, 1), MCS_FN("WriteLine", uart_writeline, -1), MCS_GET("Available", uart_available),
    MCS_FN("Read", uart_read, 0), MCS_FN("ReadString", uart_readstring, 0), MCS_FN("ReadLine", uart_readline, 0),
    MCS_FN("OnReceive", uart_onreceive, 1), MCS_GET("Connected", uart_connected), MCS_FN("Start", uart_start, -1), MCS_REG_END };
static const mcs_class_def_t uart_def = { "BleUart", sizeof(buart_t), uart_ctor, NULL, uart_members, NULL };

/* ============================================================ the queue -> C# callbacks */
static int dispatch(mcs_vm_t* vm, ble_state_t* s, const qrec_t* r, const uint8_t* data) {
    int ran = 0, rc = 0;
    mcs_value_t fn;
    int slot = r->ev == MCS_BLE_EV_REPORT ? -1 : conn_slot((int)r->conn);
    mcs_value_t co = slot >= 0 ? reg_get(vm, s, R_CONN + slot) : mcs_int(r->conn);
    switch (r->ev) {
    case MCS_BLE_EV_REPORT:
        if (s->collecting && !mcs_is_null(s->found)) { found_merge(vm, s, r, data); return 0; }
        fn = reg_get(vm, s, R_SCAN);
        if (mcs_is_null(fn)) return 0;
        {
            mcs_value_t d = dev_from_report(vm, r, data);
            if (mcs_is_null(d)) return 0;
            mcs_push_root(vm, d);
            rc = call_cb(vm, fn, &d, 1, NULL);
            mcs_pop_root(vm, 1);
        }
        return rc;
    case MCS_BLE_EV_CONNECT: {
        if (r->value == MCS_BLE_ROLE_PERIPHERAL) s->advertising = 0;   /* a legacy advertising set stops on connect */
        mcs_value_t o = conn_add(vm, s, (int)r->conn, &r->addr, (int)r->value);
        if (mcs_is_null(o)) return 0;
        co = o;
        fn = reg_get(vm, s, R_CONNECT);
        if (!mcs_is_null(fn)) { mcs_push_root(vm, o); rc = call_cb(vm, fn, &o, 1, NULL); mcs_pop_root(vm, 1); if (rc < 0) return rc; ran += rc; }
        break;
    }
    case MCS_BLE_EV_DISCONNECT:
        if (slot >= 0) {
            mcs_value_t o = co;
            mcs_push_root(vm, o);
            int role = g_conn[slot].role;
            conn_remove(vm, s, slot);
            fn = reg_get(vm, s, R_DISCONNECT);
            if (!mcs_is_null(fn)) { mcs_value_t a[2] = { o, mcs_int(r->value) }; rc = call_cb(vm, fn, a, 2, NULL); }
            mcs_pop_root(vm, 1);
            if (rc < 0) return rc;
            ran += rc;
            /* peripheral: be connectable again */
            if (role == MCS_BLE_ROLE_PERIPHERAL && s->want_adv && s->auto_adv && !s->advertising && s->on && s->ble->adv_start
                && s->ble->adv_start(s->ble->ctx, &s->adv) >= 0) s->advertising = 1;
        }
        break;
    case MCS_BLE_EV_ADV_DONE: s->advertising = 0; break;
    case MCS_BLE_EV_SCAN_DONE: s->scanning = 0; break;
    case MCS_BLE_EV_MTU: if (slot >= 0 && r->value >= 23) g_conn[slot].mtu = (uint16_t)r->value; break;
    case MCS_BLE_EV_SUBSCRIBE:
        if (r->id < MCS_BLE_MAX_ATTRS && slot >= 0) {
            g_attr[r->id].subs[slot] = r->kind & 3;
            fn = reg_get(vm, s, R_SUBCB + r->id);
            if (!mcs_is_null(fn)) { mcs_value_t a[2] = { co, mcs_int(r->kind & 3) }; rc = call_cb(vm, fn, a, 2, NULL); if (rc < 0) return rc; ran += rc; }
        }
        break;
    case MCS_BLE_EV_WRITE:
        if (r->id < MCS_BLE_MAX_ATTRS && g_attr[r->id].kind != A_FREE) {
            fn = reg_get(vm, s, R_WRITE + r->id);
            if (g_attr[r->id].uart) {
                mcs_value_t uo = reg_get(vm, s, R_UART);
                buart_t* u = (buart_t*)mcs_userdata(uo);
                if (mcs_is_null(fn)) { if (u) uart_push(u, data, r->len); return 0; }
                mcs_value_t str = mcs_string_n(vm, (const char*)data, r->len);
                mcs_push_root(vm, str);
                rc = call_cb(vm, fn, &str, 1, NULL);
                mcs_pop_root(vm, 1);
                return rc;
            }
            if (mcs_is_null(fn)) return 0;
            mcs_value_t a[2] = { bytes_new(vm, data, r->len), co };
            mcs_push_root(vm, a[0]);
            rc = call_cb(vm, fn, a, 2, NULL);
            mcs_pop_root(vm, 1);
            return rc;
        }
        return 0;
    case MCS_BLE_EV_NOTIFY:
        for (int i = 0; i < MCS_BLE_MAX_SUBS; i++) {
            if (!g_csub[i].used || slot < 0 || g_csub[i].slot != slot || g_csub[i].handle != r->id) continue;
            fn = reg_get(vm, s, R_CSUB + i);
            if (mcs_is_null(fn)) continue;
            mcs_value_t a[2] = { bytes_new(vm, data, r->len), co };
            mcs_push_root(vm, a[0]);
            rc = call_cb(vm, fn, a, 2, NULL);
            mcs_pop_root(vm, 1);
            if (rc < 0) return rc;
            ran += rc;
        }
        return ran;
    case MCS_BLE_EV_PASSKEY: {
        fn = reg_get(vm, s, R_PASSKEY);
        int32_t reply = r->kind == MCS_BLE_PASSKEY_ENTRY ? s->passkey : 1;
        if (!mcs_is_null(fn)) {
            mcs_value_t res = mcs_null();
            mcs_value_t a[3] = { co, mcs_int(r->kind), mcs_int(r->value) };
            rc = call_cb(vm, fn, a, 3, &res);
            if (rc < 0) return rc;
            ran += rc;
            if (mcs_is_int(res)) reply = (int32_t)mcs_to_int(vm, res);
            else if (mcs_is_bool(res)) reply = mcs_truthy(res);
        }
        if (r->kind != MCS_BLE_PASSKEY_DISPLAY && s->ble->passkey_reply)
            s->ble->passkey_reply(s->ble->ctx, (int)r->conn, r->kind, reply);
        break;
    }
    default: break;
    }
    /* every other event (and connect / disconnect / subscribe) also goes to Ble.OnEvent */
    fn = reg_get(vm, s, R_EVENT);
    if (mcs_is_null(fn) || r->ev == MCS_BLE_EV_PASSKEY) return ran;
    mcs_value_t a[3] = { mcs_int(r->ev), mcs_int(r->conn), mcs_int(r->value) };
    rc = call_cb(vm, fn, a, 3, NULL);
    return rc < 0 ? rc : ran + rc;
}
static int drain(mcs_vm_t* vm, ble_state_t* s) {
    if (!s || s->draining > 3) return 0;
    if (s->ble && s->ble->poll) s->ble->poll(s->ble->ctx);
    s->draining++;
    int ran = 0;
    qrec_t r;
    static uint8_t data[MCS_BLE_MAX_VALUE];   /* VM thread only; copied before any callback runs */
    uint8_t local[MCS_BLE_MAX_VALUE];
    for (int budget = 256; budget > 0 && q_pop(&r, data, sizeof data); budget--) {
        memcpy(local, data, r.len);
        int rc = dispatch(vm, s, &r, local);
        if (rc < 0 || mcs_has_exception(vm)) { s->draining--; return rc < 0 ? rc : -1; }
        ran += rc;
    }
    s->draining--;
    return ran;
}
#if MCS_ENABLE_HAL
static int ble_event(mcs_vm_t* vm, const mcs_hal_event_t* ev) {
    (void)ev;
    ble_state_t* s = bstate(vm);
    return s ? drain(vm, s) : 0;
}
#endif

/* ============================================================ Ble */
static bool need_on(mcs_vm_t* vm, ble_state_t* s, const char* op) {
    if (!s->ble || !s->ble->enable) { fail(vm, op, MCS_HAL_ENOTSUP); return false; }
    if (s->on) return true;
    int rc = s->ble->enable(s->ble->ctx, 1);
    if (rc < 0) { fail(vm, op, rc); return false; }
    s->on = 1;
    if (s->ble->set_name) s->ble->set_name(s->ble->ctx, s->name);
    return true;
}
NATIVE(ble_enable) { ST(); ON("Ble.Enable"); return mcs_null(); }
static void ble_off(mcs_vm_t* vm, ble_state_t* s) {
    if (!s->on) return;
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++) if (g_conn[i].used) conn_remove(vm, s, i);
    s->ble->enable(s->ble->ctx, 0);
    s->on = s->advertising = s->want_adv = s->scanning = 0;
    s->db_dirty = 1;               /* the stack forgot the services */
    q_clear();
}
NATIVE(ble_disable) { ST(); ble_off(vm, s); return mcs_null(); }
NATIVE(ble_enabled) { ST(); return mcs_bool(s->on); }
NATIVE(ble_has) { ST(); STR_ARG(0, f); return mcs_bool(mcs_ble_has(BLE(), f)); }
NATIVE(ble_backend) { ST(); return mcs_string(vm, BLE() && BLE()->name ? BLE()->name : "none"); }
NATIVE(ble_getname) { ST(); return mcs_string(vm, s->name); }
NATIVE(ble_setname) {
    ST(); STR_ARG(0, n);
    if (strlen(n) > 29) { mcs_raise(vm, "ArgumentException", "Ble.Name: at most 29 characters"); return mcs_null(); }
    snprintf(s->name, sizeof s->name, "%s", n);
    if (s->on && BLE()->set_name) CALL("Ble.Name", BLE()->set_name(BLE()->ctx, s->name));
    return mcs_null();
}
NATIVE(ble_address) {
    ST(); ON("Ble.Address"); NEED(address, "Ble.Address");
    mcs_ble_addr_t a; memset(&a, 0, sizeof a);
    CALL("Ble.Address", BLE()->address(BLE()->ctx, &a));
    return addr_val(vm, &a);
}
NATIVE(ble_addrtype) {
    ST(); ON("Ble.AddressType"); NEED(address, "Ble.AddressType");
    mcs_ble_addr_t a; memset(&a, 0, sizeof a);
    CALL("Ble.AddressType", BLE()->address(BLE()->ctx, &a));
    return mcs_string(vm, addr_type_name(a.type));
}
static mcs_value_t opt_get(mcs_vm_t* vm, ble_state_t* s, int opt, const char* op) {
    if (!s->ble || !s->ble->opt_get) return fail(vm, op, MCS_HAL_ENOTSUP);
    if (!need_on(vm, s, op)) return mcs_null();
    int32_t v = 0;
    int rc = s->ble->opt_get(s->ble->ctx, opt, &v);
    if (rc < 0) return fail(vm, op, rc);
    return mcs_int(v);
}
static mcs_value_t opt_set(mcs_vm_t* vm, ble_state_t* s, int opt, mcs_value_t val, const char* op) {
    if (!s->ble || !s->ble->opt_set) return fail(vm, op, MCS_HAL_ENOTSUP);
    if (!need_on(vm, s, op)) return mcs_null();
    int32_t v = mcs_is_bool(val) ? mcs_truthy(val) : (int32_t)mcs_to_int(vm, val);
    CHECK();
    int rc = s->ble->opt_set(s->ble->ctx, opt, v);
    if (rc < 0) return fail(vm, op, rc);
    return mcs_null();
}
#define OPT_PROP(get, set, opt, name, isbool) \
    NATIVE(get) { ST(); mcs_value_t v = opt_get(vm, s, opt, name); return (isbool) && mcs_is_int(v) ? mcs_bool(mcs_to_int(vm, v) != 0) : v; } \
    NATIVE(set) { ST(); return opt_set(vm, s, opt, argv[0], name); }
OPT_PROP(ble_gettx, ble_settx, MCS_BLE_OPT_TX_POWER, "Ble.TxPower", 0)
OPT_PROP(ble_getmtu, ble_setmtu, MCS_BLE_OPT_MTU, "Ble.Mtu", 0)
OPT_PROP(ble_getapp, ble_setapp, MCS_BLE_OPT_APPEARANCE, "Ble.Appearance", 0)
OPT_PROP(ble_getio, ble_setio, MCS_BLE_OPT_IO_CAP, "Ble.IoCapability", 0)
OPT_PROP(ble_getbond, ble_setbond, MCS_BLE_OPT_BONDING, "Ble.Bonding", 1)
OPT_PROP(ble_getmitm, ble_setmitm, MCS_BLE_OPT_MITM, "Ble.Mitm", 1)
OPT_PROP(ble_getsc, ble_setsc, MCS_BLE_OPT_SC, "Ble.SecureConnections", 1)
OPT_PROP(ble_getphy, ble_setphy, MCS_BLE_OPT_PHY, "Ble.Phy", 0)
NATIVE(ble_maxconn) {
    ST();
    if (BLE() && BLE()->opt_get) {
        mcs_value_t v = opt_get(vm, s, MCS_BLE_OPT_MAX_CONN, "Ble.MaxConnections");
        if (mcs_is_int(v) && mcs_to_int(vm, v) < MCS_BLE_MAX_CONN) return v;
        if (mcs_has_exception(vm)) return mcs_null();
    }
    return mcs_int(MCS_BLE_MAX_CONN);
}
NATIVE(ble_getpasskey) { ST(); return mcs_int(s->passkey); }
NATIVE(ble_setpasskey) {
    ST(); INT_ARG(0, k);
    if (k < -1 || k > 999999) { mcs_raise(vm, "ArgumentOutOfRangeException", "Ble.Passkey: 0..999999 (-1 = random)"); return mcs_null(); }
    s->passkey = (int32_t)k;
    if (BLE() && BLE()->opt_set && s->on) BLE()->opt_set(BLE()->ctx, MCS_BLE_OPT_PASSKEY, (int32_t)k);
    return mcs_null();
}
NATIVE(ble_gettimeout) { ST(); return mcs_int((mcs_int_t)s->timeout); }
NATIVE(ble_settimeout) { ST(); INT_ARG(0, t); s->timeout = t < 1 ? 1 : (uint32_t)t; return mcs_null(); }
NATIVE(ble_getauto) { ST(); return mcs_bool(s->auto_adv); }
NATIVE(ble_setauto) { ST(); s->auto_adv = mcs_truthy(argv[0]); return mcs_null(); }
NATIVE(ble_dropped) { ST(); return mcs_int((mcs_int_t)g_dropped); }

/* ---- advertising */
static mcs_value_t ble_start_adv(mcs_vm_t* vm, ble_state_t* s, mcs_value_t arg) {
    if (!s->ble || !s->ble->adv_start) return fail(vm, "Ble.StartAdvertising", MCS_HAL_ENOTSUP);
    if (!need_on(vm, s, "Ble.StartAdvertising")) return mcs_null();
    badv_t def;
    const badv_t* v = mcs_is_null(arg) || mcs_is_string(arg) ? NULL : (const badv_t*)mcs_check_userdata(vm, arg, &adv_def);
    if (mcs_has_exception(vm)) return mcs_null();
    mcs_ble_adv_t out;
    if (!v) {                    /* default: flags, the services' UUIDs (as many as fit), the name */
        adv_defaults(&def);
        if (mcs_is_string(arg)) { snprintf(def.name, sizeof def.name, "%s", mcs_cstr(arg)); def.name_set = 1; }
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < MCS_BLE_MAX_SERVICES && def.nuuid < 4; i++) {
                if (!g_svc[i].used || !g_svc[i].added || !g_svc[i].primary) continue;
                mcs_ble_uuid_t u = g_svc[i].uuid; uuid_shrink(&u);
                if ((pass == 0) != (u.len == 2)) continue;
                def.uuids[def.nuuid++] = u;
                if (adv_build(s, &def, &out)) def.nuuid--;
                else {   /* keep the full name visible: drop UUIDs that push it out */
                    const uint8_t* p; size_t n;
                    bdev_t tmp; memset(&tmp, 0, sizeof tmp);
                    memcpy(tmp.adv, out.data, out.len); tmp.adv_len = out.len; memcpy(tmp.rsp, out.rsp, out.rsp_len); tmp.rsp_len = out.rsp_len;
                    if (s->name[0] && !ad_find(&tmp, 0x09, 0, &p, &n)) def.nuuid--;
                }
            }
        v = &def;
    }
    const char* e = adv_build(s, v, &out);
    if (e) { mcs_raise(vm, "ArgumentException", "Ble.StartAdvertising: %s", e); return mcs_null(); }
    int rc = publish(s);
    if (rc < 0) return fail(vm, "Ble.StartAdvertising (services)", rc);
    if (s->advertising && s->ble->adv_stop) s->ble->adv_stop(s->ble->ctx);
    s->advertising = 0;
    CALL("Ble.StartAdvertising", s->ble->adv_start(s->ble->ctx, &out));
    s->adv = out;
    s->advertising = s->want_adv = 1;
    return mcs_null();
}
NATIVE(ble_startadv) {   /* StartAdvertising([BleAdvertisement | name]) */
    ST();
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "Ble.StartAdvertising([advertisement or name])"); return mcs_null(); }
    return ble_start_adv(vm, s, argc ? argv[0] : mcs_null());
}
NATIVE(ble_stopadv) {
    ST();
    s->want_adv = 0;
    if (!s->advertising || !BLE() || !BLE()->adv_stop) { s->advertising = 0; return mcs_null(); }
    s->advertising = 0;
    CALL("Ble.StopAdvertising", BLE()->adv_stop(BLE()->ctx));
    return mcs_null();
}
NATIVE(ble_advertising) { ST(); drain(vm, s); return mcs_bool(s->advertising); }

/* ---- scanning */
static int scan_begin(mcs_vm_t* vm, ble_state_t* s, int active, int dup, uint32_t duration) {
    if (s->scanning && s->ble->scan_stop) s->ble->scan_stop(s->ble->ctx);
    s->scanning = 0;
    mcs_ble_scan_t p; memset(&p, 0, sizeof p);
    p.active = (uint8_t)active; p.filter_duplicates = (uint8_t)!dup; p.duration_ms = duration;
    int rc = s->ble->scan_start(s->ble->ctx, &p);
    if (rc >= 0) s->scanning = 1;
    (void)vm;
    return rc;
}
static int dev_cmp_rssi(mcs_value_t a, mcs_value_t b) { return ((bdev_t*)mcs_userdata(b))->rssi - ((bdev_t*)mcs_userdata(a))->rssi; }
NATIVE(ble_scan) {       /* Scan([durationMs = 3000[, active = true]]) -> List<BleDevice>, strongest first */
    ST(); NEED(scan_start, "Ble.Scan"); ON("Ble.Scan");
    OPT_INT(0, ms, 3000);
    int active = argc > 1 ? mcs_truthy(argv[1]) : 1;
    if (ms < 10) ms = 10;
    drain(vm, s);
    if (mcs_has_exception(vm)) return mcs_null();
    mcs_value_t list = mcs_new_list(vm);
    mcs_push_root(vm, list);
    s->found = list; s->collecting = 1;
    int rc = scan_begin(vm, s, active, 1, 0);
    if (rc < 0) { s->collecting = 0; s->found = mcs_null(); mcs_pop_root(vm, 1); return fail(vm, "Ble.Scan", rc); }
    uint32_t t0 = mcs_ticks(vm);
    while ((uint32_t)(mcs_ticks(vm) - t0) < (uint32_t)ms) {
        drain(vm, s);
        if (mcs_has_exception(vm) || !ble_wait(vm, 10)) break;
    }
    if (s->ble->scan_stop) s->ble->scan_stop(s->ble->ctx);
    s->scanning = 0;
    if (!mcs_has_exception(vm)) drain(vm, s);
    s->collecting = 0; s->found = mcs_null();
    mcs_pop_root(vm, 1);
    if (mcs_has_exception(vm)) return mcs_null();
    /* strongest first (insertion sort: a few dozen devices) */
    uint32_t n = mcs_len(list);
    for (uint32_t i = 1; i < n; i++) {
        mcs_value_t x = mcs_index(list, i);
        uint32_t j = i;
        while (j > 0 && dev_cmp_rssi(mcs_index(list, j - 1), x) > 0) { mcs_set_index(list, j, mcs_index(list, j - 1)); j--; }
        mcs_set_index(list, j, x);
    }
    return list;
}
NATIVE(ble_startscan) {  /* StartScan(device => ...[, active = true[, duplicates = false]]) */
    ST(); NEED(scan_start, "Ble.StartScan"); ON("Ble.StartScan");
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "Ble.StartScan(callback[, active[, duplicates]])"); return mcs_null(); }
    reg_set(vm, s, R_SCAN, argv[0]);
    CALL("Ble.StartScan", scan_begin(vm, s, argc > 1 ? mcs_truthy(argv[1]) : 1, argc > 2 && mcs_truthy(argv[2]), 0));
    return mcs_null();
}
NATIVE(ble_stopscan) {
    ST();
    if (s->scanning && BLE() && BLE()->scan_stop) BLE()->scan_stop(BLE()->ctx);
    s->scanning = 0;
    reg_set(vm, s, R_SCAN, mcs_null());
    return mcs_null();
}
NATIVE(ble_scanning) { ST(); drain(vm, s); return mcs_bool(s->scanning); }

/* ---- GATT server */
NATIVE(ble_addservice) {
    ST();
    bsvc_t* sv = (bsvc_t*)mcs_check_userdata(vm, argv[0], &svc_def);
    if (!sv || !svc_ok(vm, sv)) return mcs_null();
    if (!BLE() || !BLE()->gatts_set) return fail(vm, "Ble.AddService", MCS_HAL_ENOTSUP);
    if (g_svc[sv->slot].added) return mcs_null();
    if (!db_changeable(vm, s, "Ble.AddService")) return mcs_null();
    g_svc[sv->slot].added = 1;
    s->db_dirty = 1;
    return mcs_null();
}
static void clear_services(mcs_vm_t* vm, ble_state_t* s) {
    for (int i = 0; i < MCS_BLE_MAX_ATTRS; i++) {
        if (g_attr[i].kind != A_FREE) {
            mcs_value_t o = reg_get(vm, s, R_ATTR + i);
            battr_t* b = (battr_t*)mcs_userdata(o);
            if (b) b->id = -1;
        }
        g_attr[i].kind = A_FREE;
        reg_set(vm, s, R_ATTR + i, mcs_null()); reg_set(vm, s, R_WRITE + i, mcs_null()); reg_set(vm, s, R_SUBCB + i, mcs_null());
    }
    for (int i = 0; i < MCS_BLE_MAX_SERVICES; i++) {
        if (g_svc[i].used) {
            bsvc_t* b = (bsvc_t*)mcs_userdata(reg_get(vm, s, R_SVC + i));
            if (b) b->slot = -1;
        }
        g_svc[i].used = 0;
        reg_set(vm, s, R_SVC + i, mcs_null());
    }
    mcs_value_t uo = reg_get(vm, s, R_UART);
    buart_t* u = mcs_is_null(uo) ? NULL : (buart_t*)mcs_userdata(uo);
    if (u) u->rx = u->tx = -1;
    reg_set(vm, s, R_UART, mcs_null());
    g_pool_used = 0;
    s->db_dirty = 1;
}
NATIVE(ble_clearservices) {
    ST();
    if (!db_changeable(vm, s, "Ble.ClearServices")) return mcs_null();
    clear_services(vm, s);
    if (s->on) { int rc = publish(s); if (rc < 0 && rc != MCS_HAL_ENOTSUP) return fail(vm, "Ble.ClearServices", rc); }
    return mcs_null();
}
NATIVE(ble_services) {
    ST();
    mcs_value_t l = mcs_new_list(vm);
    mcs_push_root(vm, l);
    for (int i = 0; i < MCS_BLE_MAX_SERVICES; i++) if (g_svc[i].used && g_svc[i].added) mcs_list_add(vm, l, reg_get(vm, s, R_SVC + i));
    mcs_pop_root(vm, 1);
    return l;
}

/* ---- connections */
static mcs_value_t ble_connect_to(mcs_vm_t* vm, ble_state_t* s, const mcs_ble_addr_t* a, uint32_t timeout) {
    if (!s->ble || !s->ble->connect) return fail(vm, "Ble.Connect", MCS_HAL_ENOTSUP);
    if (!need_on(vm, s, "Ble.Connect")) return mcs_null();
    if (nconn() >= MCS_BLE_MAX_CONN) { mcs_raise(vm, "InvalidOperationException", "Ble.Connect: all %d connections in use (MCS_BLE_MAX_CONN)", MCS_BLE_MAX_CONN); return mcs_null(); }
    bool resume = s->scanning;   /* most controllers cannot scan while they initiate */
    if (resume && s->ble->scan_stop) s->ble->scan_stop(s->ble->ctx);
    op_begin(OP_CONNECT, -1);
    int rc = op_wait(vm, s, s->ble->connect(s->ble->ctx, a, timeout), timeout + 500);
    if (rc == MCS_HAL_ETIMEOUT && s->ble->connect_cancel) s->ble->connect_cancel(s->ble->ctx);
    if (resume) s->scanning = 0;
    if (mcs_has_exception(vm)) return mcs_null();
    if (rc < 0) {
        char b[18]; addr_fmt(a, b);
        if (rc == MCS_HAL_ETIMEOUT) { mcs_raise(vm, "TimeoutException", "Ble.Connect: %s did not answer", b); return mcs_null(); }
        return fail(vm, "Ble.Connect", rc);
    }
    mcs_value_t o = conn_add(vm, s, (int)g_op.value, a, MCS_BLE_ROLE_CENTRAL);
    if (mcs_is_null(o)) { mcs_raise(vm, "InvalidOperationException", "Ble.Connect: no free connection slot"); return o; }
    mcs_push_root(vm, o);
    drain(vm, s);                /* the Connect event, MTU ... */
    mcs_pop_root(vm, 1);
    if (mcs_has_exception(vm)) return mcs_null();
    return o;
}
NATIVE(ble_connect) {    /* Connect(address | BleDevice[, timeoutMs[, addressType]]) */
    ST();
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "Ble.Connect(address[, timeoutMs[, addressType]])"); return mcs_null(); }
    mcs_ble_addr_t a; memset(&a, 0, sizeof a);
    bdev_t* d = (bdev_t*)mcs_userdata_of(argv[0], &dev_def);
    if (d) a = d->addr;
    else {
        STR_ARG(0, str);
        if (!addr_parse(str, &a)) { mcs_raise(vm, "ArgumentException", "'%s' is not a Bluetooth address (C0:11:22:33:44:55)", str); return mcs_null(); }
        a.type = MCS_BLE_ADDR_RANDOM;     /* most peripherals use a static random address */
        if (argc > 2 && !mcs_is_null(argv[2])) {
            if (mcs_is_string(argv[2])) a.type = !strcmp(mcs_cstr(argv[2]), "public") ? MCS_BLE_ADDR_PUBLIC : MCS_BLE_ADDR_RANDOM;
            else { INT_ARG(2, t); a.type = (uint8_t)t; }
        }
    }
    OPT_INT(1, t, MCS_BLE_CONNECT_TIMEOUT);
    return ble_connect_to(vm, s, &a, (uint32_t)(t < 1 ? 1 : t));
}
NATIVE(ble_connections) {
    ST(); drain(vm, s);
    CHECK();
    mcs_value_t l = mcs_new_list(vm);
    mcs_push_root(vm, l);
    for (int i = 0; i < MCS_BLE_MAX_CONN; i++) if (g_conn[i].used) mcs_list_add(vm, l, reg_get(vm, s, R_CONN + i));
    mcs_pop_root(vm, 1);
    return l;
}
NATIVE(ble_connected) { ST(); drain(vm, s); return mcs_bool(nconn() > 0); }
#define CB_SETTER(fn, slot) NATIVE(fn) { ST(); reg_set(vm, s, slot, argv[0]); return mcs_null(); }
CB_SETTER(ble_onconnect, R_CONNECT)
CB_SETTER(ble_ondisconnect, R_DISCONNECT)
CB_SETTER(ble_onevent, R_EVENT)
CB_SETTER(ble_onpasskey, R_PASSKEY)
NATIVE(ble_poll) { ST(); int r = drain(vm, s); return mcs_int(r < 0 ? 0 : r); }
NATIVE(ble_eventname) { INT_ARG(0, ev); return mcs_string(vm, mcs_ble_event_name((int)ev)); }

/* ---- bonds */
NATIVE(ble_bonds) {
    ST(); NEED(bonds, "Ble.Bonds"); ON("Ble.Bonds");
    mcs_ble_addr_t a[16];
    int n = BLE()->bonds(BLE()->ctx, MCS_BLE_BONDS_LIST, a, 16);
    if (n < 0) return fail(vm, "Ble.Bonds", n);
    mcs_value_t l = mcs_new_list(vm);
    mcs_push_root(vm, l);
    for (int i = 0; i < n && i < 16; i++) { mcs_value_t v = addr_val(vm, &a[i]); mcs_list_add(vm, l, v); }
    mcs_pop_root(vm, 1);
    return l;
}
NATIVE(ble_removebond) {
    ST(); NEED(bonds, "Ble.RemoveBond"); ON("Ble.RemoveBond"); STR_ARG(0, str);
    mcs_ble_addr_t a; memset(&a, 0, sizeof a);
    if (!addr_parse(str, &a)) { mcs_raise(vm, "ArgumentException", "'%s' is not a Bluetooth address", str); return mcs_null(); }
    int rc = BLE()->bonds(BLE()->ctx, MCS_BLE_BONDS_DELETE, &a, 1);
    if (rc == MCS_HAL_ENODEV) return mcs_bool(false);
    if (rc < 0) return fail(vm, "Ble.RemoveBond", rc);
    return mcs_bool(true);
}
NATIVE(ble_clearbonds) { ST(); NEED(bonds, "Ble.ClearBonds"); ON("Ble.ClearBonds"); CALL("Ble.ClearBonds", BLE()->bonds(BLE()->ctx, MCS_BLE_BONDS_CLEAR, NULL, 0)); return mcs_null(); }

static const mcs_reg_t ble_fns[] = {
    MCS_FN("Enable", ble_enable, 0), MCS_FN("Disable", ble_disable, 0), MCS_GET("Enabled", ble_enabled),
    MCS_FN("Has", ble_has, 1), MCS_GET("Backend", ble_backend), MCS_GET("Name", ble_getname), MCS_SET("Name", ble_setname),
    MCS_GET("Address", ble_address), MCS_GET("AddressType", ble_addrtype),
    MCS_GET("TxPower", ble_gettx), MCS_SET("TxPower", ble_settx), MCS_GET("Mtu", ble_getmtu), MCS_SET("Mtu", ble_setmtu),
    MCS_GET("Appearance", ble_getapp), MCS_SET("Appearance", ble_setapp), MCS_GET("IoCapability", ble_getio), MCS_SET("IoCapability", ble_setio),
    MCS_GET("Bonding", ble_getbond), MCS_SET("Bonding", ble_setbond), MCS_GET("Mitm", ble_getmitm), MCS_SET("Mitm", ble_setmitm),
    MCS_GET("SecureConnections", ble_getsc), MCS_SET("SecureConnections", ble_setsc), MCS_GET("Phy", ble_getphy), MCS_SET("Phy", ble_setphy),
    MCS_GET("Passkey", ble_getpasskey), MCS_SET("Passkey", ble_setpasskey), MCS_GET("MaxConnections", ble_maxconn),
    MCS_GET("Timeout", ble_gettimeout), MCS_SET("Timeout", ble_settimeout), MCS_GET("AutoAdvertise", ble_getauto), MCS_SET("AutoAdvertise", ble_setauto),
    MCS_GET("DroppedEvents", ble_dropped),
    MCS_FN("StartAdvertising", ble_startadv, -1), MCS_FN("StopAdvertising", ble_stopadv, 0), MCS_GET("Advertising", ble_advertising),
    MCS_FN("Scan", ble_scan, -1), MCS_FN("StartScan", ble_startscan, -1), MCS_FN("StopScan", ble_stopscan, 0), MCS_GET("Scanning", ble_scanning),
    MCS_FN("AddService", ble_addservice, 1), MCS_FN("ClearServices", ble_clearservices, 0), MCS_GET("Services", ble_services),
    MCS_FN("Connect", ble_connect, -1), MCS_GET("Connections", ble_connections), MCS_GET("Connected", ble_connected),
    MCS_FN("OnConnect", ble_onconnect, 1), MCS_FN("OnDisconnect", ble_ondisconnect, 1), MCS_FN("OnEvent", ble_onevent, 1),
    MCS_FN("OnPasskey", ble_onpasskey, 1), MCS_FN("Poll", ble_poll, 0), MCS_FN("EventName", ble_eventname, 1),
    MCS_GET("Bonds", ble_bonds), MCS_FN("RemoveBond", ble_removebond, 1), MCS_FN("ClearBonds", ble_clearbonds, 0), MCS_REG_END };
static const mcs_const_t ble_consts[] = {
    MCS_CONST("ApiVersion", MCS_BLE_API_VERSION),
    MCS_CONST("EventConnect", MCS_BLE_EV_CONNECT), MCS_CONST("EventDisconnect", MCS_BLE_EV_DISCONNECT),
    MCS_CONST("EventConnectFailed", MCS_BLE_EV_CONNECT_FAILED), MCS_CONST("EventAdvertisingDone", MCS_BLE_EV_ADV_DONE),
    MCS_CONST("EventScanDone", MCS_BLE_EV_SCAN_DONE), MCS_CONST("EventMtu", MCS_BLE_EV_MTU), MCS_CONST("EventSubscribe", MCS_BLE_EV_SUBSCRIBE),
    MCS_CONST("EventEncrypted", MCS_BLE_EV_ENCRYPTED), MCS_CONST("EventPairingDone", MCS_BLE_EV_PAIRING_DONE),
    MCS_CONST("EventConnectionUpdate", MCS_BLE_EV_CONN_UPDATE), MCS_CONST("EventIndicateDone", MCS_BLE_EV_INDICATE_DONE),
    MCS_CONST("EventPhy", MCS_BLE_EV_PHY),
    MCS_CONST("IoDisplayOnly", MCS_BLE_IO_DISPLAY_ONLY), MCS_CONST("IoDisplayYesNo", MCS_BLE_IO_DISPLAY_YESNO),
    MCS_CONST("IoKeyboardOnly", MCS_BLE_IO_KEYBOARD_ONLY), MCS_CONST("IoNone", MCS_BLE_IO_NONE),
    MCS_CONST("IoKeyboardDisplay", MCS_BLE_IO_KEYBOARD_DISPLAY),
    MCS_CONST("PasskeyDisplay", MCS_BLE_PASSKEY_DISPLAY), MCS_CONST("PasskeyEntry", MCS_BLE_PASSKEY_ENTRY),
    MCS_CONST("PasskeyCompare", MCS_BLE_PASSKEY_COMPARE), MCS_CONST("PasskeyConfirm", MCS_BLE_PASSKEY_CONFIRM),
    MCS_CONST("SubscribeNotify", MCS_BLE_SUB_NOTIFY), MCS_CONST("SubscribeIndicate", MCS_BLE_SUB_INDICATE),
    MCS_CONST("Phy1M", 1), MCS_CONST("Phy2M", 2), MCS_CONST("PhyCoded", 4), MCS_CONST_END };
static const mcs_const_t prop_consts[] = {
    MCS_CONST("Broadcast", MCS_BLE_PROP_BROADCAST), MCS_CONST("Read", MCS_BLE_PROP_READ),
    MCS_CONST("WriteWithoutResponse", MCS_BLE_PROP_WRITE_NR), MCS_CONST("Write", MCS_BLE_PROP_WRITE),
    MCS_CONST("Notify", MCS_BLE_PROP_NOTIFY), MCS_CONST("Indicate", MCS_BLE_PROP_INDICATE),
    MCS_CONST("AuthenticatedSignedWrites", MCS_BLE_PROP_SIGNED_WRITE), MCS_CONST("ExtendedProperties", MCS_BLE_PROP_EXTENDED),
    MCS_CONST("Encrypted", MCS_BLE_PERM_ENCRYPTED), MCS_CONST("Authenticated", MCS_BLE_PERM_AUTHENTICATED), MCS_CONST_END };

/* ============================================================ BleUuid */
NATIVE(uuid_normalize) { mcs_ble_uuid_t u; if (!uuid_arg(vm, argv[0], &u)) return mcs_null(); return uuid_val(vm, &u); }
NATIVE(uuid_long) {
    mcs_ble_uuid_t u; if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    uint8_t f[16]; uuid_full(&u, f);
    char s[37]; int k = 0;
    for (int i = 15; i >= 0; i--) { k += snprintf(s + k, sizeof s - (size_t)k, "%02X", f[i]); if (i == 12 || i == 10 || i == 8 || i == 6) s[k++] = '-'; }
    s[k] = 0;
    return mcs_string(vm, s);
}
NATIVE(uuid_equals) {
    mcs_ble_uuid_t a, b;
    if (!uuid_arg(vm, argv[0], &a) || !uuid_arg(vm, argv[1], &b)) return mcs_null();
    return mcs_bool(mcs_ble_uuid_eq(&a, &b));
}
NATIVE(uuid_isvalid) {
    if (!mcs_is_string(argv[0])) return mcs_bool(mcs_is_int(argv[0]) && mcs_to_int(vm, argv[0]) >= 0 && mcs_to_int(vm, argv[0]) <= 0xFFFF);
    mcs_ble_uuid_t u; return mcs_bool(mcs_ble_uuid_parse(mcs_cstr(argv[0]), &u));
}
NATIVE(uuid_name_n) {
    mcs_ble_uuid_t u; if (!uuid_arg(vm, argv[0], &u)) return mcs_null();
    const char* n = uuid_name(&u);
    return n ? mcs_string(vm, n) : mcs_null();
}
static const mcs_reg_t uuid_fns[] = {
    MCS_FN("Normalize", uuid_normalize, 1), MCS_FN("ToLong", uuid_long, 1), MCS_FN("Equals", uuid_equals, 2),
    MCS_FN("IsValid", uuid_isvalid, 1), MCS_FN("Name", uuid_name_n, 1), MCS_REG_END };

/* ============================================================ Has + open */
bool mcs_ble_has(const mcs_ble_t* b, const char* f) {
    if (!f || !b || !b->enable) return false;
    if (!strcmp(f, "Ble") || !strcmp(f, "Bluetooth")) return true;
    if (!strcmp(f, "Ble.Advertise")) return b->adv_start != NULL;
    if (!strcmp(f, "Ble.Scan")) return b->scan_start != NULL;
    if (!strcmp(f, "Ble.Server") || !strcmp(f, "BleUart")) return b->gatts_set != NULL && b->adv_start != NULL;
    if (!strcmp(f, "Ble.Central") || !strcmp(f, "Ble.Client")) return b->connect != NULL && b->discover != NULL;
    if (!strcmp(f, "Ble.Security")) return b->pair != NULL;
    if (!strcmp(f, "Ble.Bonds")) return b->bonds != NULL;
    return false;
}

void mcs_ble_open_lib(mcs_vm_t* vm, const mcs_ble_t* ble) {
    ble_state_t* s = bstate(vm);
    if (!s) {
        mcs_register_class(vm, &state_def);
        mcs_value_t obj;
        if (mcs_new_object(vm, "__BleState", 0, NULL, &obj) != MCS_OK) return;
        s = (ble_state_t*)mcs_userdata(obj);
        memset(s, 0, sizeof *s);
        s->self_pin = mcs_pin(vm, obj);
        mcs_value_t list = mcs_new_list(vm);
        mcs_push_root(vm, list);
        for (int i = 0; i < R__COUNT; i++) mcs_list_add(vm, list, mcs_null());
        s->reg_pin = mcs_pin(vm, list);
        mcs_pop_root(vm, 1);
        if (s->self_pin < 0 || s->reg_pin < 0) {
            if (s->self_pin >= 0) mcs_unpin(vm, s->self_pin);
            if (s->reg_pin >= 0) mcs_unpin(vm, s->reg_pin);
            mcs_fail(vm, MCS_ERR_MEMORY, "Bluetooth: no free pin handles (MCS_MAX_PINS)");
            return;
        }
        s->timeout = MCS_BLE_TIMEOUT;
        s->auto_adv = 1;
        s->passkey = -1;
        s->found = mcs_null();
        snprintf(s->name, sizeof s->name, "%s", MCS_BLE_NAME);
        /* a new VM starts from a clean table (the previous one's objects are gone) */
        memset(g_attr, 0, sizeof g_attr); memset(g_svc, 0, sizeof g_svc);
        memset(g_conn, 0, sizeof g_conn); memset(g_csub, 0, sizeof g_csub);
        g_pool_used = 0; g_op.kind = OP_NONE;
        s->db_dirty = 1;
        q_clear();
        mcs_set_ext(vm, MCS_EXT_BLE, s);
    }
    s->ble = ble;
    if (!mcs_ble_has(ble, "Ble")) return;
    if (ble->enable && ble->enable(ble->ctx, 0) >= 0) { /* start from a radio that is off */ }
    s->on = 0;
    mcs_register_module(vm, "Ble", ble_fns);
    mcs_register_consts(vm, "Ble", ble_consts);
    mcs_register_consts(vm, "BleProperty", prop_consts);
    mcs_register_module(vm, "BleUuid", uuid_fns);
    mcs_register_class(vm, &dev_def);
    mcs_register_class(vm, &adv_def);
    mcs_register_class(vm, &conn_def);
    mcs_register_class(vm, &svc_def);
    mcs_register_class(vm, &chr_def);
    mcs_register_class(vm, &dsc_def);
    mcs_register_class(vm, &rsvc_def);
    mcs_register_class(vm, &rchr_def);
    mcs_register_class(vm, &rdsc_def);
    mcs_register_class(vm, &uart_def);
#if MCS_ENABLE_HAL
    mcs_hal_set_event_handler(MCS_HAL_EV_BLE, ble_event);
#endif
}
#endif
