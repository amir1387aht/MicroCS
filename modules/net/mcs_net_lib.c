/*
 * MicroCS - C# Wi-Fi and networking front end (MCS_ENABLE_NET, include/mcs_net.h, docs/NETWORK.md).
 *
 *   Wifi.Connect("MyNet", "secret");                    station: blocks until an IP address (or false)
 *   Wifi.StartAp("MicroCS-Setup", "12345678");          soft access point (192.168.4.1)
 *   foreach (var n in Wifi.Scan()) Console.WriteLine(n);
 *   Wifi.OnEvent((ev, value) => { if (ev == Wifi.EventGotIp) Console.WriteLine(Wifi.Ip); });
 *   EspNow.Start(); EspNow.Send(null, "hello");         broadcast to every ESP-NOW listener
 *   Ntp.Sync(); Ntp.Timezone = "CET-1CEST,M3.5.0,M10.5.0/3"; Console.WriteLine(Ntp.LocalTime);
 *
 * The radio is the port's mcs_net_t table; sockets, DNS and HTTP live in mcs_net_sock.c.
 */
#include "mcs_net_internal.h"
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#if MCS_ENABLE_NET
#include <time.h>
#include <stdlib.h>

static const mcs_net_t* g_net;
void mcs_net_set(const mcs_net_t* net) { g_net = net; }
const mcs_net_t* mcs_net_get(void) { return g_net; }

/* ------------------------------------------------------------ events */
static bool (*g_poster)(int type, int source, int32_t value);
void mcs_net_set_poster(bool (*post)(int type, int source, int32_t value)) { g_poster = post; }
bool mcs_net_post(int ev, int32_t value) {
    if (g_poster) return g_poster(MCS_HAL_EV_NET, ev, value);
#if MCS_ENABLE_HAL
    return mcs_hal_post(MCS_HAL_EV_NET, ev, value);
#else
    (void)ev; (void)value;
    return false;
#endif
}

/* ------------------------------------------------------------ names */
static const char* const g_auth_names[MCS_WIFI_AUTH__COUNT] = {
    "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", "ENTERPRISE", "WPA3_PSK",
    "WPA2_WPA3_PSK", "WAPI_PSK", "OWE", "WPA3_ENT_192" };
const char* mcs_wifi_auth_name(int a) { return a >= 0 && a < MCS_WIFI_AUTH__COUNT ? g_auth_names[a] : "UNKNOWN"; }

const char* mcs_wifi_reason_name(int r) {
    switch (r) {
    case 0: return "NONE";
    case 1: return "UNSPECIFIED";
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "ASSOC_EXPIRE";
    case 5: return "ASSOC_TOOMANY";
    case 6: return "NOT_AUTHED";
    case 7: return "NOT_ASSOCED";
    case 8: return "ASSOC_LEAVE";
    case 9: return "ASSOC_NOT_AUTHED";
    case 13: return "IE_INVALID";
    case 14: return "MIC_FAILURE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
    case 23: return "802_1X_AUTH_FAILED";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    case 206: return "AP_TSF_RESET";
    case 207: return "ROAMING";
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    default: return "OTHER";
    }
}
static const char* status_name(int st) {
    static const char* const n[] = { "Idle", "NoSsid", "ScanDone", "Connected", "ConnectFailed",
        "ConnectionLost", "Disconnected", "Connecting", "WrongPassword", "Stopped" };
    return st >= 0 && st < (int)(sizeof n / sizeof n[0]) ? n[st] : "Unknown";
}
static const char* event_name(int ev) {
    static const char* const n[MCS_NET_EV__COUNT] = { "None", "StaStart", "StaStop", "Connected",
        "Disconnected", "GotIp", "LostIp", "GotIp6", "ScanDone", "ApStart", "ApStop", "ApJoined",
        "ApLeft", "ApAssigned", "EspNowRecv", "EspNowSent", "NtpSync" };
    return ev > 0 && ev < MCS_NET_EV__COUNT ? n[ev] : "Unknown";
}

/* ------------------------------------------------------------ helpers */
mcs_value_t mcs__net_fail(mcs_vm_t* vm, const char* op, int rc) {
    const char* why = rc == MCS_HAL_ENOTSUP ? "not supported" : rc == MCS_HAL_ETIMEOUT ? "timeout"
                    : rc == MCS_HAL_ENODEV ? "no device / not found" : rc == MCS_HAL_EBUSY ? "busy"
                    : rc == MCS_HAL_EINVAL ? "invalid argument" : "failed";
    if (rc == MCS_HAL_ETIMEOUT) mcs_raise(vm, "TimeoutException", "%s: %s", op, why);
    else if (rc == MCS_HAL_ENOTSUP) mcs_raise(vm, "NotSupportedException", "%s: %s", op, why);
    else if (rc == MCS_HAL_EINVAL) mcs_raise(vm, "ArgumentException", "%s: %s", op, why);
    else mcs_raise(vm, "IOException", "%s: %s (%d)", op, why, rc);
    return mcs_null();
}
bool mcs__ip_parse(const char* s, uint8_t ip[4]) {
    if (!s) return false;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return false;
        unsigned v = 0; int d = 0;
        while (*s >= '0' && *s <= '9' && d < 4) { v = v * 10u + (unsigned)(*s++ - '0'); d++; }
        if (v > 255 || d > 3) return false;
        ip[i] = (uint8_t)v;
        if (i < 3 && *s++ != '.') return false;
    }
    return *s == 0;
}
void mcs__ip_fmt(const uint8_t ip[4], char out[16]) { snprintf(out, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]); }
static mcs_value_t ip_val(mcs_vm_t* vm, const uint8_t ip[4]) { char b[16]; mcs__ip_fmt(ip, b); return mcs_string(vm, b); }
static bool mac_parse(const char* s, uint8_t mac[6]) {
    if (!s) return false;
    for (int i = 0; i < 6; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = *s++;
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        mac[i] = (uint8_t)v;
        if (i < 5 && (*s == ':' || *s == '-')) s++;
    }
    return *s == 0;
}
static mcs_value_t mac_val(mcs_vm_t* vm, const uint8_t m[6]) {
    char b[18];
    snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return mcs_string(vm, b);
}
static bool mac_arg(mcs_vm_t* vm, mcs_value_t v, uint8_t mac[6]) {
    const char* s = mcs_to_cstr(vm, v);
    if (mcs_has_exception(vm)) return false;
    if (!mac_parse(s, mac)) { mcs_raise(vm, "ArgumentException", "'%s' is not a MAC address (AA:BB:CC:DD:EE:FF)", s); return false; }
    return true;
}
static bool ip_arg(mcs_vm_t* vm, mcs_value_t v, uint8_t ip[4], const char* what) {
    const char* s = mcs_to_cstr(vm, v);
    if (mcs_has_exception(vm)) return false;
    if (!mcs__ip_parse(s, ip)) { mcs_raise(vm, "ArgumentException", "%s '%s' is not an IPv4 address", what, s); return false; }
    return true;
}
/* copy a string argument into a fixed buffer; ArgumentException when it is too long */
static bool str_copy(mcs_vm_t* vm, mcs_value_t v, char* out, size_t cap, const char* what) {
    const char* s = mcs_to_cstr(vm, v);
    if (mcs_has_exception(vm)) return false;
    size_t n = strlen(s);
    if (n >= cap) { mcs_raise(vm, "ArgumentException", "%s is too long (at most %d characters)", what, (int)cap - 1); return false; }
    memcpy(out, s, n + 1);
    return true;
}
bool mcs__bytes_arg(mcs_vm_t* vm, mcs_value_t v, const uint8_t** p, size_t* n, uint8_t** heap) {
    *heap = NULL;
    if (mcs_is_string(v)) { *p = (const uint8_t*)mcs_cstr(v); *n = mcs_strlen(v); return true; }
    int k = mcs_obj_kind(v);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "expected a string or byte[]"); return false; }
    size_t len = mcs_len(v);
    uint8_t* b = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, len ? len : 1);
    if (!b) return false;
    for (size_t i = 0; i < len; i++) {
        mcs_int_t x = mcs_to_int(vm, mcs_index(v, (uint32_t)i));
        if (mcs_has_exception(vm)) { mcs_mem_realloc(vm, b, len ? len : 1, 0); return false; }
        b[i] = (uint8_t)x;
    }
    *p = b; *n = len; *heap = b;
    return true;
}
void mcs__bytes_free(mcs_vm_t* vm, uint8_t* heap, size_t n) { if (heap) mcs_mem_realloc(vm, heap, n ? n : 1, 0); }
mcs_value_t mcs__bytes_new(mcs_vm_t* vm, const uint8_t* p, size_t n) {
    mcs_value_t a = mcs_new_array(vm, (uint32_t)n);
    for (size_t i = 0; i < n; i++) mcs_set_index(a, (uint32_t)i, mcs_int(p[i]));
    return a;
}
bool mcs__net_wait(mcs_vm_t* vm, uint32_t ms) {
    mcs_sleep(vm, ms);
    return !mcs_has_exception(vm) && mcs_safepoint(vm) == 0;
}

/* ------------------------------------------------------------ state */
static const mcs_class_def_t state_def = { "__NetState", sizeof(mcs__net_state_t), NULL, NULL, NULL, NULL };
mcs__net_state_t* mcs__net_state(mcs_vm_t* vm) { return (mcs__net_state_t*)mcs_get_ext(vm, MCS_EXT_NET); }
int mcs__net_yield(mcs_vm_t* vm) {
    void* ud;
    mcs_idle_fn idle = mcs_get_idle(vm, &ud);
    if (idle) idle(vm, ud);
    return mcs_has_exception(vm) || mcs_safepoint(vm);
}
void mcs__net_untrack(mcs__net_resource_t* r) {
    if (!r->vm) return;
    mcs__net_state_t* s = mcs__net_state(r->vm);
    if (s) {
        mcs__net_resource_t** p = &s->resources;
        while (*p && *p != r) p = &(*p)->next;
        if (*p) *p = r->next;
    }
    r->vm = NULL; r->next = NULL;
}
void mcs__net_track(mcs_vm_t* vm, mcs__net_resource_t* r, void* data, void (*close)(mcs_vm_t*, void*)) {
    mcs__net_untrack(r);
    mcs__net_state_t* s = mcs__net_state(vm);
    if (!s) return;
    r->vm = vm; r->data = data; r->close = close;
    r->generation = ++s->resource_generation;
    r->next = s->resources; s->resources = r;
}
uint32_t mcs_net_scope_begin(mcs_vm_t* vm) {
    mcs__net_state_t* s = mcs__net_state(vm);
    return s ? s->resource_generation : 0;
}
void mcs_net_scope_end(mcs_vm_t* vm, uint32_t checkpoint) {
    mcs__net_state_t* s = mcs__net_state(vm);
    if (!s) return;
    for (;;) {
        mcs__net_resource_t* r = s->resources;
        while (r && (int32_t)(r->generation - checkpoint) <= 0) r = r->next;
        if (!r) break;
        void* data = r->data; void (*close)(mcs_vm_t*, void*) = r->close;
        mcs__net_untrack(r);   /* the callback may also remove other resources */
        close(vm, data);       /* no script callbacks / graceful network handshakes on abort */
    }
}
#define ST() mcs__net_state_t* s = mcs__net_state(vm); \
    if (!s) { mcs_raise(vm, "InvalidOperationException", "network library not opened"); return mcs_null(); }
#define NET() (s->net)
#define NEED(fn, op) if (!NET() || !NET()->fn) return mcs__net_fail(vm, op, MCS_HAL_ENOTSUP)
#define CALL(op, expr) do { int rc_ = (expr); if (rc_ < 0) return mcs__net_fail(vm, op, rc_); } while (0)

#if MCS_ENABLE_HAL   /* callbacks run from the HAL event queue */
static mcs_value_t cb_get(mcs_vm_t* vm, mcs__net_state_t* s, int i) { return mcs_index(mcs_pinned(vm, s->cb_pin), (uint32_t)i); }
static int call_cb(mcs_vm_t* vm, mcs_value_t fn, const mcs_value_t* args, int full) {
    int a = mcs_arity(fn);
    int argc = a < 0 || a >= full ? full : a;
    mcs_result_t r = mcs_call_value(vm, fn, argc, args, NULL);
    return r == MCS_OK ? 1 : -(int)r;
}
#endif

/* ============================================================ Wifi */
typedef struct { mcs_wifi_ap_info_t i; } wnet_t;
typedef struct { mcs_wifi_client_t c; } wcli_t;
static const mcs_class_def_t wnet_def, wcli_def;
static void plain_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argv;
    if (argc) mcs_raise(vm, "InvalidOperationException", "created by the network library");
}
#define WN() wnet_t* w = (wnet_t*)mcs_check_userdata(vm, self, &wnet_def); if (!w) return mcs_null()
NATIVE(wn_ssid) { WN(); return mcs_string(vm, w->i.ssid); }
NATIVE(wn_bssid) { WN(); return mac_val(vm, w->i.bssid); }
NATIVE(wn_rssi) { WN(); return mcs_int(w->i.rssi); }
NATIVE(wn_channel) { WN(); return mcs_int(w->i.channel); }
NATIVE(wn_auth) { WN(); return mcs_int(w->i.auth); }
NATIVE(wn_authname) { WN(); return mcs_string(vm, mcs_wifi_auth_name(w->i.auth)); }
NATIVE(wn_secure) { WN(); return mcs_bool(w->i.auth != MCS_WIFI_AUTH_OPEN); }
NATIVE(wn_hidden) { WN(); return mcs_bool(w->i.ssid[0] == 0); }
NATIVE(wn_protocols) { WN(); return mcs_int(w->i.protocols); }
NATIVE(wn_quality) {      /* 0..100 % from dBm, as phones show it */
    WN(); int q = 2 * (w->i.rssi + 100);
    return mcs_int(q < 0 ? 0 : q > 100 ? 100 : q);
}
NATIVE(wn_tostring) {
    WN(); char b[96];
    snprintf(b, sizeof b, "%s (%d dBm, ch %d, %s)", w->i.ssid[0] ? w->i.ssid : "<hidden>", w->i.rssi, w->i.channel, mcs_wifi_auth_name(w->i.auth));
    return mcs_string(vm, b);
}
static const mcs_reg_t wnet_members[] = {
    MCS_GET("Ssid", wn_ssid), MCS_GET("Bssid", wn_bssid), MCS_GET("Rssi", wn_rssi), MCS_GET("Channel", wn_channel),
    MCS_GET("Auth", wn_auth), MCS_GET("AuthName", wn_authname), MCS_GET("Secure", wn_secure), MCS_GET("Hidden", wn_hidden),
    MCS_GET("Protocols", wn_protocols), MCS_GET("Quality", wn_quality), MCS_FN("ToString", wn_tostring, 0), MCS_REG_END };
static const mcs_class_def_t wnet_def = { "WifiNetwork", sizeof(wnet_t), plain_ctor, NULL, wnet_members, NULL };

#define WC() wcli_t* c = (wcli_t*)mcs_check_userdata(vm, self, &wcli_def); if (!c) return mcs_null()
NATIVE(wc_mac) { WC(); return mac_val(vm, c->c.mac); }
NATIVE(wc_rssi) { WC(); return mcs_int(c->c.rssi); }
NATIVE(wc_ip) { WC(); return ip_val(vm, c->c.ip); }
NATIVE(wc_tostring) {
    WC(); char b[64], ip[16]; mcs__ip_fmt(c->c.ip, ip);
    snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X %s (%d dBm)", c->c.mac[0], c->c.mac[1], c->c.mac[2], c->c.mac[3], c->c.mac[4], c->c.mac[5], ip, c->c.rssi);
    return mcs_string(vm, b);
}
static const mcs_reg_t wcli_members[] = {
    MCS_GET("Mac", wc_mac), MCS_GET("Rssi", wc_rssi), MCS_GET("Ip", wc_ip), MCS_FN("ToString", wc_tostring, 0), MCS_REG_END };
static const mcs_class_def_t wcli_def = { "WifiClient", sizeof(wcli_t), plain_ctor, NULL, wcli_members, NULL };

static int link_of(mcs__net_state_t* s, mcs_wifi_link_t* l) {
    memset(l, 0, sizeof *l);
    return s->net->sta_link ? s->net->sta_link(s->net->ctx, l) : MCS_HAL_ENOTSUP;
}
/* make sure the station interface runs (Off -> Station, AccessPoint -> both) */
static int need_sta(mcs__net_state_t* s) {
    int m = s->net->wifi_get_mode ? s->net->wifi_get_mode(s->net->ctx) : MCS_WIFI_STA;
    if (m < 0) return m;
    if (m & MCS_WIFI_STA) return 0;
    return s->net->wifi_set_mode(s->net->ctx, m | MCS_WIFI_STA);
}
static int need_ap(mcs__net_state_t* s) {
    int m = s->net->wifi_get_mode ? s->net->wifi_get_mode(s->net->ctx) : MCS_WIFI_AP;
    if (m < 0) return m;
    if (m & MCS_WIFI_AP) return 0;
    return s->net->wifi_set_mode(s->net->ctx, m | MCS_WIFI_AP);
}
static int sta_start(mcs__net_state_t* s, const mcs_wifi_sta_cfg_t* c) {
    int rc = need_sta(s);
    if (rc < 0) return rc;
    if (c != &s->last) {
        s->last = *c;
        s->eap_identity[0] = s->eap_user[0] = 0;
        if (c->identity) { snprintf(s->eap_identity, sizeof s->eap_identity, "%s", c->identity); s->last.identity = s->eap_identity; }
        if (c->username) { snprintf(s->eap_user, sizeof s->eap_user, "%s", c->username); s->last.username = s->eap_user; }
        s->last.ca_pem = NULL;
        s->have_last = 1;
    }
    s->last_reason = 0;
    return s->net->sta_connect(s->net->ctx, c);
}
static bool failed_status(int st) { return st == MCS_WIFI_NO_SSID || st == MCS_WIFI_WRONG_PASSWORD || st == MCS_WIFI_CONNECT_FAILED; }
/* wait for an IP address: true connected, false failed or timed out */
static mcs_value_t wait_connected(mcs_vm_t* vm, mcs__net_state_t* s, uint32_t timeout_ms) {
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        mcs_wifi_link_t l;
        int rc = link_of(s, &l);
        if (rc < 0) return mcs__net_fail(vm, "Wifi.Connect", rc);
        if (l.status == MCS_WIFI_CONNECTED) return mcs_bool(true);
        if (failed_status(l.status)) { if (l.reason) s->last_reason = l.reason; return mcs_bool(false); }
        if ((uint32_t)(mcs_ticks(vm) - t0) >= timeout_ms) return mcs_bool(false);
        if (!mcs__net_wait(vm, 20)) return mcs_null();
    }
}
/* ---- saved credentials: the port's store (NVS on ESP32) or, without one, the file
 * MCS_WIFI_CREDS_FILE on the VM's filesystem (e.g. LittleFS on a Pico W) */
#ifndef MCS_WIFI_CREDS_FILE
#define MCS_WIFI_CREDS_FILE "/.wifi"
#endif
static bool creds_avail(mcs_vm_t* vm, const mcs_net_t* n) {
    if (n && n->creds) return true;
#if MCS_ENABLE_FS
    return n && n->wifi_set_mode && mcs_get_ext(vm, MCS_EXT_VFS) != NULL;
#else
    (void)vm; return false;
#endif
}
static int creds_op(mcs_vm_t* vm, const mcs_net_t* n, int op, char ssid[33], char pass[65]) {
    if (n && n->creds) return n->creds(n->ctx, op, ssid, pass);
#if MCS_ENABLE_FS
    mcs_vfs_t* vfs = (mcs_vfs_t*)mcs_get_ext(vm, MCS_EXT_VFS);
    if (!vfs || !creds_avail(vm, n)) return MCS_HAL_ENOTSUP;
    if (op == MCS_WIFI_CREDS_FORGET) { mcs_vfs_remove(vfs, MCS_WIFI_CREDS_FILE); return 0; }
    if (op == MCS_WIFI_CREDS_SAVE) {
        char b[33 + 65 + 2];
        int l = snprintf(b, sizeof b, "%s\n%s\n", ssid, pass);
        int e = mcs_vfs_write_file(vfs, MCS_WIFI_CREDS_FILE, b, (size_t)l, false);
        memset(b, 0, sizeof b);
        return e < 0 ? MCS_HAL_ERR : 0;
    }
    char* d = NULL; size_t len = 0;
    if (mcs_vfs_read_file(vfs, MCS_WIFI_CREDS_FILE, &d, &len) < 0 || !d) return MCS_HAL_ENODEV;
    size_t i = 0, k = 0;
    while (i < len && d[i] != '\n' && k < 32) ssid[k++] = d[i++];
    ssid[k] = 0;
    while (i < len && d[i] != '\n') i++;
    if (i < len) i++;
    k = 0;
    while (i < len && d[i] != '\n' && d[i] != '\r' && k < 64) pass[k++] = d[i++];
    pass[k] = 0;
    memset(d, 0, len);
    mcs_vfs_free(vfs, d, len);
    return ssid[0] ? 0 : MCS_HAL_ENODEV;
#else
    (void)vm; (void)op; (void)ssid; (void)pass;
    return MCS_HAL_ENOTSUP;
#endif
}
/* (ssid[, password[, timeoutMs[, bssid[, channel]]]]) -> cfg; argc 0: saved credentials */
static bool sta_args(mcs_vm_t* vm, mcs__net_state_t* s, int argc, mcs_value_t* argv, mcs_wifi_sta_cfg_t* c, uint32_t* timeout) {
    memset(c, 0, sizeof *c);
    c->min_auth = -1;
    *timeout = MCS_WIFI_CONNECT_TIMEOUT;
    if (argc == 0 || mcs_is_null(argv[0])) {
        if (creds_op(vm, s->net, MCS_WIFI_CREDS_LOAD, c->ssid, c->password) >= 0 && c->ssid[0]) {}
        else if (s->have_last) *c = s->last;
        else { mcs_raise(vm, "InvalidOperationException", "no Wi-Fi network saved: Wifi.Connect(ssid, password) or Wifi.Save(ssid, password) first"); return false; }
    } else {
        if (!str_copy(vm, argv[0], c->ssid, sizeof c->ssid, "SSID")) return false;
        if (!c->ssid[0]) { mcs_raise(vm, "ArgumentException", "SSID is empty"); return false; }
        if (argc > 1 && !mcs_is_null(argv[1]) && !str_copy(vm, argv[1], c->password, sizeof c->password, "password")) return false;
    }
    size_t pl = strlen(c->password);
    if (pl && pl < 8 && pl != 5 && pl != 13) { mcs_raise(vm, "ArgumentException", "Wi-Fi password: 8..64 characters (WPA) or 5 / 13 (WEP)"); return false; }
    if (argc > 2 && !mcs_is_null(argv[2])) {
        mcs_int_t t = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return false;
        if (t < 0) { mcs_raise(vm, "ArgumentOutOfRangeException", "timeout must be >= 0 ms"); return false; }
        *timeout = (uint32_t)t;
    }
    if (argc > 3 && !mcs_is_null(argv[3])) { if (!mac_arg(vm, argv[3], c->bssid)) return false; c->has_bssid = 1; }
    if (argc > 4 && !mcs_is_null(argv[4])) {
        mcs_int_t ch = mcs_to_int(vm, argv[4]);
        if (mcs_has_exception(vm)) return false;
        if (ch < 0 || ch > 196) { mcs_raise(vm, "ArgumentOutOfRangeException", "Wi-Fi channel out of range"); return false; }
        c->channel = (uint8_t)ch;
    }
    return true;
}
NATIVE(wifi_connect) {
    ST(); NEED(sta_connect, "Wifi.Connect");
    if (argc > 5) { mcs_raise(vm, "ArgumentException", "Wifi.Connect([ssid[, password[, timeoutMs[, bssid[, channel]]]]])"); return mcs_null(); }
    mcs_wifi_sta_cfg_t c; uint32_t timeout;
    if (!sta_args(vm, s, argc, argv, &c, &timeout)) return mcs_null();
    CALL("Wifi.Connect", sta_start(s, &c));
    return wait_connected(vm, s, timeout);
}
NATIVE(wifi_begin) {
    ST(); NEED(sta_connect, "Wifi.Begin");
    if (argc > 5) { mcs_raise(vm, "ArgumentException", "Wifi.Begin([ssid[, password[, timeoutMs[, bssid[, channel]]]]])"); return mcs_null(); }
    mcs_wifi_sta_cfg_t c; uint32_t timeout;
    if (!sta_args(vm, s, argc, argv, &c, &timeout)) return mcs_null();
    CALL("Wifi.Begin", sta_start(s, &c));
    return mcs_null();
}
NATIVE(wifi_connect_ent) {   /* (ssid, identity, username, password[, timeoutMs[, caPem]]) */
    ST(); NEED(sta_connect, "Wifi.ConnectEnterprise");
    if (argc < 4 || argc > 6) { mcs_raise(vm, "ArgumentException", "Wifi.ConnectEnterprise(ssid, identity, username, password[, timeoutMs[, caPem]])"); return mcs_null(); }
    mcs_wifi_sta_cfg_t c; memset(&c, 0, sizeof c); c.min_auth = -1;
    char ident[65], user[65];
    if (!str_copy(vm, argv[0], c.ssid, sizeof c.ssid, "SSID") || !str_copy(vm, argv[1], ident, sizeof ident, "identity")
        || !str_copy(vm, argv[2], user, sizeof user, "username") || !str_copy(vm, argv[3], c.password, sizeof c.password, "password")) return mcs_null();
    OPT_INT(4, timeout, MCS_WIFI_CONNECT_TIMEOUT);
    OPT_STR(5, ca);
    if (timeout < 0) return mcs__net_fail(vm, "Wifi.ConnectEnterprise", MCS_HAL_EINVAL);
    c.identity = ident; c.username = user[0] ? user : ident; c.ca_pem = ca;
    CALL("Wifi.ConnectEnterprise", sta_start(s, &c));
    return wait_connected(vm, s, (uint32_t)timeout);
}
NATIVE(wifi_reconnect) {
    ST(); NEED(sta_connect, "Wifi.Reconnect");
    if (!s->have_last) return mcs_bool(false);
    if (NET()->sta_disconnect) NET()->sta_disconnect(NET()->ctx);
    CALL("Wifi.Reconnect", sta_start(s, &s->last));
    return wait_connected(vm, s, MCS_WIFI_CONNECT_TIMEOUT);
}
NATIVE(wifi_wait) {
    ST(); NEED(sta_link, "Wifi.WaitConnected");
    OPT_INT(0, timeout, MCS_WIFI_CONNECT_TIMEOUT);
    if (timeout < 0) return mcs__net_fail(vm, "Wifi.WaitConnected", MCS_HAL_EINVAL);
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        mcs_wifi_link_t l;
        CALL("Wifi.WaitConnected", link_of(s, &l));
        if (l.status == MCS_WIFI_CONNECTED) return mcs_bool(true);
        if ((uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) return mcs_bool(false);
        if (!mcs__net_wait(vm, 20)) return mcs_null();
    }
}
NATIVE(wifi_disconnect) {
    ST(); NEED(sta_disconnect, "Wifi.Disconnect");
    CALL("Wifi.Disconnect", NET()->sta_disconnect(NET()->ctx));
    return mcs_null();
}
NATIVE(wifi_off) {
    ST(); NEED(wifi_set_mode, "Wifi.Off");
    CALL("Wifi.Off", NET()->wifi_set_mode(NET()->ctx, MCS_WIFI_OFF));
    return mcs_null();
}
NATIVE(wifi_get_mode) {
    ST(); NEED(wifi_get_mode, "Wifi.Mode");
    int m = NET()->wifi_get_mode(NET()->ctx);
    if (m < 0) return mcs__net_fail(vm, "Wifi.Mode", m);
    return mcs_int(m);
}
NATIVE(wifi_set_mode) {
    ST(); NEED(wifi_set_mode, "Wifi.Mode");
    INT_ARG(0, m);
    if (m < MCS_WIFI_OFF || m > MCS_WIFI_APSTA) { mcs_raise(vm, "ArgumentOutOfRangeException", "Wifi.Mode: Wifi.Off, Station, AccessPoint or StationAp"); return mcs_null(); }
    CALL("Wifi.Mode", NET()->wifi_set_mode(NET()->ctx, (int)m));
    return mcs_null();
}
NATIVE(wifi_status) { ST(); mcs_wifi_link_t l; if (!NET() || link_of(s, &l) < 0) return mcs_int(MCS_WIFI_IDLE); return mcs_int(l.status); }
NATIVE(wifi_status_name) { ST(); mcs_wifi_link_t l; if (!NET() || link_of(s, &l) < 0) l.status = MCS_WIFI_IDLE; return mcs_string(vm, status_name(l.status)); }
NATIVE(wifi_connected) { ST(); mcs_wifi_link_t l; return mcs_bool(NET() && link_of(s, &l) >= 0 && l.status == MCS_WIFI_CONNECTED); }
NATIVE(wifi_reason) {
    ST(); mcs_wifi_link_t l;
    if (NET() && link_of(s, &l) >= 0 && l.reason) return mcs_int(l.reason);
    return mcs_int(s->last_reason);
}
NATIVE(wifi_reason_name) {
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "Wifi.ReasonName([reason])"); return mcs_null(); }
    if (argc == 1) { INT_ARG(0, r); return mcs_string(vm, mcs_wifi_reason_name((int)r)); }
    mcs_value_t v = wifi_reason(vm, self, 0, argv);
    CHECK();
    return mcs_string(vm, mcs_wifi_reason_name((int)v.as.i));
}
NATIVE(wifi_event_name) { INT_ARG(0, e); return mcs_string(vm, event_name((int)e)); }
NATIVE(wifi_auth_name) { INT_ARG(0, a); return mcs_string(vm, mcs_wifi_auth_name((int)a)); }
#define LINKED(op) ST(); NEED(sta_link, op); mcs_wifi_link_t l; CALL(op, link_of(s, &l)); bool up = l.status == MCS_WIFI_CONNECTED
NATIVE(wifi_ssid) { LINKED("Wifi.Ssid"); return up ? mcs_string(vm, l.ap.ssid) : mcs_string(vm, ""); }
NATIVE(wifi_bssid) { LINKED("Wifi.Bssid"); static const uint8_t z[6]; return mac_val(vm, up ? l.ap.bssid : z); }
NATIVE(wifi_rssi) { LINKED("Wifi.Rssi"); return mcs_int(up ? l.ap.rssi : 0); }
NATIVE(wifi_auth) { LINKED("Wifi.Auth"); return mcs_int(up ? l.ap.auth : MCS_WIFI_AUTH_OPEN); }

/* ---- IP configuration */
static int ip_of(mcs__net_state_t* s, int iface, mcs_net_ip_t* ip) {
    memset(ip, 0, sizeof *ip);
    return s->net && s->net->ip_get ? s->net->ip_get(s->net->ctx, iface, ip) : MCS_HAL_ENOTSUP;
}
#define IPGET(name, iface, field) NATIVE(name) { ST(); mcs_net_ip_t ip; int rc = ip_of(s, iface, &ip); \
    if (rc < 0) { return mcs__net_fail(vm, "Wifi." #field, rc); } \
    return ip_val(vm, ip.field); }
IPGET(wifi_ip, MCS_NET_IF_STA, ip)
IPGET(wifi_mask, MCS_NET_IF_STA, netmask)
IPGET(wifi_gw, MCS_NET_IF_STA, gateway)
IPGET(wifi_dns, MCS_NET_IF_STA, dns1)
IPGET(wifi_dns2, MCS_NET_IF_STA, dns2)
IPGET(wifi_apip, MCS_NET_IF_AP, ip)
NATIVE(wifi_dhcp) { ST(); mcs_net_ip_t ip; int rc = ip_of(s, MCS_NET_IF_STA, &ip); if (rc < 0) return mcs__net_fail(vm, "Wifi.Dhcp", rc); return mcs_bool(ip.dhcp != 0); }
static mcs_value_t set_ip(mcs_vm_t* vm, int iface, int argc, mcs_value_t* argv, const char* op) {
    ST(); NEED(ip_set, op);
    if (argc < 3 || argc > 5) { mcs_raise(vm, "ArgumentException", "%s(ip, gateway, netmask[, dns1[, dns2]])", op); return mcs_null(); }
    mcs_net_ip_t ip; memset(&ip, 0, sizeof ip);
    if (!ip_arg(vm, argv[0], ip.ip, "ip") || !ip_arg(vm, argv[1], ip.gateway, "gateway") || !ip_arg(vm, argv[2], ip.netmask, "netmask")) return mcs_null();
    if (argc > 3 && !mcs_is_null(argv[3]) && !ip_arg(vm, argv[3], ip.dns1, "dns")) return mcs_null();
    if (argc > 4 && !mcs_is_null(argv[4]) && !ip_arg(vm, argv[4], ip.dns2, "dns")) return mcs_null();
    CALL(op, NET()->ip_set(NET()->ctx, iface, &ip));
    return mcs_null();
}
NATIVE(wifi_static) { return set_ip(vm, MCS_NET_IF_STA, argc, argv, "Wifi.SetStaticIp"); }
NATIVE(wifi_apsetip) { return set_ip(vm, MCS_NET_IF_AP, argc, argv, "Wifi.SetApIp"); }
NATIVE(wifi_usedhcp) {
    ST(); NEED(ip_set, "Wifi.UseDhcp");
    mcs_net_ip_t ip; memset(&ip, 0, sizeof ip); ip.dhcp = 1;
    CALL("Wifi.UseDhcp", NET()->ip_set(NET()->ctx, MCS_NET_IF_STA, &ip));
    return mcs_null();
}
static mcs_value_t get_mac(mcs_vm_t* vm, int iface) {
    ST(); NEED(mac_get, "Wifi.Mac");
    uint8_t m[6];
    CALL("Wifi.Mac", NET()->mac_get(NET()->ctx, iface, m));
    return mac_val(vm, m);
}
static mcs_value_t put_mac(mcs_vm_t* vm, int iface, mcs_value_t v) {
    ST(); NEED(mac_set, "Wifi.Mac");
    uint8_t m[6];
    if (!mac_arg(vm, v, m)) return mcs_null();
    if (m[0] & 1) { mcs_raise(vm, "ArgumentException", "Wifi.Mac: a multicast address (first byte odd) cannot be used"); return mcs_null(); }
    CALL("Wifi.Mac", NET()->mac_set(NET()->ctx, iface, m));
    return mcs_null();
}
NATIVE(wifi_mac) { return get_mac(vm, MCS_NET_IF_STA); }
NATIVE(wifi_setmac) { return put_mac(vm, MCS_NET_IF_STA, argv[0]); }
NATIVE(wifi_apmac) { return get_mac(vm, MCS_NET_IF_AP); }
NATIVE(wifi_setapmac) { return put_mac(vm, MCS_NET_IF_AP, argv[0]); }

/* ---- integer / string options */
static mcs_value_t opt_get(mcs_vm_t* vm, int opt, const char* op) {
    ST(); NEED(opt_get, op);
    int32_t v = 0;
    CALL(op, NET()->opt_get(NET()->ctx, opt, &v));
    return mcs_int(v);
}
static mcs_value_t opt_set(mcs_vm_t* vm, int opt, int32_t v, const char* op) {
    ST(); NEED(opt_set, op);
    CALL(op, NET()->opt_set(NET()->ctx, opt, v));
    return mcs_null();
}
#define OPT_PROP(get, set, opt, name) \
    NATIVE(get) { return opt_get(vm, opt, "Wifi." name); } \
    NATIVE(set) { INT_ARG(0, v); return opt_set(vm, opt, (int32_t)v, "Wifi." name); }
OPT_PROP(wifi_getps, wifi_setps, MCS_WIFI_OPT_POWER_SAVE, "PowerSave")
OPT_PROP(wifi_getproto, wifi_setproto, MCS_WIFI_OPT_PROTOCOL, "Protocol")
OPT_PROP(wifi_getapproto, wifi_setapproto, MCS_WIFI_OPT_AP_PROTOCOL, "ApProtocol")
OPT_PROP(wifi_getbw, wifi_setbw, MCS_WIFI_OPT_BANDWIDTH, "Bandwidth")
OPT_PROP(wifi_getminauth, wifi_setminauth, MCS_WIFI_OPT_MIN_AUTH, "MinAuth")
OPT_PROP(wifi_getchan, wifi_setchan, MCS_WIFI_OPT_CHANNEL, "Channel")
OPT_PROP(wifi_getli, wifi_setli, MCS_WIFI_OPT_LISTEN_INTERVAL, "ListenInterval")
OPT_PROP(wifi_getband, wifi_setband, MCS_WIFI_OPT_BAND, "Band")
OPT_PROP(wifi_getant, wifi_setant, MCS_WIFI_OPT_ANTENNA, "Antenna")
NATIVE(wifi_getauto) { mcs_value_t v = opt_get(vm, MCS_WIFI_OPT_AUTO_RECONNECT, "Wifi.AutoReconnect"); CHECK(); return mcs_bool(v.as.i != 0); }
NATIVE(wifi_setauto) { return opt_set(vm, MCS_WIFI_OPT_AUTO_RECONNECT, mcs_truthy(argv[0]), "Wifi.AutoReconnect"); }
NATIVE(wifi_getstore) { mcs_value_t v = opt_get(vm, MCS_WIFI_OPT_STORAGE, "Wifi.Persistent"); CHECK(); return mcs_bool(v.as.i != 0); }
NATIVE(wifi_setstore) { return opt_set(vm, MCS_WIFI_OPT_STORAGE, mcs_truthy(argv[0]), "Wifi.Persistent"); }
NATIVE(wifi_gettx) {          /* dBm (quarter-dBm steps) */
    mcs_value_t v = opt_get(vm, MCS_WIFI_OPT_TX_POWER, "Wifi.TxPower"); CHECK();
#if MCS_ENABLE_FLOAT
    return mcs_float((mcs_float_t)v.as.i / 4);
#else
    return mcs_int(v.as.i / 4);
#endif
}
NATIVE(wifi_settx) {
#if MCS_ENABLE_FLOAT
    mcs_float_t d = mcs_to_float(vm, argv[0]); CHECK();
    if (!(d >= 2 && d <= 21)) { mcs_raise(vm, "ArgumentOutOfRangeException", "Wifi.TxPower: 2 .. 21 dBm"); return mcs_null(); }
    int32_t q = (int32_t)(d * 4 + (mcs_float_t)0.5);
#else
    INT_ARG(0, d);
    if (d < 2 || d > 21) { mcs_raise(vm, "ArgumentOutOfRangeException", "Wifi.TxPower: 2 .. 21 dBm"); return mcs_null(); }
    int32_t q = (int32_t)d * 4;
#endif
    return opt_set(vm, MCS_WIFI_OPT_TX_POWER, q, "Wifi.TxPower");
}
static mcs_value_t str_get(mcs_vm_t* vm, int opt, const char* op) {
    ST(); NEED(str_get, op);
    char b[128]; b[0] = 0;
    CALL(op, NET()->str_get(NET()->ctx, opt, b, sizeof b));
    return mcs_string(vm, b);
}
static mcs_value_t str_set(mcs_vm_t* vm, int opt, mcs_value_t v, size_t max, const char* op) {
    ST(); NEED(str_set, op);
    const char* t = mcs_to_cstr(vm, v); CHECK();
    if (strlen(t) > max) { mcs_raise(vm, "ArgumentException", "%s: at most %d characters", op, (int)max); return mcs_null(); }
    CALL(op, NET()->str_set(NET()->ctx, opt, t));
    return mcs_null();
}
NATIVE(wifi_gethost) { return str_get(vm, MCS_WIFI_STR_HOSTNAME, "Wifi.Hostname"); }
NATIVE(wifi_sethost) {
    const char* h = mcs_to_cstr(vm, argv[0]); CHECK();
    for (const char* p = h; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || (*p == '-' && p != h))) {
            mcs_raise(vm, "ArgumentException", "Wifi.Hostname: letters, digits and '-' only"); return mcs_null(); }
    if (!*h) { mcs_raise(vm, "ArgumentException", "Wifi.Hostname is empty"); return mcs_null(); }
    return str_set(vm, MCS_WIFI_STR_HOSTNAME, argv[0], 32, "Wifi.Hostname");
}
NATIVE(wifi_getcountry) { return str_get(vm, MCS_WIFI_STR_COUNTRY, "Wifi.Country"); }
NATIVE(wifi_setcountry) {
    const char* c = mcs_to_cstr(vm, argv[0]); CHECK();
    if (strlen(c) != 2) { mcs_raise(vm, "ArgumentException", "Wifi.Country: two letters (\"US\", \"DE\", \"JP\" ... or \"01\" = world)"); return mcs_null(); }
    return str_set(vm, MCS_WIFI_STR_COUNTRY, argv[0], 2, "Wifi.Country");
}
NATIVE(wifi_ip6) { return str_get(vm, MCS_WIFI_STR_IP6, "Wifi.Ipv6"); }

/* ---- scan */
NATIVE(wifi_scan) {     /* ([showHidden[, timeoutMs]]) -> WifiNetwork[] strongest first */
    ST(); NEED(scan, "Wifi.Scan");
    if (argc > 2) { mcs_raise(vm, "ArgumentException", "Wifi.Scan([showHidden[, timeoutMs]])"); return mcs_null(); }
    bool hidden = argc > 0 && mcs_truthy(argv[0]);
    OPT_INT(1, timeout, 10000);
    if (timeout < 0) return mcs__net_fail(vm, "Wifi.Scan", MCS_HAL_EINVAL);
    CALL("Wifi.Scan", need_sta(s));
    size_t sz = sizeof(mcs_wifi_ap_info_t) * MCS_WIFI_SCAN_MAX;
    mcs_wifi_ap_info_t* r = (mcs_wifi_ap_info_t*)mcs_mem_realloc(vm, NULL, 0, sz);
    if (!r) return mcs_null();
    int n = NET()->scan(NET()->ctx, r, MCS_WIFI_SCAN_MAX, hidden, (uint32_t)timeout);
    if (n < 0) { mcs_mem_realloc(vm, r, sz, 0); return mcs__net_fail(vm, "Wifi.Scan", n); }
    if (n > MCS_WIFI_SCAN_MAX) n = MCS_WIFI_SCAN_MAX;
    for (int i = 1; i < n; i++) {               /* strongest first (stable insertion sort) */
        mcs_wifi_ap_info_t t = r[i]; int j = i - 1;
        while (j >= 0 && r[j].rssi < t.rssi) { r[j + 1] = r[j]; j--; }
        r[j + 1] = t;
    }
    mcs_value_t arr = mcs_new_array(vm, (uint32_t)n);
    mcs_push_root(vm, arr);
    for (int i = 0; i < n; i++) {
        mcs_value_t o;
        if (mcs_new_object(vm, "WifiNetwork", 0, NULL, &o) != MCS_OK) break;
        ((wnet_t*)mcs_userdata(o))->i = r[i];
        mcs_set_index(arr, (uint32_t)i, o);
    }
    mcs_pop_root(vm, 1);
    mcs_mem_realloc(vm, r, sz, 0);
    return arr;
}

/* ---- access point */
NATIVE(wifi_startap) {  /* (ssid[, password[, channel[, hidden[, maxClients]]]]) */
    ST(); NEED(ap_start, "Wifi.StartAp");
    if (argc < 1 || argc > 5) { mcs_raise(vm, "ArgumentException", "Wifi.StartAp(ssid[, password[, channel[, hidden[, maxClients]]]])"); return mcs_null(); }
    mcs_wifi_ap_cfg_t c; memset(&c, 0, sizeof c);
    if (!str_copy(vm, argv[0], c.ssid, sizeof c.ssid, "SSID")) return mcs_null();
    if (!c.ssid[0]) { mcs_raise(vm, "ArgumentException", "SSID is empty"); return mcs_null(); }
    if (argc > 1 && !mcs_is_null(argv[1]) && !str_copy(vm, argv[1], c.password, sizeof c.password, "password")) return mcs_null();
    size_t pl = strlen(c.password);
    if (pl && pl < 8) { mcs_raise(vm, "ArgumentException", "access point password: 8..64 characters (or \"\" / null for an open network)"); return mcs_null(); }
    OPT_INT(2, ch, 0);
    if (ch < 0 || ch > 196) { mcs_raise(vm, "ArgumentOutOfRangeException", "Wi-Fi channel out of range"); return mcs_null(); }
    c.channel = (uint8_t)ch;
    c.hidden = argc > 3 && mcs_truthy(argv[3]);
    OPT_INT(4, maxc, 0);
    if (maxc < 0 || maxc > 15) { mcs_raise(vm, "ArgumentOutOfRangeException", "maxClients: 1..15 (0 = default)"); return mcs_null(); }
    c.max_clients = (uint8_t)maxc;
    c.auth = pl ? MCS_WIFI_AUTH_WPA2_PSK : MCS_WIFI_AUTH_OPEN;
    CALL("Wifi.StartAp", need_ap(s));
    CALL("Wifi.StartAp", NET()->ap_start(NET()->ctx, &c));
    return mcs_null();
}
NATIVE(wifi_stopap) {
    ST(); NEED(ap_stop, "Wifi.StopAp");
    CALL("Wifi.StopAp", NET()->ap_stop(NET()->ctx));
    return mcs_null();
}
static int clients_of(mcs__net_state_t* s, mcs_wifi_client_t* out, int max) {
    return s->net && s->net->ap_clients ? s->net->ap_clients(s->net->ctx, out, max) : MCS_HAL_ENOTSUP;
}
NATIVE(wifi_apclients) {
    ST();
    mcs_wifi_client_t c[16];
    int n = clients_of(s, c, 16);
    if (n < 0) return mcs__net_fail(vm, "Wifi.ApClients", n);
    if (n > 16) n = 16;
    mcs_value_t arr = mcs_new_array(vm, (uint32_t)n);
    mcs_push_root(vm, arr);
    for (int i = 0; i < n; i++) {
        mcs_value_t o;
        if (mcs_new_object(vm, "WifiClient", 0, NULL, &o) != MCS_OK) break;
        ((wcli_t*)mcs_userdata(o))->c = c[i];
        mcs_set_index(arr, (uint32_t)i, o);
    }
    mcs_pop_root(vm, 1);
    return arr;
}
NATIVE(wifi_apcount) { ST(); mcs_wifi_client_t c[16]; int n = clients_of(s, c, 16); return mcs_int(n < 0 ? 0 : n); }

/* ---- saved credentials */
NATIVE(wifi_save) {
    ST();
    if (!creds_avail(vm, NET())) return mcs__net_fail(vm, "Wifi.Save", MCS_HAL_ENOTSUP);
    char ssid[33], pass[65]; pass[0] = 0;
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Wifi.Save(ssid[, password])"); return mcs_null(); }
    if (!str_copy(vm, argv[0], ssid, sizeof ssid, "SSID")) return mcs_null();
    if (argc > 1 && !mcs_is_null(argv[1]) && !str_copy(vm, argv[1], pass, sizeof pass, "password")) return mcs_null();
    CALL("Wifi.Save", creds_op(vm, NET(), MCS_WIFI_CREDS_SAVE, ssid, pass));
    return mcs_null();
}
static mcs_value_t provision(mcs_vm_t* vm, int argc, mcs_value_t* argv, int method, const char* op) {
    ST();
    if (!NET() || !NET()->provision || NET()->provision(NET()->ctx, method, MCS_WIFI_PROV_QUERY, NULL, NULL) < 0) return mcs__net_fail(vm, op, MCS_HAL_ENOTSUP);
    if (argc > 2) { mcs_raise(vm, "ArgumentException", "%s([timeoutMs[, save]])", op); return mcs_null(); }
    OPT_INT(0, timeout, 120000);
    bool save = argc < 2 || mcs_truthy(argv[1]);
    if (timeout < 0) return mcs__net_fail(vm, op, MCS_HAL_EINVAL);
    mcs_wifi_sta_cfg_t c; memset(&c, 0, sizeof c); c.min_auth = -1;
    CALL(op, NET()->provision(NET()->ctx, method, MCS_WIFI_PROV_START, c.ssid, c.password));
    uint32_t t0 = mcs_ticks(vm);
    int rc;
    for (;;) {
        rc = NET()->provision(NET()->ctx, method, MCS_WIFI_PROV_POLL, c.ssid, c.password);
        if (rc != 0) break;
        if (timeout && (uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) break;
        if (!mcs__net_wait(vm, 100)) { NET()->provision(NET()->ctx, method, MCS_WIFI_PROV_STOP, NULL, NULL); return mcs_null(); }
    }
    NET()->provision(NET()->ctx, method, MCS_WIFI_PROV_STOP, NULL, NULL);
    if (rc != 1 || !c.ssid[0]) return mcs_bool(false);
    if (save && creds_avail(vm, NET())) creds_op(vm, NET(), MCS_WIFI_CREDS_SAVE, c.ssid, c.password);
    CALL(op, sta_start(s, &c));
    return wait_connected(vm, s, MCS_WIFI_CONNECT_TIMEOUT);
}
NATIVE(wifi_wps) { return provision(vm, argc, argv, MCS_WIFI_PROV_WPS, "Wifi.Wps"); }
NATIVE(wifi_smartconfig) { return provision(vm, argc, argv, MCS_WIFI_PROV_SMARTCONFIG, "Wifi.SmartConfig"); }
NATIVE(wifi_forget) {
    ST();
    if (!creds_avail(vm, NET())) return mcs__net_fail(vm, "Wifi.Forget", MCS_HAL_ENOTSUP);
    char ssid[33] = "", pass[65] = "";
    CALL("Wifi.Forget", creds_op(vm, NET(), MCS_WIFI_CREDS_FORGET, ssid, pass));
    return mcs_null();
}
NATIVE(wifi_saved) {
    ST();
    char ssid[33] = "", pass[65] = "";
    if (!creds_avail(vm, NET()) || creds_op(vm, NET(), MCS_WIFI_CREDS_LOAD, ssid, pass) < 0 || !ssid[0]) { memset(pass, 0, sizeof pass); return mcs_string(vm, ""); }
    memset(pass, 0, sizeof pass);
    return mcs_string(vm, ssid);
}

/* ---- callbacks */
static mcs_value_t set_cb(mcs_vm_t* vm, int slot, mcs_value_t fn, const char* op) {
    ST();
    int k = mcs_obj_kind(fn);
    if (!mcs_is_null(fn) && k != MCS_O_CLOSURE && k != MCS_O_NATIVE && k != MCS_O_BOUND && k != MCS_O_FUNCTION && k != MCS_O_OVERLOADS) {
        mcs_raise(vm, "ArgumentException", "%s: expected a callback (lambda or method)", op); return mcs_null(); }
#if MCS_ENABLE_HAL
    if (!mcs_is_null(fn) && !mcs_hal_events(vm)) {
        mcs_raise(vm, "InvalidOperationException", "%s: callbacks run in the main script (with the HAL library), not in a thread", op); return mcs_null(); }
#else
    if (!mcs_is_null(fn)) { mcs_raise(vm, "NotSupportedException", "%s: needs the HAL event queue (MCS_ENABLE_HAL)", op); return mcs_null(); }
#endif
    mcs_set_index(mcs_pinned(vm, s->cb_pin), (uint32_t)slot, fn);
    return mcs_null();
}
NATIVE(wifi_onevent) { return set_cb(vm, MCS__NET_CB_WIFI, argv[0], "Wifi.OnEvent"); }

static const mcs_reg_t wifi_fns[] = {
    MCS_FN("Connect", wifi_connect, -1), MCS_FN("Begin", wifi_begin, -1), MCS_FN("ConnectEnterprise", wifi_connect_ent, -1),
    MCS_FN("Reconnect", wifi_reconnect, 0), MCS_FN("WaitConnected", wifi_wait, -1), MCS_FN("Disconnect", wifi_disconnect, 0),
    MCS_FN("Off", wifi_off, 0), MCS_GET("Mode", wifi_get_mode), MCS_SET("Mode", wifi_set_mode),
    MCS_GET("Status", wifi_status), MCS_GET("StatusName", wifi_status_name), MCS_GET("IsConnected", wifi_connected),
    MCS_GET("DisconnectReason", wifi_reason), MCS_FN("ReasonName", wifi_reason_name, -1), MCS_FN("EventName", wifi_event_name, 1),
    MCS_FN("AuthName", wifi_auth_name, 1),
    MCS_GET("Ssid", wifi_ssid), MCS_GET("Bssid", wifi_bssid), MCS_GET("Rssi", wifi_rssi), MCS_GET("Auth", wifi_auth),
    MCS_GET("Channel", wifi_getchan), MCS_SET("Channel", wifi_setchan),
    MCS_GET("Ip", wifi_ip), MCS_GET("Netmask", wifi_mask), MCS_GET("Gateway", wifi_gw), MCS_GET("Dns", wifi_dns), MCS_GET("Dns2", wifi_dns2),
    MCS_GET("Ipv6", wifi_ip6), MCS_GET("Dhcp", wifi_dhcp),
    MCS_FN("SetStaticIp", wifi_static, -1), MCS_FN("UseDhcp", wifi_usedhcp, 0),
    MCS_GET("Mac", wifi_mac), MCS_SET("Mac", wifi_setmac), MCS_GET("ApMac", wifi_apmac), MCS_SET("ApMac", wifi_setapmac),
    MCS_GET("Hostname", wifi_gethost), MCS_SET("Hostname", wifi_sethost),
    MCS_GET("Country", wifi_getcountry), MCS_SET("Country", wifi_setcountry),
    MCS_GET("AutoReconnect", wifi_getauto), MCS_SET("AutoReconnect", wifi_setauto),
    MCS_GET("Persistent", wifi_getstore), MCS_SET("Persistent", wifi_setstore),
    MCS_GET("PowerSave", wifi_getps), MCS_SET("PowerSave", wifi_setps),
    MCS_GET("TxPower", wifi_gettx), MCS_SET("TxPower", wifi_settx),
    MCS_GET("Protocol", wifi_getproto), MCS_SET("Protocol", wifi_setproto),
    MCS_GET("ApProtocol", wifi_getapproto), MCS_SET("ApProtocol", wifi_setapproto),
    MCS_GET("Bandwidth", wifi_getbw), MCS_SET("Bandwidth", wifi_setbw),
    MCS_GET("MinAuth", wifi_getminauth), MCS_SET("MinAuth", wifi_setminauth),
    MCS_GET("ListenInterval", wifi_getli), MCS_SET("ListenInterval", wifi_setli),
    MCS_GET("Band", wifi_getband), MCS_SET("Band", wifi_setband),
    MCS_GET("Antenna", wifi_getant), MCS_SET("Antenna", wifi_setant),
    MCS_FN("Scan", wifi_scan, -1),
    MCS_FN("StartAp", wifi_startap, -1), MCS_FN("StopAp", wifi_stopap, 0), MCS_GET("ApIp", wifi_apip),
    MCS_FN("SetApIp", wifi_apsetip, -1), MCS_FN("ApClients", wifi_apclients, 0), MCS_GET("ApClientCount", wifi_apcount),
    MCS_FN("Save", wifi_save, -1), MCS_FN("Forget", wifi_forget, 0), MCS_GET("SavedSsid", wifi_saved),
    MCS_FN("Wps", wifi_wps, -1), MCS_FN("SmartConfig", wifi_smartconfig, -1),
    MCS_FN("OnEvent", wifi_onevent, 1),
    MCS_REG_END };
static const mcs_const_t wifi_consts[] = {
    MCS_CONST("ModeOff", MCS_WIFI_OFF), MCS_CONST("ModeStation", MCS_WIFI_STA), MCS_CONST("ModeAccessPoint", MCS_WIFI_AP), MCS_CONST("ModeStationAp", MCS_WIFI_APSTA),
    MCS_CONST("Idle", MCS_WIFI_IDLE), MCS_CONST("NoSsid", MCS_WIFI_NO_SSID), MCS_CONST("ScanDone", MCS_WIFI_SCAN_DONE),
    MCS_CONST("Connected", MCS_WIFI_CONNECTED), MCS_CONST("ConnectFailed", MCS_WIFI_CONNECT_FAILED),
    MCS_CONST("ConnectionLost", MCS_WIFI_CONNECTION_LOST), MCS_CONST("Disconnected", MCS_WIFI_DISCONNECTED),
    MCS_CONST("Connecting", MCS_WIFI_CONNECTING), MCS_CONST("WrongPassword", MCS_WIFI_WRONG_PASSWORD), MCS_CONST("Stopped", MCS_WIFI_STOPPED),
    MCS_CONST("AuthOpen", MCS_WIFI_AUTH_OPEN), MCS_CONST("AuthWep", MCS_WIFI_AUTH_WEP), MCS_CONST("AuthWpa", MCS_WIFI_AUTH_WPA_PSK),
    MCS_CONST("AuthWpa2", MCS_WIFI_AUTH_WPA2_PSK), MCS_CONST("AuthWpaWpa2", MCS_WIFI_AUTH_WPA_WPA2_PSK),
    MCS_CONST("AuthEnterprise", MCS_WIFI_AUTH_ENTERPRISE), MCS_CONST("AuthWpa3", MCS_WIFI_AUTH_WPA3_PSK),
    MCS_CONST("AuthWpa2Wpa3", MCS_WIFI_AUTH_WPA2_WPA3_PSK), MCS_CONST("AuthWapi", MCS_WIFI_AUTH_WAPI_PSK),
    MCS_CONST("AuthOwe", MCS_WIFI_AUTH_OWE), MCS_CONST("AuthWpa3Enterprise192", MCS_WIFI_AUTH_WPA3_ENT_192),
    MCS_CONST("PowerNone", MCS_WIFI_PS_NONE), MCS_CONST("PowerMin", MCS_WIFI_PS_MIN), MCS_CONST("PowerMax", MCS_WIFI_PS_MAX),
    MCS_CONST("Proto11b", MCS_WIFI_PROTO_B), MCS_CONST("Proto11g", MCS_WIFI_PROTO_G), MCS_CONST("Proto11n", MCS_WIFI_PROTO_N),
    MCS_CONST("ProtoLR", MCS_WIFI_PROTO_LR), MCS_CONST("Proto11ax", MCS_WIFI_PROTO_AX),
    MCS_CONST("Band2G", MCS_WIFI_BAND_2G), MCS_CONST("Band5G", MCS_WIFI_BAND_5G), MCS_CONST("BandAuto", MCS_WIFI_BAND_AUTO),
    MCS_CONST("EventStaStart", MCS_NET_EV_STA_START), MCS_CONST("EventStaStop", MCS_NET_EV_STA_STOP),
    MCS_CONST("EventConnected", MCS_NET_EV_CONNECTED), MCS_CONST("EventDisconnected", MCS_NET_EV_DISCONNECTED),
    MCS_CONST("EventGotIp", MCS_NET_EV_GOT_IP), MCS_CONST("EventLostIp", MCS_NET_EV_LOST_IP), MCS_CONST("EventGotIp6", MCS_NET_EV_GOT_IP6),
    MCS_CONST("EventScanDone", MCS_NET_EV_SCAN_DONE), MCS_CONST("EventApStart", MCS_NET_EV_AP_START), MCS_CONST("EventApStop", MCS_NET_EV_AP_STOP),
    MCS_CONST("EventApJoined", MCS_NET_EV_AP_JOINED), MCS_CONST("EventApLeft", MCS_NET_EV_AP_LEFT), MCS_CONST("EventApAssigned", MCS_NET_EV_AP_ASSIGNED),
    MCS_CONST("EventEspNowRecv", MCS_NET_EV_ESPNOW_RECV), MCS_CONST("EventEspNowSent", MCS_NET_EV_ESPNOW_SENT),
    MCS_CONST("EventNtpSync", MCS_NET_EV_NTP_SYNC),
    MCS_CONST("ReasonBeaconTimeout", MCS_WIFI_REASON_BEACON_TIMEOUT), MCS_CONST("ReasonNoApFound", MCS_WIFI_REASON_NO_AP_FOUND),
    MCS_CONST("ReasonAuthFail", MCS_WIFI_REASON_AUTH_FAIL), MCS_CONST("ReasonAssocFail", MCS_WIFI_REASON_ASSOC_FAIL),
    MCS_CONST("ReasonHandshakeTimeout", MCS_WIFI_REASON_HANDSHAKE_TIMEOUT), MCS_CONST("ReasonAssocLeave", MCS_WIFI_REASON_ASSOC_LEAVE),
    MCS_CONST_END };

/* ============================================================ EspNow */
#define ESPNOW_MAX 250       /* ESP-NOW v1 payload (v2 radios accept 1470; the front end keeps 250) */
typedef struct { uint8_t mac[6]; int16_t rssi; uint16_t len; uint8_t data[ESPNOW_MAX]; } enmsg_t;
static const mcs_class_def_t enmsg_def;
#define EM() enmsg_t* m = (enmsg_t*)mcs_check_userdata(vm, self, &enmsg_def); if (!m) return mcs_null()
NATIVE(em_mac) { EM(); return mac_val(vm, m->mac); }
NATIVE(em_data) { EM(); return mcs__bytes_new(vm, m->data, m->len); }
NATIVE(em_text) { EM(); return mcs_string_n(vm, (const char*)m->data, m->len); }
NATIVE(em_rssi) { EM(); return mcs_int(m->rssi); }
NATIVE(em_len) { EM(); return mcs_int(m->len); }
NATIVE(em_tostring) {
    EM(); char b[64];
    snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X: %u bytes", m->mac[0], m->mac[1], m->mac[2], m->mac[3], m->mac[4], m->mac[5], (unsigned)m->len);
    return mcs_string(vm, b);
}
static const mcs_reg_t enmsg_members[] = {
    MCS_GET("Mac", em_mac), MCS_GET("Data", em_data), MCS_GET("Text", em_text), MCS_GET("Rssi", em_rssi),
    MCS_GET("Length", em_len), MCS_FN("ToString", em_tostring, 0), MCS_REG_END };
static const mcs_class_def_t enmsg_def = { "EspNowMessage", sizeof(enmsg_t), plain_ctor, NULL, enmsg_members, NULL };

/* next packet as an EspNowMessage (null when none is waiting) */
static mcs_value_t en_next(mcs_vm_t* vm, mcs__net_state_t* s) {
    uint8_t mac[6], buf[ESPNOW_MAX]; int rssi = 0;
    int n = NET()->espnow_recv(NET()->ctx, mac, buf, sizeof buf, &rssi);
    if (n <= 0) return mcs_null();
    mcs_value_t o;
    if (mcs_new_object(vm, "EspNowMessage", 0, NULL, &o) != MCS_OK) return mcs_null();
    enmsg_t* m = (enmsg_t*)mcs_userdata(o);
    memcpy(m->mac, mac, 6); m->rssi = (int16_t)rssi; m->len = (uint16_t)(n > ESPNOW_MAX ? ESPNOW_MAX : n);
    memcpy(m->data, buf, m->len);
    return o;
}
#if MCS_ENABLE_HAL
static int en_drain(mcs_vm_t* vm, mcs__net_state_t* s) {
    mcs_value_t fn = cb_get(vm, s, MCS__NET_CB_ESPNOW);
    if (mcs_is_null(fn) || !NET() || !NET()->espnow_recv) return 0;
    int ran = 0;
    for (int i = 0; i < 16; i++) {
        mcs_value_t o = en_next(vm, s);
        if (mcs_is_null(o)) break;
        mcs_push_root(vm, o);
        int r;
        if (mcs_arity(fn) == 2) {      /* (mac, data) */
            enmsg_t* m = (enmsg_t*)mcs_userdata(o);
            mcs_value_t a[2];
            a[0] = mac_val(vm, m->mac); mcs_push_root(vm, a[0]);
            a[1] = mcs__bytes_new(vm, m->data, m->len); mcs_push_root(vm, a[1]);
            r = call_cb(vm, fn, a, 2);
            mcs_pop_root(vm, 2);
        } else r = call_cb(vm, fn, &o, 1);
        mcs_pop_root(vm, 1);
        if (r < 0) return r;
        ran++;
    }
    return ran;
}
#endif
#define EN(fn, op) ST(); NEED(fn, op)
NATIVE(en_start) {
    EN(espnow_start, "EspNow.Start");
    CALL("EspNow.Start", need_sta(s));
    CALL("EspNow.Start", NET()->espnow_start(NET()->ctx));
    return mcs_null();
}
NATIVE(en_stop) { EN(espnow_stop, "EspNow.Stop"); CALL("EspNow.Stop", NET()->espnow_stop(NET()->ctx)); return mcs_null(); }
static bool key16(mcs_vm_t* vm, mcs_value_t v, uint8_t key[16], const char* what) {
    const uint8_t* p; size_t n; uint8_t* heap;
    if (!mcs__bytes_arg(vm, v, &p, &n, &heap)) return false;
    bool ok = n == 16;
    if (ok) memcpy(key, p, 16);
    mcs__bytes_free(vm, heap, n);
    if (!ok) mcs_raise(vm, "ArgumentException", "%s: exactly 16 bytes (a 16-character string or byte[16])", what);
    return ok;
}
NATIVE(en_addpeer) {     /* (mac[, channel[, key]]) */
    EN(espnow_peer, "EspNow.AddPeer");
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "EspNow.AddPeer(mac[, channel[, key]])"); return mcs_null(); }
    uint8_t mac[6], lmk[16];
    if (!mac_arg(vm, argv[0], mac)) return mcs_null();
    OPT_INT(1, ch, 0);
    if (ch < 0 || ch > 14) { mcs_raise(vm, "ArgumentOutOfRangeException", "EspNow peer channel: 0 (current) .. 14"); return mcs_null(); }
    bool enc = argc > 2 && !mcs_is_null(argv[2]);
    if (enc && !key16(vm, argv[2], lmk, "EspNow.AddPeer key")) return mcs_null();
    CALL("EspNow.AddPeer", NET()->espnow_peer(NET()->ctx, mac, 1, (int)ch, enc ? lmk : NULL));
    return mcs_null();
}
NATIVE(en_rmpeer) {
    EN(espnow_peer, "EspNow.RemovePeer");
    uint8_t mac[6];
    if (!mac_arg(vm, argv[0], mac)) return mcs_null();
    CALL("EspNow.RemovePeer", NET()->espnow_peer(NET()->ctx, mac, 0, 0, NULL));
    return mcs_null();
}
NATIVE(en_peers) {
    EN(espnow_peers, "EspNow.Peers");
    uint8_t m[MCS_ESPNOW_MAX_PEERS][6];
    int n = NET()->espnow_peers(NET()->ctx, m, MCS_ESPNOW_MAX_PEERS);
    if (n < 0) return mcs__net_fail(vm, "EspNow.Peers", n);
    if (n > MCS_ESPNOW_MAX_PEERS) n = MCS_ESPNOW_MAX_PEERS;
    mcs_value_t arr = mcs_new_array(vm, (uint32_t)n);
    mcs_push_root(vm, arr);
    for (int i = 0; i < n; i++) mcs_set_index(arr, (uint32_t)i, mac_val(vm, m[i]));
    mcs_pop_root(vm, 1);
    return arr;
}
NATIVE(en_pmk) {
    EN(espnow_pmk, "EspNow.SetPmk");
    uint8_t k[16];
    if (!key16(vm, argv[0], k, "EspNow.SetPmk")) return mcs_null();
    CALL("EspNow.SetPmk", NET()->espnow_pmk(NET()->ctx, k));
    return mcs_null();
}
NATIVE(en_send) {       /* (mac or null = broadcast, data[, timeoutMs]) -> delivered */
    EN(espnow_send, "EspNow.Send");
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "EspNow.Send(mac, data[, timeoutMs])"); return mcs_null(); }
    uint8_t mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (!mcs_is_null(argv[0]) && !mac_arg(vm, argv[0], mac)) return mcs_null();
    OPT_INT(2, timeout, 1000);
    if (timeout < 0) return mcs__net_fail(vm, "EspNow.Send", MCS_HAL_EINVAL);
    const uint8_t* p; size_t n; uint8_t* heap;
    if (!mcs__bytes_arg(vm, argv[1], &p, &n, &heap)) return mcs_null();
    if (n == 0 || n > ESPNOW_MAX) { mcs__bytes_free(vm, heap, n); mcs_raise(vm, "ArgumentOutOfRangeException", "EspNow.Send: 1..%d bytes", ESPNOW_MAX); return mcs_null(); }
    int rc = NET()->espnow_send(NET()->ctx, mac, p, n, (uint32_t)timeout);
    mcs__bytes_free(vm, heap, n);
    if (rc == MCS_HAL_ENODEV) return mcs_bool(false);
    if (rc < 0) return mcs__net_fail(vm, "EspNow.Send", rc);
    return mcs_bool(true);
}
NATIVE(en_broadcast) { mcs_value_t a[2] = { mcs_null(), argv[0] }; return en_send(vm, self, 2, a); }
NATIVE(en_receive) {    /* ([timeoutMs]) -> EspNowMessage or null */
    EN(espnow_recv, "EspNow.Receive");
    OPT_INT(0, timeout, 0);
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        mcs_value_t o = en_next(vm, s);
        if (!mcs_is_null(o) || mcs_has_exception(vm)) return o;
        if (timeout >= 0 && (uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) return mcs_null();
        if (!mcs__net_wait(vm, 5)) return mcs_null();
    }
}
NATIVE(en_onreceive) { return set_cb(vm, MCS__NET_CB_ESPNOW, argv[0], "EspNow.OnReceive"); }
static const mcs_reg_t espnow_fns[] = {
    MCS_FN("Start", en_start, 0), MCS_FN("Stop", en_stop, 0), MCS_FN("AddPeer", en_addpeer, -1),
    MCS_FN("RemovePeer", en_rmpeer, 1), MCS_GET("Peers", en_peers), MCS_FN("SetPmk", en_pmk, 1),
    MCS_FN("Send", en_send, -1), MCS_FN("Broadcast", en_broadcast, 1), MCS_FN("Receive", en_receive, -1),
    MCS_FN("OnReceive", en_onreceive, 1), MCS_REG_END };
static const mcs_const_t espnow_consts[] = { MCS_CONST("MaxData", ESPNOW_MAX), MCS_CONST_END };

/* ============================================================ Ntp */
static char g_tz[64];
NATIVE(ntp_start) {     /* ([server1[, server2[, server3]]]) */
    ST();
    if (argc > 3) { mcs_raise(vm, "ArgumentException", "Ntp.Start([server1[, server2[, server3]]])"); return mcs_null(); }
    if (!NET() || !NET()->ntp_start) return mcs_null();          /* the system clock is already right */
    const char* sv[3]; int n = 0;
    for (int i = 0; i < argc; i++) { if (mcs_is_null(argv[i])) continue; sv[n] = mcs_to_cstr(vm, argv[i]); CHECK(); n++; }
    if (n == 0) sv[n++] = "pool.ntp.org";
    CALL("Ntp.Start", NET()->ntp_start(NET()->ctx, sv, n));
    return mcs_null();
}
NATIVE(ntp_synced) { ST(); return mcs_bool(!NET() || !NET()->ntp_start || NET()->ntp_synced(NET()->ctx) > 0); }
NATIVE(ntp_sync) {      /* ([server[, timeoutMs]]) or (timeoutMs) -> true once the clock is set */
    ST();
    if (argc > 2) { mcs_raise(vm, "ArgumentException", "Ntp.Sync([server[, timeoutMs]])"); return mcs_null(); }
    if (!NET() || !NET()->ntp_start) return mcs_bool(true);
    bool only_t = argc == 1 && mcs_is_number(argv[0]);
    const char* server = argc > 0 && !only_t && !mcs_is_null(argv[0]) ? mcs_to_cstr(vm, argv[0]) : NULL; CHECK();
    mcs_value_t tv = only_t ? argv[0] : argc > 1 ? argv[1] : mcs_null();
    mcs_int_t timeout = mcs_is_null(tv) ? (mcs_int_t)MCS_NET_TIMEOUT : mcs_to_int(vm, tv); CHECK();
    const char* sv[1] = { server ? server : "pool.ntp.org" };
    CALL("Ntp.Sync", NET()->ntp_start(NET()->ctx, sv, 1));
    uint32_t t0 = mcs_ticks(vm);
    while (NET()->ntp_synced(NET()->ctx) <= 0) {
        if ((uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) return mcs_bool(false);
        if (!mcs__net_wait(vm, 50)) return mcs_null();
    }
    return mcs_bool(true);
}
NATIVE(ntp_time) { time_t t = time(NULL); return mcs_int((mcs_int_t)t); }
static mcs_value_t fmt_time(mcs_vm_t* vm, bool local) {
    time_t t = time(NULL); struct tm tm; char b[32];
    if (local) localtime_r(&t, &tm); else gmtime_r(&t, &tm);
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm);
    return mcs_string(vm, b);
}
NATIVE(ntp_local) { return fmt_time(vm, true); }
NATIVE(ntp_utc) { return fmt_time(vm, false); }
NATIVE(ntp_gettz) { return mcs_string(vm, g_tz[0] ? g_tz : "UTC0"); }
NATIVE(ntp_settz) {
    const char* tz = mcs_to_cstr(vm, argv[0]); CHECK();
    if (strlen(tz) >= sizeof g_tz) { mcs_raise(vm, "ArgumentException", "Ntp.Timezone: at most %d characters (POSIX TZ string, e.g. \"CET-1CEST,M3.5.0,M10.5.0/3\")", (int)sizeof g_tz - 1); return mcs_null(); }
    snprintf(g_tz, sizeof g_tz, "%s", tz);
    setenv("TZ", g_tz, 1);
    tzset();
    return mcs_null();
}
NATIVE(ntp_offset) {    /* local - UTC in seconds */
    time_t t = time(NULL); struct tm lt, gt;
    localtime_r(&t, &lt); gmtime_r(&t, &gt);
    long d = (long)(lt.tm_hour - gt.tm_hour) * 3600 + (long)(lt.tm_min - gt.tm_min) * 60 + (lt.tm_sec - gt.tm_sec);
    int dd = lt.tm_yday - gt.tm_yday;
    if (dd == 1 || dd < -1) d += 86400; else if (dd == -1 || dd > 1) d -= 86400;
    return mcs_int((mcs_int_t)d);
}
static const mcs_reg_t ntp_fns[] = {
    MCS_FN("Start", ntp_start, -1), MCS_FN("Sync", ntp_sync, -1), MCS_GET("Synced", ntp_synced),
    MCS_GET("Time", ntp_time), MCS_GET("LocalTime", ntp_local), MCS_GET("UtcTime", ntp_utc),
    MCS_GET("Timezone", ntp_gettz), MCS_SET("Timezone", ntp_settz), MCS_GET("UtcOffset", ntp_offset), MCS_REG_END };

/* ============================================================ Mdns */
NATIVE(md_start) {      /* (hostname[, instanceName]) */
    ST(); NEED(mdns, "Mdns.Start");
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Mdns.Start(hostname[, instanceName])"); return mcs_null(); }
    STR_ARG(0, host); OPT_STR(1, inst);
    if (!*host || strlen(host) > 63) { mcs_raise(vm, "ArgumentException", "Mdns.Start: host name of 1..63 characters"); return mcs_null(); }
    CALL("Mdns.Start", NET()->mdns(NET()->ctx, MCS_MDNS_START, host, inst ? inst : host, 0, NULL, 0));
    return mcs_null();
}
NATIVE(md_stop) { ST(); NEED(mdns, "Mdns.Stop"); CALL("Mdns.Stop", NET()->mdns(NET()->ctx, MCS_MDNS_STOP, NULL, NULL, 0, NULL, 0)); return mcs_null(); }
NATIVE(md_add) {        /* (service, protocol, port[, txt string[]]) */
    ST(); NEED(mdns, "Mdns.AddService");
    if (argc < 3 || argc > 4) { mcs_raise(vm, "ArgumentException", "Mdns.AddService(service, protocol, port[, txt])"); return mcs_null(); }
    STR_ARG(0, svc); STR_ARG(1, proto); INT_ARG(2, port);
    if (port < 1 || port > 65535) { mcs_raise(vm, "ArgumentOutOfRangeException", "port: 1..65535"); return mcs_null(); }
    const char* txt[8]; int nt = 0;
    if (argc > 3 && !mcs_is_null(argv[3])) {
        int k = mcs_obj_kind(argv[3]);
        if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "Mdns.AddService txt: string[] of \"key=value\""); return mcs_null(); }
        uint32_t n = mcs_len(argv[3]);
        for (uint32_t i = 0; i < n && nt < 8; i++) { txt[nt] = mcs_to_cstr(vm, mcs_index(argv[3], i)); CHECK(); nt++; }
    }
    CALL("Mdns.AddService", NET()->mdns(NET()->ctx, MCS_MDNS_ADD, svc, proto, (int)port, txt, nt));
    return mcs_null();
}
NATIVE(md_remove) {
    ST(); NEED(mdns, "Mdns.RemoveService");
    STR_ARG(0, svc); STR_ARG(1, proto);
    CALL("Mdns.RemoveService", NET()->mdns(NET()->ctx, MCS_MDNS_REMOVE, svc, proto, 0, NULL, 0));
    return mcs_null();
}
NATIVE(md_resolve) {    /* (host[, timeoutMs]) -> IPv4 string or null */
    ST(); NEED(mdns_resolve, "Mdns.Resolve");
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Mdns.Resolve(host[, timeoutMs])"); return mcs_null(); }
    STR_ARG(0, host); OPT_INT(1, timeout, 2000);
    uint8_t ip[4];
    int rc = NET()->mdns_resolve(NET()->ctx, host, (uint32_t)(timeout < 0 ? 0 : timeout), ip);
    if (rc == MCS_HAL_ETIMEOUT || rc == MCS_HAL_ENODEV) return mcs_null();
    if (rc < 0) return mcs__net_fail(vm, "Mdns.Resolve", rc);
    return ip_val(vm, ip);
}
static const mcs_reg_t mdns_fns[] = {
    MCS_FN("Start", md_start, -1), MCS_FN("Stop", md_stop, 0), MCS_FN("AddService", md_add, -1),
    MCS_FN("RemoveService", md_remove, 2), MCS_FN("Resolve", md_resolve, -1), MCS_REG_END };

/* ============================================================ Ping */
NATIVE(ping_send) {     /* (host[, timeoutMs]) -> round trip ms, -1 = no answer */
    ST(); NEED(ping, "Ping.Send");
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Ping.Send(host[, timeoutMs])"); return mcs_null(); }
    STR_ARG(0, host); OPT_INT(1, timeout, 1000);
    if (timeout < 0) return mcs__net_fail(vm, "Ping.Send", MCS_HAL_EINVAL);
    uint8_t ip[4];
    if (!mcs__ip_parse(host, ip)) {
#if MCS_ENABLE_SOCKETS
        int rc = mcs__resolve4(vm, host, ip, MCS_NET_TIMEOUT);
        if (mcs_has_exception(vm)) return mcs_null();
        if (rc < 0) return mcs__net_fail(vm, "Ping.Send", rc);
#else
        return mcs__net_fail(vm, "Ping.Send", MCS_HAL_EINVAL);
#endif
    }
    int rc = NET()->ping(NET()->ctx, ip, (uint32_t)timeout);
    if (rc == MCS_HAL_ETIMEOUT || rc == MCS_HAL_ENODEV) return mcs_int(-1);
    if (rc < 0) return mcs__net_fail(vm, "Ping.Send", rc);
    return mcs_int(rc);
}
static const mcs_reg_t ping_fns[] = { MCS_FN("Send", ping_send, -1), MCS_REG_END };

/* ============================================================ Net */
bool mcs_net_has(const mcs_net_t* n, const char* f) {
    if (!f) return false;
    if (!strcmp(f, "Net")) return true;
    if (!strcmp(f, "Sockets") || !strcmp(f, "TcpClient") || !strcmp(f, "TcpListener") || !strcmp(f, "UdpClient")
        || !strcmp(f, "Dns") || !strcmp(f, "Http") || !strcmp(f, "HttpServer") || !strcmp(f, "WebSocket")
        || !strcmp(f, "DnsServer")) return MCS_ENABLE_SOCKETS != 0;
    if (!strcmp(f, "Ntp")) return true;
    if (!n) return false;
    if (!strcmp(f, "Wifi")) return n->wifi_set_mode != NULL && n->sta_connect != NULL;
    if (!strcmp(f, "Wifi.AP")) return n->ap_start != NULL;
    if (!strcmp(f, "Wifi.Scan")) return n->scan != NULL;
    if (!strcmp(f, "Wifi.Enterprise")) return n->sta_connect != NULL && !strcmp(n->name ? n->name : "", "esp32");
    if (!strcmp(f, "EspNow")) return n->espnow_start != NULL;
    if (!strcmp(f, "Mdns")) return n->mdns != NULL;
    if (!strcmp(f, "Ping")) return n->ping != NULL;
    if (!strcmp(f, "Tls") || !strcmp(f, "Https")) return n->tls_open != NULL && MCS_ENABLE_SOCKETS;
    if (!strcmp(f, "Ntp.Sntp")) return n->ntp_start != NULL;
    if (!strcmp(f, "Wifi.Wps")) return n->provision && n->provision(n->ctx, MCS_WIFI_PROV_WPS, MCS_WIFI_PROV_QUERY, NULL, NULL) >= 0;
    if (!strcmp(f, "Wifi.SmartConfig")) return n->provision && n->provision(n->ctx, MCS_WIFI_PROV_SMARTCONFIG, MCS_WIFI_PROV_QUERY, NULL, NULL) >= 0;
    if (!strcmp(f, "Wifi.Save")) return n->creds != NULL || (MCS_ENABLE_FS && n->wifi_set_mode != NULL);
    return false;
}
NATIVE(net_has) { ST(); STR_ARG(0, f); return mcs_bool(mcs_net_has(NET(), f)); }
NATIVE(net_backend) { ST(); return mcs_string(vm, NET() && NET()->name ? NET()->name : "none"); }
static const mcs_reg_t net_fns[] = { MCS_FN("Has", net_has, 1), MCS_GET("Backend", net_backend), MCS_REG_END };
static const mcs_const_t net_consts[] = { MCS_CONST("ApiVersion", MCS_NET_API_VERSION), MCS_CONST_END };

/* ============================================================ events + open */
#if MCS_ENABLE_HAL
static int net_event(mcs_vm_t* vm, const mcs_hal_event_t* ev) {
    mcs__net_state_t* s = mcs__net_state(vm);
    if (!s) return 0;
    if (ev->source == MCS_NET_EV_DISCONNECTED) s->last_reason = (int)ev->value;
    int ran = 0;
    if (ev->source == MCS_NET_EV_ESPNOW_RECV) {
        int r = en_drain(vm, s);
        if (r < 0) return r;
        ran += r;
    }
    mcs_value_t fn = cb_get(vm, s, MCS__NET_CB_WIFI);
    if (mcs_is_null(fn)) return ran;
    mcs_value_t a[2] = { mcs_int(ev->source), mcs_int(ev->value) };
    int r = call_cb(vm, fn, a, 2);
    return r < 0 ? r : ran + r;
}
#endif

void mcs_net_open_lib(mcs_vm_t* vm, const mcs_net_t* net) {
    mcs__net_state_t* s = mcs__net_state(vm);
    if (!s) {
        mcs_register_class(vm, &state_def);
        mcs_value_t obj;
        if (mcs_new_object(vm, "__NetState", 0, NULL, &obj) != MCS_OK) return;
        s = (mcs__net_state_t*)mcs_userdata(obj);
        memset(s, 0, sizeof *s);
        s->self_pin = mcs_pin(vm, obj);
        mcs_value_t list = mcs_new_list(vm);
        mcs_push_root(vm, list);
        for (int i = 0; i < MCS__NET_CB_COUNT; i++) mcs_list_add(vm, list, mcs_null());
        s->cb_pin = mcs_pin(vm, list);
        mcs_pop_root(vm, 1);
        if (s->self_pin < 0 || s->cb_pin < 0) {
            if (s->self_pin >= 0) mcs_unpin(vm, s->self_pin);
            if (s->cb_pin >= 0) mcs_unpin(vm, s->cb_pin);
            mcs_fail(vm, MCS_ERR_MEMORY, "network: no free pin handles (MCS_MAX_PINS)");
            return;
        }
        s->http_timeout = MCS_NET_TIMEOUT;
        snprintf(s->user_agent, sizeof s->user_agent, "MicroCS/%s", MCS_VERSION_STRING);
        mcs_set_ext(vm, MCS_EXT_NET, s);
    }
    s->net = net;
    mcs_register_module(vm, "Net", net_fns);
    mcs_register_consts(vm, "Net", net_consts);
    if (mcs_net_has(net, "Wifi")) {
        mcs_register_module(vm, "Wifi", wifi_fns);
        mcs_register_consts(vm, "Wifi", wifi_consts);
        mcs_register_class(vm, &wnet_def);
        mcs_register_class(vm, &wcli_def);
    }
    if (mcs_net_has(net, "EspNow")) {
        mcs_register_module(vm, "EspNow", espnow_fns);
        mcs_register_consts(vm, "EspNow", espnow_consts);
        mcs_register_class(vm, &enmsg_def);
    }
    mcs_register_module(vm, "Ntp", ntp_fns);
    if (mcs_net_has(net, "Mdns")) mcs_register_module(vm, "Mdns", mdns_fns);
    if (mcs_net_has(net, "Ping")) mcs_register_module(vm, "Ping", ping_fns);
#if MCS_ENABLE_SOCKETS
    mcs__sock_open(vm, net);
#endif
#if MCS_ENABLE_HAL
    mcs_hal_set_event_handler(MCS_HAL_EV_NET, net_event);
#endif
}
#endif
