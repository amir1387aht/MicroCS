/* MicroCS - HttpServer, HttpRequest, WebSocket (client + server side) and DnsServer (captive portal).
 * Built on the BSD socket layer in mcs_net_sys.h, so it runs on every port with MCS_ENABLE_SOCKETS. */
#include "mcs_net_internal.h"
#if MCS_ENABLE_NET && MCS_ENABLE_SOCKETS
#include "mcs_net_sys.h"
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#include <ctype.h>

#ifndef MCS_HTTPD_MAX_HEADER
#define MCS_HTTPD_MAX_HEADER 2048
#endif
#ifndef MCS_HTTPD_MAX_BODY
#define MCS_HTTPD_MAX_BODY 16384
#endif
#ifndef MCS_HTTPD_TIMEOUT
#define MCS_HTTPD_TIMEOUT 5000
#endif
#ifndef MCS_HTTPD_MAX_WS
#define MCS_HTTPD_MAX_WS 8
#endif
#ifndef MCS_WS_MAX_MESSAGE
#define MCS_WS_MAX_MESSAGE 8192
#endif

/* ============================================================ helpers */
static void* xalloc(mcs_vm_t* vm, void* p, size_t o, size_t n) { return mcs_mem_realloc(vm, p, o, n); }
static int ci_eq(const char* a, const char* b, size_t n) { for (size_t i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0; return 1; }
static const char* hfind(const char* h, size_t hl, const char* name, size_t* vl) {
    size_t nl = strlen(name); const char* p = h; const char* end = h + hl;
    while (p < end) {
        const char* eol = memchr(p, '\n', (size_t)(end - p)); if (!eol) eol = end;
        if ((size_t)(eol - p) > nl && p[nl] == ':' && ci_eq(p, name, nl)) {
            const char* v = p + nl + 1; while (v < eol && (*v == ' ' || *v == '\t')) v++;
            const char* ve = eol; while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
            *vl = (size_t)(ve - v); return v;
        }
        p = eol + 1;
    }
    return NULL;
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* url-decode s[0..n) into out (cap), returns length */
static size_t udec(const char* s, size_t n, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char c = s[i];
        if (c == '+') c = ' ';
        else if (c == '%' && i + 2 < n + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { c = (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
        out[o++] = c;
    }
    out[o] = 0; return o;
}
/* find `name` in "a=1&b=2" (n bytes); decoded value into out; true when found */
static bool qfind(const char* q, size_t n, const char* name, char* out, size_t cap) {
    if (!q) return false;
    size_t nl = strlen(name); const char* p = q; const char* end = q + n;
    char k[96];
    while (p < end) {
        const char* amp = memchr(p, '&', (size_t)(end - p)); if (!amp) amp = end;
        const char* eq = memchr(p, '=', (size_t)(amp - p));
        const char* ke = eq ? eq : amp;
        size_t kl = udec(p, (size_t)(ke - p), k, sizeof k);
        if (kl == nl && !memcmp(k, name, nl)) { if (eq) udec(eq + 1, (size_t)(amp - eq - 1), out, cap); else out[0] = 0; return true; }
        p = amp + 1;
    }
    return false;
}
static mcs_value_t qdict(mcs_vm_t* vm, const char* q, size_t n) {
    mcs_value_t d = mcs_new_dictionary(vm); mcs_push_root(vm, d);
    const char* p = q; const char* end = q ? q + n : q;
    char k[96], v[512];
    while (q && p < end) {
        const char* amp = memchr(p, '&', (size_t)(end - p)); if (!amp) amp = end;
        const char* eq = memchr(p, '=', (size_t)(amp - p));
        const char* ke = eq ? eq : amp;
        if (ke > p) {
            udec(p, (size_t)(ke - p), k, sizeof k);
            if (eq) udec(eq + 1, (size_t)(amp - eq - 1), v, sizeof v); else v[0] = 0;
            mcs_value_t ks = mcs_string(vm, k); mcs_push_root(vm, ks);
            mcs_value_t vs = mcs_string(vm, v); mcs_push_root(vm, vs);
            mcs_dict_put(vm, d, ks, vs); mcs_pop_root(vm, 2);
        }
        p = amp + 1;
    }
    mcs_pop_root(vm, 1); return d;
}
static const char* reason(int s) {
    switch (s) {
    case 101: return "Switching Protocols"; case 200: return "OK"; case 201: return "Created"; case 204: return "No Content";
    case 301: return "Moved Permanently"; case 302: return "Found"; case 304: return "Not Modified";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 408: return "Request Timeout"; case 413: return "Payload Too Large";
    case 500: return "Internal Server Error"; case 501: return "Not Implemented"; case 503: return "Service Unavailable";
    default: return s < 300 ? "OK" : s < 400 ? "Redirect" : s < 500 ? "Client Error" : "Server Error";
    }
}
static int send_all(mcs_vm_t* vm, mcs__conn_t* c, const void* p, size_t n) {
    if (c->fd < 0 && !c->tls) return -1;
    int r = mcs__conn_write(vm, c, (const uint8_t*)p, n, MCS_HTTPD_TIMEOUT);
    return r < 0 ? r : 0;
}
/* read exactly n bytes (0 ok, -1 closed / timeout / error) */
static int read_n(mcs_vm_t* vm, mcs__conn_t* c, uint8_t* b, size_t n, uint32_t to) {
    size_t got = 0;
    while (got < n) {
        int r = mcs__conn_read(vm, c, b + got, n - got, to);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

/* ---- SHA-1 + base64 (WebSocket handshake) */
static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static void sha1(const uint8_t* m, size_t len, uint8_t out[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint8_t blk[64]; size_t total = ((len + 8) / 64 + 1) * 64;
    for (size_t off = 0; off < total; off += 64) {
        for (int i = 0; i < 64; i++) {
            size_t k = off + (size_t)i;
            blk[i] = k < len ? m[k] : k == len ? 0x80 : 0;
            if (k >= total - 8) blk[i] = (uint8_t)(((uint64_t)len * 8) >> (8 * (total - 1 - k)));
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 | (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}
static void b64(const uint8_t* p, size_t n, char* out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        out[o++] = T[v >> 18 & 63]; out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}
static void ws_accept_key(const char* key, size_t kl, char out[32]) {
    char buf[128]; static const char G[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    if (kl > 60) kl = 60;
    memcpy(buf, key, kl); memcpy(buf + kl, G, sizeof G - 1);
    uint8_t d[20]; sha1((const uint8_t*)buf, kl + sizeof G - 1, d); b64(d, 20, out);
}
static uint32_t rnd_state = 0x9E3779B9u;
static uint32_t rnd(mcs_vm_t* vm) { rnd_state ^= mcs_ticks(vm) + (uint32_t)(uintptr_t)&rnd_state; rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state; }

/* ============================================================ WebSocket */
typedef struct {
    mcs__conn_t c;
    uint8_t open, server, have_cb;
    int close_code;
    int cb_slot;                   /* kept list [OnMessage, OnClose] */
    uint32_t timeout;
    char path[128];
    char raddr[16];
    int rport;
} ws_t;
static const mcs_class_def_t ws_def;
#define WS() ws_t* w = (ws_t*)mcs_check_userdata(vm, self, &ws_def); if (!w) return mcs_null()
static void ws_drop(mcs_vm_t* vm, ws_t* w) {
    mcs__conn_close(&w->c); w->open = 0;
}
static void ws_fin(mcs_vm_t* vm, void* d) { ws_drop(vm, (ws_t*)d); }
static void ws_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    ws_t* w = (ws_t*)mcs_userdata(self);
    w->c.fd = -1; w->cb_slot = -1; w->timeout = MCS_NET_TIMEOUT; w->close_code = 0;
    if (argc > 0 && !mcs_is_null(argv[0])) {
        mcs_value_t r = mcs_null();
        mcs_invoke(vm, self, "Connect", argc, argv, &r);
    }
}
/* send one frame */
static int ws_frame(mcs_vm_t* vm, ws_t* w, int op, const uint8_t* p, size_t n) {
    uint8_t h[14]; size_t hl = 0;
    h[hl++] = (uint8_t)(0x80 | op);
    uint8_t mask = w->server ? 0 : 0x80;
    if (n < 126) h[hl++] = (uint8_t)(mask | n);
    else if (n < 65536) { h[hl++] = mask | 126; h[hl++] = (uint8_t)(n >> 8); h[hl++] = (uint8_t)n; }
    else { h[hl++] = mask | 127; for (int i = 7; i >= 0; i--) h[hl++] = (uint8_t)((uint64_t)n >> (8 * i)); }
    if (w->server) {
        if (send_all(vm, &w->c, h, hl) < 0) return -1;
        return n ? send_all(vm, &w->c, p, n) : 0;
    }
    uint32_t k = rnd(vm); uint8_t km[4] = { (uint8_t)k, (uint8_t)(k >> 8), (uint8_t)(k >> 16), (uint8_t)(k >> 24) };
    memcpy(h + hl, km, 4); hl += 4;
    if (send_all(vm, &w->c, h, hl) < 0) return -1;
    uint8_t buf[256];
    for (size_t off = 0; off < n;) {
        size_t m = n - off < sizeof buf ? n - off : sizeof buf;
        for (size_t i = 0; i < m; i++) buf[i] = p[off + i] ^ km[(off + i) & 3];
        if (send_all(vm, &w->c, buf, m) < 0) return -1;
        off += m;
    }
    return 0;
}
static void ws_close_cb(mcs_vm_t* vm, ws_t* w, mcs_value_t self);
/* read one message. returns 1 message (*out set), 0 nothing within timeout, -1 closed */
static int ws_read(mcs_vm_t* vm, ws_t* w, mcs_value_t self, uint32_t to, mcs_value_t* out) {
    uint8_t* msg = NULL; size_t ml = 0, mcap = 0; int mop = 0;
    for (;;) {
        if (!w->open) { if (msg) xalloc(vm, msg, mcap, 0); return -1; }
        if (!msg) { int r = w->c.tls ? 1 : mcs__wait_fd(vm, w->c.fd, false, to ? to : 1); if (r <= 0) return 0; }
        uint8_t h[2];
        if (read_n(vm, &w->c, h, 2, MCS_HTTPD_TIMEOUT) < 0) goto dead;
        int op = h[0] & 15, fin = h[0] >> 7, masked = h[1] >> 7;
        uint64_t n = h[1] & 127;
        uint8_t ext[8];
        if (n == 126) { if (read_n(vm, &w->c, ext, 2, MCS_HTTPD_TIMEOUT) < 0) goto dead; n = (uint64_t)ext[0] << 8 | ext[1]; }
        else if (n == 127) { if (read_n(vm, &w->c, ext, 8, MCS_HTTPD_TIMEOUT) < 0) goto dead; n = 0; for (int i = 0; i < 8; i++) n = n << 8 | ext[i]; }
        uint8_t km[4] = { 0 };
        if (masked && read_n(vm, &w->c, km, 4, MCS_HTTPD_TIMEOUT) < 0) goto dead;
        if (ml + n > MCS_WS_MAX_MESSAGE) { uint8_t b[2] = { 1009 >> 8, 1009 & 255 }; ws_frame(vm, w, 8, b, 2); w->close_code = 1009; goto dead; }
        size_t need = ml + (size_t)n + 1;
        if (need > mcap) { uint8_t* nm = (uint8_t*)xalloc(vm, msg, mcap, need); if (!nm) goto dead; msg = nm; mcap = need; }
        if (n && read_n(vm, &w->c, msg + ml, (size_t)n, MCS_HTTPD_TIMEOUT) < 0) goto dead;
        if (masked) for (size_t i = 0; i < n; i++) msg[ml + i] ^= km[i & 3];
        if (op >= 8) {       /* control frame */
            uint8_t* cp = msg + ml; size_t cn = (size_t)n;
            if (op == 9) ws_frame(vm, w, 10, cp, cn);
            else if (op == 8) {
                w->close_code = cn >= 2 ? (cp[0] << 8 | cp[1]) : 1005;
                ws_frame(vm, w, 8, cp, cn >= 2 ? 2 : 0);
                goto dead;
            }
            if (ml == 0) { xalloc(vm, msg, mcap, 0); msg = NULL; mcap = 0; }
            continue;
        }
        if (op) mop = op;
        ml += (size_t)n;
        if (!fin) continue;
        *out = mop == 2 ? mcs__bytes_new(vm, msg, ml) : mcs_string_n(vm, (const char*)msg, ml);
        xalloc(vm, msg, mcap, 0);
        return 1;
    }
dead:
    if (msg) xalloc(vm, msg, mcap, 0);
    if (!w->close_code) w->close_code = 1006;
    ws_drop(vm, w);
    ws_close_cb(vm, w, self);
    return -1;
}
static mcs_value_t ws_cb(mcs_vm_t* vm, ws_t* w, int i) {
    if (w->cb_slot < 0) return mcs_null();
    mcs_value_t l = mcs__kept(vm, w->cb_slot);
    return mcs_is_null(l) ? l : mcs_index(l, (uint32_t)i);
}
static bool call_handler(mcs_vm_t* vm, mcs_value_t fn, int argc, mcs_value_t* argv, mcs_value_t* res, const char* what);
static void ws_close_cb(mcs_vm_t* vm, ws_t* w, mcs_value_t self) {
    mcs_value_t fn = ws_cb(vm, w, 1);
    if (!mcs_is_null(fn)) { mcs_value_t a[2] = { mcs_int(w->close_code), self }; call_handler(vm, fn, 2, a, NULL, "WebSocket.OnClose"); }
    if (w->cb_slot >= 0) { mcs__unkeep(vm, w->cb_slot); w->cb_slot = -1; }
}
static void ws_dispatch(mcs_vm_t* vm, ws_t* w, mcs_value_t self, mcs_value_t msg) {
    mcs_value_t fn = ws_cb(vm, w, 0);
    if (mcs_is_null(fn)) return;
    mcs_push_root(vm, msg);
    mcs_value_t a[2] = { msg, self };
    call_handler(vm, fn, 2, a, NULL, "WebSocket.OnMessage");
    mcs_pop_root(vm, 1);
}
static void ws_set_cb(mcs_vm_t* vm, ws_t* w, int i, mcs_value_t fn) {
    if (w->cb_slot < 0) {
        mcs_value_t l = mcs_new_list(vm); mcs_push_root(vm, l);
        mcs_list_add(vm, l, mcs_null()); mcs_list_add(vm, l, mcs_null());
        w->cb_slot = mcs__keep(vm, l); mcs_pop_root(vm, 1);
        if (w->cb_slot < 0) return;
    }
    mcs_set_index(mcs__kept(vm, w->cb_slot), (uint32_t)i, fn);
}
NATIVE(ws_connect) {   /* Connect(url[, headers[, timeoutMs]]) */
    WS();
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "WebSocket.Connect(url[, headers[, timeoutMs]])"); return mcs_null(); }
    STR_ARG(0, url); OPT_STR(1, extra); OPT_INT(2, to, w->timeout);
    bool tls = false; const char* p = url;
    if (!strncmp(p, "ws://", 5)) p += 5; else if (!strncmp(p, "wss://", 6)) { p += 6; tls = true; }
    else { mcs_raise(vm, "ArgumentException", "WebSocket: url must start with ws:// or wss://"); return mcs_null(); }
    char host[128]; const char* hs = p;
    while (*p && *p != ':' && *p != '/' && *p != '?') p++;
    size_t hl = (size_t)(p - hs); if (!hl || hl >= sizeof host) { mcs_raise(vm, "ArgumentException", "WebSocket: bad host in '%s'", url); return mcs_null(); }
    memcpy(host, hs, hl); host[hl] = 0;
    int port = tls ? 443 : 80;
    if (*p == ':') { port = atoi(p + 1); while (*p && *p != '/' && *p != '?') p++; }
    const char* path = *p ? p : "/";
    snprintf(w->path, sizeof w->path, "%s", path);
    ws_drop(vm, w); w->server = 0; w->close_code = 0;
    if (mcs__conn_open(vm, &w->c, host, port, tls, (uint32_t)to, NULL, "WebSocket.Connect") < 0 || mcs_has_exception(vm)) return mcs_null();
    uint8_t kr[16]; for (int i = 0; i < 16; i++) kr[i] = (uint8_t)rnd(vm);
    char key[32], acc[32]; b64(kr, 16, key); ws_accept_key(key, strlen(key), acc);
    char req[1024];
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n%s%s\r\n", path, host, port, key, extra ? extra : "",
        extra && *extra && extra[strlen(extra) - 1] != '\n' ? "\r\n" : "");
    if (n >= (int)sizeof req || send_all(vm, &w->c, req, (size_t)n) < 0) { mcs__conn_close(&w->c); mcs_raise(vm, "WebSocketException", "WebSocket.Connect: send failed"); return mcs_null(); }
    char resp[1024]; size_t rl = 0;
    while (rl + 1 < sizeof resp) {
        int r = mcs__conn_read(vm, &w->c, (uint8_t*)resp + rl, 1, (uint32_t)to);
        if (r <= 0) break;
        rl++;
        if (rl >= 4 && !memcmp(resp + rl - 4, "\r\n\r\n", 4)) break;
    }
    resp[rl] = 0;
    size_t al; const char* a = hfind(resp, rl, "Sec-WebSocket-Accept", &al);
    if (strncmp(resp, "HTTP/1.1 101", 12) || !a || al != strlen(acc) || memcmp(a, acc, al)) {
        mcs__conn_close(&w->c);
        if (!mcs_has_exception(vm)) mcs_raise(vm, "WebSocketException", "WebSocket.Connect: handshake refused by %s (%.40s)", host, rl ? resp : "no reply");
        return mcs_null();
    }
    w->open = 1;
    snprintf(w->raddr, sizeof w->raddr, "%.15s", host); w->rport = port;
    return mcs_bool(true);
}
#define WOPEN(op) if (!w->open) { mcs_raise(vm, "InvalidOperationException", "%s: the WebSocket is not open", op); return mcs_null(); }
static mcs_value_t ws_send_op(mcs_vm_t* vm, ws_t* w, mcs_value_t v, int op, const char* name) {
    WOPEN(name);
    const uint8_t* p; size_t n; uint8_t* heap = NULL;
    if (mcs_is_string(v)) { p = (const uint8_t*)mcs_cstr(v); n = mcs_len(v); if (!op) op = 1; }
    else { if (!mcs__bytes_arg(vm, v, &p, &n, &heap)) return mcs_null(); if (!op) op = 2; }
    int r = ws_frame(vm, w, op, p, n);
    mcs__bytes_free(vm, heap, n);
    if (r < 0) { ws_drop(vm, w); if (!w->close_code) w->close_code = 1006; if (!mcs_has_exception(vm)) mcs_raise(vm, "WebSocketException", "%s: connection lost", name); }
    return mcs_null();
}
NATIVE(ws_send) { WS(); return ws_send_op(vm, w, argv[0], 0, "WebSocket.Send"); }
NATIVE(ws_sendtext) { WS(); mcs_value_t s = mcs_tostring(vm, argv[0]); CHECK(); return ws_send_op(vm, w, s, 1, "WebSocket.SendText"); }
NATIVE(ws_sendbin) { WS(); return ws_send_op(vm, w, argv[0], 2, "WebSocket.SendBinary"); }
NATIVE(ws_ping) { WS(); WOPEN("WebSocket.Ping"); ws_frame(vm, w, 9, (const uint8_t*)"mcs", 3); return mcs_null(); }
NATIVE(ws_receive) {   /* ([timeoutMs]) -> string | byte[] | null */
    WS(); OPT_INT(0, to, w->timeout);
    mcs_value_t m = mcs_null();
    return ws_read(vm, w, self, (uint32_t)(to < 1 ? 1 : to), &m) == 1 ? m : mcs_null();
}
NATIVE(ws_poll) {   /* dispatch pending messages to OnMessage; returns false once closed */
    WS();
    for (int i = 0; i < 16 && w->open; i++) {
        if (!w->c.tls && mcs__wait_fd(vm, w->c.fd, false, 1) <= 0) break;
        mcs_value_t m = mcs_null();
        if (ws_read(vm, w, self, 1, &m) != 1) break;
        ws_dispatch(vm, w, self, m);
        if (mcs_has_exception(vm)) return mcs_null();
    }
    return mcs_bool(w->open);
}
NATIVE(ws_close) {   /* ([code[, reason]]) */
    WS(); OPT_INT(0, code, 1000); OPT_STR(1, why);
    if (w->open) {
        uint8_t b[125]; b[0] = (uint8_t)(code >> 8); b[1] = (uint8_t)code;
        size_t n = 2; if (why) { size_t l = strlen(why); if (l > 120) l = 120; memcpy(b + 2, why, l); n += l; }
        ws_frame(vm, w, 8, b, n);
        w->close_code = (int)code;
        ws_drop(vm, w);
        ws_close_cb(vm, w, self);
    }
    return mcs_null();
}
NATIVE(ws_isopen) { WS(); return mcs_bool(w->open); }
NATIVE(ws_code) { WS(); return mcs_int(w->close_code); }
NATIVE(ws_path) { WS(); return mcs_string(vm, w->path); }
NATIVE(ws_raddr) { WS(); return mcs_string(vm, w->raddr); }
NATIVE(ws_rport) { WS(); return mcs_int(w->rport); }
NATIVE(ws_gto) { WS(); return mcs_int((mcs_int_t)w->timeout); }
NATIVE(ws_sto) { WS(); INT_ARG(0, t); w->timeout = (uint32_t)(t < 0 ? 0 : t); return mcs_null(); }
NATIVE(ws_gmsg) { WS(); return ws_cb(vm, w, 0); }
NATIVE(ws_smsg) { WS(); ws_set_cb(vm, w, 0, argv[0]); return mcs_null(); }
NATIVE(ws_gcls) { WS(); return ws_cb(vm, w, 1); }
NATIVE(ws_scls) { WS(); ws_set_cb(vm, w, 1, argv[0]); return mcs_null(); }
static const mcs_reg_t ws_members[] = {
    MCS_FN("Connect", ws_connect, -1), MCS_FN("Send", ws_send, 1), MCS_FN("SendText", ws_sendtext, 1), MCS_FN("SendBinary", ws_sendbin, 1),
    MCS_FN("Receive", ws_receive, -1), MCS_FN("Poll", ws_poll, 0), MCS_FN("Ping", ws_ping, 0), MCS_FN("Close", ws_close, -1),
    MCS_FN("Dispose", ws_close, -1), MCS_GET("IsOpen", ws_isopen), MCS_GET("CloseCode", ws_code), MCS_GET("Path", ws_path),
    MCS_GET("RemoteAddress", ws_raddr), MCS_GET("RemotePort", ws_rport), MCS_GET("Timeout", ws_gto), MCS_SET("Timeout", ws_sto),
    MCS_GET("OnMessage", ws_gmsg), MCS_SET("OnMessage", ws_smsg), MCS_GET("OnClose", ws_gcls), MCS_SET("OnClose", ws_scls), MCS_REG_END };
static const mcs_class_def_t ws_def = { "WebSocket", sizeof(ws_t), ws_ctor, ws_fin, ws_members, NULL };

/* ============================================================ DnsServer (answers every A query with one address) */
typedef struct { int fd; uint16_t port; uint8_t ip[4]; uint32_t ttl, queries; char domain[64]; int self_slot; } dns_t;
static const mcs_class_def_t dnss_def;
#define DS() dns_t* d = (dns_t*)mcs_check_userdata(vm, self, &dnss_def); if (!d) return mcs_null()
#define MAX_DNS 2
static dns_t* g_dns[MAX_DNS];
static void dns_unreg(dns_t* d) { for (int i = 0; i < MAX_DNS; i++) if (g_dns[i] == d) g_dns[i] = NULL; }
static void dnss_fin(mcs_vm_t* vm, void* p) { dns_t* d = (dns_t*)p; if (d->fd >= 0) close(d->fd); d->fd = -1; dns_unreg(d); }
static void dnss_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    dns_t* d = (dns_t*)mcs_userdata(self); d->fd = -1; d->port = 53; d->ttl = 60; d->self_slot = -1;
    d->ip[0] = 192; d->ip[1] = 168; d->ip[2] = 4; d->ip[3] = 1;
    if (argc > 0 && !mcs_is_null(argv[0])) { const char* s = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return; snprintf(d->domain, sizeof d->domain, "%s", s); }
}
static void dns_answer(dns_t* d) {
    uint8_t q[512]; struct sockaddr_in from; socklen_t fl = sizeof from;
    int n = (int)recvfrom(d->fd, (char*)q, sizeof q, 0, (struct sockaddr*)&from, &fl);
    if (n < 17 || (q[2] & 0x80)) return;
    int i = 12;                                   /* walk the question name */
    char name[128]; size_t nl = 0;
    while (i < n && q[i]) { int l = q[i++]; if (i + l > n) return; if (nl) name[nl < sizeof name - 1 ? nl++ : nl] = '.'; for (int k = 0; k < l; k++, i++) if (nl < sizeof name - 1) name[nl++] = (char)tolower(q[i]); }
    name[nl] = 0;
    i++; if (i + 4 > n) return;
    int qtype = q[i] << 8 | q[i + 1];
    int end = i + 4;
    d->queries++;
    bool match = !d->domain[0] || !strcmp(name, d->domain);
    uint8_t r[560]; memcpy(r, q, (size_t)end);
    r[2] = 0x84 | (q[2] & 1); r[3] = match ? 0x80 : 0x83;    /* response, AA, RD copied; NXDOMAIN on mismatch */
    r[4] = 0; r[5] = 1; r[6] = 0; r[7] = 0; r[8] = r[9] = r[10] = r[11] = 0;
    int o = end;
    if (match && qtype == 1) {
        r[7] = 1;
        uint8_t a[16] = { 0xC0, 0x0C, 0, 1, 0, 1, (uint8_t)(d->ttl >> 24), (uint8_t)(d->ttl >> 16), (uint8_t)(d->ttl >> 8), (uint8_t)d->ttl, 0, 4, d->ip[0], d->ip[1], d->ip[2], d->ip[3] };
        memcpy(r + o, a, 16); o += 16;
    } else if (match) r[3] = 0x80;               /* no AAAA etc.: empty answer */
    sendto(d->fd, (const char*)r, (size_t)o, 0, (struct sockaddr*)&from, fl);
}
static void dns_poll_all(void) {
    for (int k = 0; k < MAX_DNS; k++) {
        dns_t* d = g_dns[k]; if (!d || d->fd < 0) continue;
        for (int j = 0; j < 8; j++) {
            fd_set s; FD_ZERO(&s); FD_SET(d->fd, &s); struct timeval tv = { 0, 0 };
            if (select(d->fd + 1, &s, NULL, NULL, &tv) <= 0) break;
            dns_answer(d);
        }
    }
}
NATIVE(dnss_start) {   /* Start([ip[, port]]) - ip defaults to the soft-AP address */
    DS(); OPT_STR(0, ip); OPT_INT(1, port, d->port);
    if (ip && !mcs__ip_parse(ip, d->ip)) { mcs_raise(vm, "ArgumentException", "DnsServer.Start: '%s' is not an IPv4 address", ip); return mcs_null(); }
    if (!ip) {
        mcs__net_state_t* s = mcs__net_state(vm); mcs_net_ip_t ni;
        if (s && s->net && s->net->ip_get && s->net->ip_get(s->net->ctx, MCS_NET_IF_AP, &ni) >= 0 && ni.ip[0]) memcpy(d->ip, ni.ip, 4);
    }
    if (d->fd >= 0) return mcs_null();
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return mcs__sock_fail(vm, "DnsServer.Start", errno);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&a, sizeof a) < 0) { int e = errno; close(fd); return mcs__sock_fail(vm, "DnsServer.Start", e); }
    socklen_t al = sizeof a; if (getsockname(fd, (struct sockaddr*)&a, &al) == 0) port = ntohs(a.sin_port);
    mcs__set_nonblock(fd);
    d->fd = fd; d->port = (uint16_t)port;
    for (int i = 0; i < MAX_DNS; i++) if (!g_dns[i]) { g_dns[i] = d; break; }
    if (d->self_slot < 0) d->self_slot = mcs__keep(vm, self);
    return mcs_null();
}
NATIVE(dnss_stop) { DS(); if (d->fd >= 0) close(d->fd); d->fd = -1; dns_unreg(d); if (d->self_slot >= 0) { mcs__unkeep(vm, d->self_slot); d->self_slot = -1; } return mcs_null(); }
NATIVE(dnss_handle) {   /* Handle([ms]) - answer queries for up to ms */
    DS(); OPT_INT(0, ms, 0);
    if (d->fd < 0) return mcs_null();
    if (ms > 0 && mcs__wait_fd(vm, d->fd, false, (uint32_t)ms) <= 0) return mcs_null();
    dns_poll_all();
    return mcs_null();
}
NATIVE(dnss_active) { DS(); return mcs_bool(d->fd >= 0); }
NATIVE(dnss_port) { DS(); return mcs_int(d->port); }
NATIVE(dnss_q) { DS(); return mcs_int((mcs_int_t)d->queries); }
NATIVE(dnss_gttl) { DS(); return mcs_int((mcs_int_t)d->ttl); }
NATIVE(dnss_sttl) { DS(); INT_ARG(0, t); d->ttl = (uint32_t)(t < 0 ? 0 : t); return mcs_null(); }
NATIVE(dnss_gip) { DS(); char b[16]; mcs__ip_fmt(d->ip, b); return mcs_string(vm, b); }
NATIVE(dnss_sip) { DS(); STR_ARG(0, s); if (!mcs__ip_parse(s, d->ip)) mcs_raise(vm, "ArgumentException", "DnsServer.Ip: '%s' is not an IPv4 address", s); return mcs_null(); }
NATIVE(dnss_gdom) { DS(); return mcs_string(vm, d->domain); }
NATIVE(dnss_sdom) { DS(); OPT_STR(0, s); snprintf(d->domain, sizeof d->domain, "%s", s ? s : ""); for (char* p = d->domain; *p; p++) *p = (char)tolower((unsigned char)*p); return mcs_null(); }
static const mcs_reg_t dnss_members[] = {
    MCS_FN("Start", dnss_start, -1), MCS_FN("Stop", dnss_stop, 0), MCS_FN("Handle", dnss_handle, -1), MCS_FN("Dispose", dnss_stop, 0),
    MCS_GET("Active", dnss_active), MCS_GET("Port", dnss_port), MCS_GET("Queries", dnss_q), MCS_GET("Ttl", dnss_gttl), MCS_SET("Ttl", dnss_sttl),
    MCS_GET("Ip", dnss_gip), MCS_SET("Ip", dnss_sip), MCS_GET("Domain", dnss_gdom), MCS_SET("Domain", dnss_sdom), MCS_REG_END };
static const mcs_class_def_t dnss_def = { "DnsServer", sizeof(dns_t), dnss_ctor, dnss_fin, dnss_members, NULL };

/* ============================================================ HttpRequest */
typedef struct {
    mcs__conn_t c;
    char method[8];
    char* raw; size_t raw_cap;          /* header block (request line + headers) */
    size_t hdr_off, hdr_len;            /* header lines inside raw */
    char path[256];
    const char* query; size_t qlen;     /* into raw */
    char* body; size_t body_len, body_cap;
    char params[256]; size_t plen;      /* route params "k=v&k=v" */
    char extra[512]; size_t elen;       /* response headers from SetHeader */
    uint8_t sent, cors;
    int status;
    char raddr[16]; int rport;
} req_t;
static const mcs_class_def_t req_def;
#define RQ() req_t* q = (req_t*)mcs_check_userdata(vm, self, &req_def); if (!q) return mcs_null()
static void req_free(mcs_vm_t* vm, req_t* q) {
    if (q->raw) xalloc(vm, q->raw, q->raw_cap, 0);
    if (q->body) xalloc(vm, q->body, q->body_cap, 0);
    q->raw = q->body = NULL; q->query = NULL;
    mcs__conn_close(&q->c);
}
static void req_fin(mcs_vm_t* vm, void* d) { req_free(vm, (req_t*)d); }
static void req_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    req_t* q = (req_t*)mcs_userdata(self); q->c.fd = -1;
    if (argc) mcs_raise(vm, "InvalidOperationException", "HttpRequest objects are passed to HttpServer handlers");
}
static int req_head(mcs_vm_t* vm, req_t* q, int status, const char* ctype, size_t n, const char* more) {
    q->sent = 1; q->status = status;
    char h[1024];
    int hl = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n%s%.*s%s\r\n",
        status, reason(status), ctype ? ctype : "text/plain", (unsigned)n,
        q->cors ? "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: *\r\nAccess-Control-Allow-Methods: *\r\n" : "",
        (int)q->elen, q->extra, more ? more : "");
    if (hl >= (int)sizeof h) hl = (int)sizeof h - 1;
    return send_all(vm, &q->c, h, (size_t)hl);
}
static int req_send(mcs_vm_t* vm, req_t* q, int status, const char* ctype, const uint8_t* body, size_t n, const char* more) {
    if (q->sent) return 0;
    if (req_head(vm, q, status, ctype, n, more) < 0) return -1;
    if (n && strcmp(q->method, "HEAD") && send_all(vm, &q->c, body, n) < 0) return -1;
    return 0;
}
#define NOTSENT(op) if (q->sent) { mcs_raise(vm, "InvalidOperationException", "%s: the response was already sent", op); return mcs_null(); }
static const char* auto_type(const char* s, size_t n) {
    while (n && isspace((unsigned char)*s)) { s++; n--; }
    if (n && *s == '<') return "text/html; charset=utf-8";
    if (n && (*s == '{' || *s == '[')) return "application/json";
    return "text/plain; charset=utf-8";
}
NATIVE(rq_send) {   /* Send([status,] body[, contentType]) */
    RQ(); NOTSENT("HttpRequest.Send");
    int i = 0; mcs_int_t st = 200;
    if (argc >= 1 && mcs_is_int(argv[0]) && (argc >= 2 || mcs_to_int(vm, argv[0]) >= 100)) { st = mcs_to_int(vm, argv[0]); i = 1; }
    if (argc > 3 || argc < 1) { mcs_raise(vm, "ArgumentException", "Send([status,] body[, contentType])"); return mcs_null(); }
    const uint8_t* p = (const uint8_t*)""; size_t n = 0; uint8_t* heap = NULL; const char* ct = NULL;
    if (i < argc && !mcs_is_null(argv[i])) {
        if (mcs_is_string(argv[i])) { p = (const uint8_t*)mcs_cstr(argv[i]); n = mcs_len(argv[i]); ct = auto_type((const char*)p, n); }
        else if (!mcs__bytes_arg(vm, argv[i], &p, &n, &heap)) return mcs_null();
        else ct = "application/octet-stream";
    }
    if (i + 1 < argc) { ct = mcs_to_cstr(vm, argv[i + 1]); CHECK(); }
    req_send(vm, q, (int)st, ct, p, n, NULL);
    mcs__bytes_free(vm, heap, n);
    return mcs_null();
}
NATIVE(rq_sendjson) {   /* SendJson(value[, status]) */
    RQ(); NOTSENT("HttpRequest.SendJson"); OPT_INT(1, st, 200);
    mcs_value_t s = mcs_json_stringify(vm, argv[0], 0); CHECK();
    req_send(vm, q, (int)st, "application/json", (const uint8_t*)mcs_cstr(s), mcs_len(s), NULL);
    return mcs_null();
}
NATIVE(rq_redirect) {   /* Redirect(url[, status]) */
    RQ(); NOTSENT("HttpRequest.Redirect"); STR_ARG(0, url); OPT_INT(1, st, 302);
    char more[512]; snprintf(more, sizeof more, "Location: %s\r\n", url);
    req_send(vm, q, (int)st, "text/plain", NULL, 0, more);
    return mcs_null();
}
NATIVE(rq_sethdr) {
    RQ(); STR_ARG(0, k); STR_ARG(1, v);
    int n = snprintf(q->extra + q->elen, sizeof q->extra - q->elen, "%s: %s\r\n", k, v);
    if (n > 0 && q->elen + (size_t)n < sizeof q->extra) q->elen += (size_t)n;
    else { q->extra[q->elen] = 0; mcs_raise(vm, "InvalidOperationException", "HttpRequest.SetHeader: too many headers"); }
    return mcs_null();
}
#if MCS_ENABLE_FS
static bool has_gzip(const char* s, size_t n) { for (size_t i = 0; i + 4 <= n; i++) if (ci_eq(s + i, "gzip", 4)) return true; return false; }
static const char* mime_of(const char* path) {
    static const char* const T[] = { ".html", "text/html; charset=utf-8", ".htm", "text/html; charset=utf-8", ".css", "text/css",
        ".js", "application/javascript", ".json", "application/json", ".png", "image/png", ".jpg", "image/jpeg", ".jpeg", "image/jpeg",
        ".gif", "image/gif", ".svg", "image/svg+xml", ".ico", "image/x-icon", ".txt", "text/plain; charset=utf-8", ".wasm", "application/wasm",
        ".woff2", "font/woff2", ".bin", "application/octet-stream", NULL };
    size_t pl = strlen(path);
    for (int i = 0; T[i]; i += 2) { size_t el = strlen(T[i]); if (pl >= el && ci_eq(path + pl - el, T[i], el)) return T[i + 1]; }
    return "application/octet-stream";
}
/* stream a file; 0 sent, -1 not found */
static int send_file(mcs_vm_t* vm, req_t* q, const char* path, const char* ctype, int max_age) {
    mcs_vfs_t* vfs = (mcs_vfs_t*)mcs_get_ext(vm, MCS_EXT_VFS);
    if (!vfs) return -1;
    char gz[300]; mcs_vfs_file_t f; bool gzip = false;
    size_t al; const char* ae = q->raw ? hfind(q->raw + q->hdr_off, q->hdr_len, "Accept-Encoding", &al) : NULL;
    snprintf(gz, sizeof gz, "%s.gz", path);
    if (ae && has_gzip(ae, al) && mcs_vfs_open(vfs, gz, MCS_VFS_READ, &f) >= 0) gzip = true;
    else if (mcs_vfs_open(vfs, path, MCS_VFS_READ, &f) < 0) return -1;
    size_t total = 0; uint8_t buf[512]; int r;
    while ((r = mcs_vfs_read(&f, buf, sizeof buf)) > 0) total += (size_t)r;
    mcs_vfs_close(&f);
    if (mcs_vfs_open(vfs, gzip ? gz : path, MCS_VFS_READ, &f) < 0) return -1;
    char more[96]; int ml = 0;
    if (gzip) ml += snprintf(more + ml, sizeof more - (size_t)ml, "Content-Encoding: gzip\r\n");
    if (max_age > 0) ml += snprintf(more + ml, sizeof more - (size_t)ml, "Cache-Control: max-age=%d\r\n", max_age);
    more[ml] = 0;
    if (req_head(vm, q, 200, ctype ? ctype : mime_of(path), total, more) >= 0 && strcmp(q->method, "HEAD"))
        while ((r = mcs_vfs_read(&f, buf, sizeof buf)) > 0) if (send_all(vm, &q->c, buf, (size_t)r) < 0) break;
    mcs_vfs_close(&f);
    return 0;
}
#endif
NATIVE(rq_sendfile) {   /* SendFile(path[, contentType]) */
    RQ(); NOTSENT("HttpRequest.SendFile"); STR_ARG(0, path); OPT_STR(1, ct);
#if MCS_ENABLE_FS
    if (send_file(vm, q, path, ct, 0) < 0) req_send(vm, q, 404, "text/plain", (const uint8_t*)"Not Found", 9, NULL);
#else
    (void)path; (void)ct; mcs_raise(vm, "NotSupportedException", "HttpRequest.SendFile needs MCS_ENABLE_FS");
#endif
    return mcs_null();
}
NATIVE(rq_method) { RQ(); return mcs_string(vm, q->method); }
NATIVE(rq_path) { RQ(); return mcs_string(vm, q->path); }
NATIVE(rq_qs) { RQ(); return mcs_string_n(vm, q->query ? q->query : "", q->query ? q->qlen : 0); }
NATIVE(rq_url) { RQ(); char b[600]; snprintf(b, sizeof b, "%s%s%.*s", q->path, q->query ? "?" : "", q->query ? (int)q->qlen : 0, q->query ? q->query : ""); return mcs_string(vm, b); }
NATIVE(rq_query) { RQ(); return qdict(vm, q->query, q->qlen); }
static bool is_form(req_t* q) {
    size_t vl; const char* v = q->raw ? hfind(q->raw + q->hdr_off, q->hdr_len, "Content-Type", &vl) : NULL;
    return v && vl >= 33 && ci_eq(v, "application/x-www-form-urlencoded", 33);
}
NATIVE(rq_form) { RQ(); return is_form(q) ? qdict(vm, q->body, q->body_len) : mcs_new_dictionary(vm); }
static bool param_find(req_t* q, const char* name, char* out, size_t cap) {
    return qfind(q->params, q->plen, name, out, cap) || qfind(q->query, q->qlen, name, out, cap) ||
           (is_form(q) && qfind(q->body, q->body_len, name, out, cap));
}
NATIVE(rq_param) {   /* Param(name[, default]) - route {param}, then query string, then form body */
    RQ(); STR_ARG(0, name);
    char v[512];
    if (param_find(q, name, v, sizeof v)) return mcs_string(vm, v);
    return argc > 1 ? argv[1] : mcs_null();
}
NATIVE(rq_hasparam) { RQ(); STR_ARG(0, name); char v[8]; return mcs_bool(param_find(q, name, v, sizeof v)); }
NATIVE(rq_headers) {
    RQ(); mcs_value_t d = mcs_new_dictionary(vm); mcs_push_root(vm, d);
    const char* p = q->raw ? q->raw + q->hdr_off : NULL; const char* end = p ? p + q->hdr_len : NULL;
    while (p && p < end) {
        const char* eol = memchr(p, '\n', (size_t)(end - p)); if (!eol) eol = end;
        const char* c = memchr(p, ':', (size_t)(eol - p));
        if (c) {
            const char* v = c + 1; while (v < eol && *v == ' ') v++;
            const char* ve = eol; while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
            mcs_value_t k = mcs_string_n(vm, p, (size_t)(c - p)); mcs_push_root(vm, k);
            mcs_value_t vv = mcs_string_n(vm, v, (size_t)(ve - v)); mcs_push_root(vm, vv);
            mcs_dict_put(vm, d, k, vv); mcs_pop_root(vm, 2);
        }
        p = eol + 1;
    }
    mcs_pop_root(vm, 1); return d;
}
NATIVE(rq_header) { RQ(); STR_ARG(0, n); size_t vl; const char* v = q->raw ? hfind(q->raw + q->hdr_off, q->hdr_len, n, &vl) : NULL; return v ? mcs_string_n(vm, v, vl) : mcs_null(); }
NATIVE(rq_ctype) { RQ(); size_t vl; const char* v = q->raw ? hfind(q->raw + q->hdr_off, q->hdr_len, "Content-Type", &vl) : NULL; return mcs_string_n(vm, v ? v : "", v ? vl : 0); }
NATIVE(rq_body) { RQ(); return mcs_string_n(vm, q->body ? q->body : "", q->body_len); }
NATIVE(rq_bbytes) { RQ(); return mcs__bytes_new(vm, (const uint8_t*)q->body, q->body_len); }
NATIVE(rq_json) { RQ(); mcs_value_t s = mcs_string_n(vm, q->body ? q->body : "", q->body_len); mcs_push_root(vm, s); mcs_value_t r = mcs_json_parse(vm, s); mcs_pop_root(vm, 1); return r; }
NATIVE(rq_raddr) { RQ(); return mcs_string(vm, q->raddr); }
NATIVE(rq_rport) { RQ(); return mcs_int(q->rport); }
NATIVE(rq_sent) { RQ(); return mcs_bool(q->sent); }
NATIVE(rq_status) { RQ(); return mcs_int(q->status); }
NATIVE(rq_tostring) { RQ(); char b[300]; snprintf(b, sizeof b, "%s %s", q->method, q->path); return mcs_string(vm, b); }
static const mcs_reg_t req_members[] = {
    MCS_GET("Method", rq_method), MCS_GET("Path", rq_path), MCS_GET("Url", rq_url), MCS_GET("QueryString", rq_qs),
    MCS_GET("Query", rq_query), MCS_GET("Form", rq_form), MCS_FN("Param", rq_param, -1), MCS_FN("HasParam", rq_hasparam, 1),
    MCS_GET("Headers", rq_headers), MCS_FN("Header", rq_header, 1), MCS_GET("ContentType", rq_ctype), MCS_GET("Body", rq_body),
    MCS_GET("BodyBytes", rq_bbytes), MCS_FN("Json", rq_json, 0), MCS_GET("RemoteAddress", rq_raddr), MCS_GET("RemotePort", rq_rport),
    MCS_FN("Send", rq_send, -1), MCS_FN("SendJson", rq_sendjson, -1), MCS_FN("SendFile", rq_sendfile, -1), MCS_FN("Redirect", rq_redirect, -1),
    MCS_FN("SetHeader", rq_sethdr, 2), MCS_GET("Sent", rq_sent), MCS_GET("Status", rq_status), MCS_FN("ToString", rq_tostring, 0), MCS_REG_END };
static const mcs_class_def_t req_def = { "HttpRequest", sizeof(req_t), req_ctor, req_fin, req_members, NULL };

/* ============================================================ HttpServer */
enum { RT_HTTP, RT_WS, RT_404 };
typedef struct { char method[8]; char pat[96]; uint8_t kind; } route_t;
typedef struct { char prefix[48]; char folder[64]; int max_age; } static_t;
#define MAX_STATIC 4
typedef struct {
    int fd; uint8_t ip[4]; uint16_t port;
    route_t* routes; int nroutes, rcap;
    static_t st[MAX_STATIC]; int nst;
    int h_slot, c_slot;            /* kept lists: handlers (parallel to routes), WebSocket clients */
    uint32_t requests, timeout, max_body;
    uint8_t cors;
} srv_t;
static const mcs_class_def_t srv_def;
#define SV() srv_t* sv = (srv_t*)mcs_check_userdata(vm, self, &srv_def); if (!sv) return mcs_null()
static void srv_fin(mcs_vm_t* vm, void* d) {
    srv_t* sv = (srv_t*)d;
    if (sv->fd >= 0) close(sv->fd);
    sv->fd = -1;
    if (sv->routes) xalloc(vm, sv->routes, sizeof(route_t) * (size_t)sv->rcap, 0);
    sv->routes = NULL;
}
static void srv_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    srv_t* sv = (srv_t*)mcs_userdata(self);
    sv->fd = -1; sv->port = 80; sv->h_slot = sv->c_slot = -1; sv->timeout = MCS_HTTPD_TIMEOUT; sv->max_body = MCS_HTTPD_MAX_BODY;
    if (argc > 2) { mcs_raise(vm, "ArgumentException", "new HttpServer([port]) or new HttpServer(address, port)"); return; }
    mcs_int_t port = 80;
    if (argc == 2) {
        const char* a = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return;
        if (!mcs__ip_parse(a, sv->ip)) { mcs_raise(vm, "ArgumentException", "HttpServer: '%s' is not an IPv4 address", a); return; }
        port = mcs_to_int(vm, argv[1]);
    } else if (argc == 1) port = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return;
    if (port < 0 || port > 65535) { mcs_raise(vm, "ArgumentOutOfRangeException", "HttpServer: port %d", (int)port); return; }
    sv->port = (uint16_t)port;
    mcs_value_t l = mcs_new_list(vm); mcs_push_root(vm, l); sv->h_slot = mcs__keep(vm, l); mcs_pop_root(vm, 1);
    l = mcs_new_list(vm); mcs_push_root(vm, l); sv->c_slot = mcs__keep(vm, l); mcs_pop_root(vm, 1);
}
static mcs_value_t add_route(mcs_vm_t* vm, srv_t* sv, mcs_value_t self, const char* method, const char* pat, mcs_value_t fn, int kind) {
    if (sv->h_slot < 0) return mcs_null();
    if (strlen(pat) >= sizeof sv->routes[0].pat) { mcs_raise(vm, "ArgumentException", "HttpServer: route '%s' is too long", pat); return mcs_null(); }
    if (sv->nroutes == sv->rcap) {
        int nc = sv->rcap ? sv->rcap * 2 : 8;
        route_t* r = (route_t*)xalloc(vm, sv->routes, sizeof(route_t) * (size_t)sv->rcap, sizeof(route_t) * (size_t)nc);
        if (!r) { mcs_raise(vm, "OutOfMemoryException", "HttpServer: routes"); return mcs_null(); }
        sv->routes = r; sv->rcap = nc;
    }
    route_t* r = &sv->routes[sv->nroutes];
    snprintf(r->method, sizeof r->method, "%s", method); snprintf(r->pat, sizeof r->pat, "%s", pat); r->kind = (uint8_t)kind;
    mcs_list_add(vm, mcs__kept(vm, sv->h_slot), fn); CHECK();
    sv->nroutes++;
    return self;
}
#define ROUTE(fname, m) NATIVE(fname) { SV(); STR_ARG(0, p); return add_route(vm, sv, self, m, p, argv[1], RT_HTTP); }
ROUTE(sv_get, "GET") ROUTE(sv_post, "POST") ROUTE(sv_put, "PUT") ROUTE(sv_patch, "PATCH") ROUTE(sv_delete, "DELETE") ROUTE(sv_any, "*")
NATIVE(sv_on) { SV(); STR_ARG(0, m); STR_ARG(1, p); char mu[8]; size_t i = 0; for (; m[i] && i < 7; i++) mu[i] = (char)toupper((unsigned char)m[i]); mu[i] = 0; return add_route(vm, sv, self, mu, p, argv[2], RT_HTTP); }
NATIVE(sv_notfound) { SV(); return add_route(vm, sv, self, "*", "", argv[0], RT_404); }
NATIVE(sv_ws) { SV(); STR_ARG(0, p); return add_route(vm, sv, self, "GET", p, argv[1], RT_WS); }
NATIVE(sv_static) {   /* Static(prefix, folder[, maxAgeSeconds]) */
    SV(); STR_ARG(0, pre); STR_ARG(1, dir); OPT_INT(2, age, 0);
    if (sv->nst >= MAX_STATIC) { mcs_raise(vm, "InvalidOperationException", "HttpServer.Static: at most %d folders", MAX_STATIC); return mcs_null(); }
    static_t* s = &sv->st[sv->nst++];
    snprintf(s->prefix, sizeof s->prefix, "%s", pre); snprintf(s->folder, sizeof s->folder, "%s", dir); s->max_age = (int)age;
    size_t pl = strlen(s->prefix); if (pl > 1 && s->prefix[pl - 1] == '/') s->prefix[pl - 1] = 0;
    size_t fl = strlen(s->folder); if (fl > 1 && s->folder[fl - 1] == '/') s->folder[fl - 1] = 0;
    return self;
}
/* match "/a/{id}/b" or "/files/" + "*"; route params appended to q->params */
static bool match(const char* pat, const char* path, req_t* q) {
    size_t p0 = q->plen;
    while (*pat && *path) {
        if (*pat == '*' && !pat[1]) {
            int n = snprintf(q->params + q->plen, sizeof q->params - q->plen, "%s*=%s", q->plen ? "&" : "", path);
            if (n > 0 && q->plen + (size_t)n < sizeof q->params) q->plen += (size_t)n;
            return true;
        }
        if (*pat == '{') {
            const char* e = strchr(pat, '}'); if (!e) break;
            const char* se = path; while (*se && *se != '/') se++;
            if (se == path) break;
            int n = snprintf(q->params + q->plen, sizeof q->params - q->plen, "%s%.*s=%.*s", q->plen ? "&" : "", (int)(e - pat - 1), pat + 1, (int)(se - path), path);
            if (n > 0 && q->plen + (size_t)n < sizeof q->params) q->plen += (size_t)n;
            pat = e + 1; path = se; continue;
        }
        if (*pat != *path) break;
        pat++; path++;
    }
    if ((!*pat && !*path) || (*pat == '*' && !pat[1] && !*path)) return true;
    q->plen = p0; q->params[p0] = 0;
    return false;
}
static bool call_handler(mcs_vm_t* vm, mcs_value_t fn, int argc, mcs_value_t* argv, mcs_value_t* res, const char* what) {
    int a = mcs_arity(fn);
    if (a >= 0 && a < argc) argc = a;
    mcs_value_t r = mcs_null();
    mcs_result_t rc = mcs_call_value(vm, fn, argc, argv, &r);
    if (rc == MCS_OK) { if (res) *res = r; return true; }
    char m[200];
    if (mcs_take_exception(vm, m, sizeof m)) mcs_fail(vm, MCS_OK, "%s: %s", what, m);
    return false;
}
static void respond_value(mcs_vm_t* vm, req_t* q, mcs_value_t r) {
    if (q->sent) return;
    if (mcs_is_null(r)) { req_send(vm, q, 200, "text/plain", NULL, 0, NULL); return; }
    if (mcs_is_string(r)) { const char* s = mcs_cstr(r); req_send(vm, q, 200, auto_type(s, mcs_len(r)), (const uint8_t*)s, mcs_len(r), NULL); return; }
    if (mcs_is_int(r) && r.as.i >= 100 && r.as.i <= 599) { const char* t = reason((int)r.as.i); req_send(vm, q, (int)r.as.i, "text/plain", (const uint8_t*)t, strlen(t), NULL); return; }
    if (mcs_is_bool(r) || mcs_is_number(r)) { mcs_value_t s = mcs_tostring(vm, r); if (mcs_has_exception(vm)) return; req_send(vm, q, 200, "text/plain", (const uint8_t*)mcs_cstr(s), mcs_len(s), NULL); return; }
    mcs_push_root(vm, r);
    mcs_value_t s = mcs_json_stringify(vm, r, 0);
    mcs_pop_root(vm, 1);
    if (mcs_has_exception(vm) || !mcs_is_string(s)) { char m[120]; mcs_take_exception(vm, m, sizeof m); req_send(vm, q, 500, "text/plain", (const uint8_t*)"Internal Server Error", 21, NULL); return; }
    req_send(vm, q, 200, "application/json", (const uint8_t*)mcs_cstr(s), mcs_len(s), NULL);
}
static void send_status(mcs_vm_t* vm, req_t* q, int st) { const char* t = reason(st); req_send(vm, q, st, "text/plain", (const uint8_t*)t, strlen(t), NULL); }
/* read request into q; 0 ok, else the HTTP status to answer with (-1 = just drop) */
static int read_request(mcs_vm_t* vm, srv_t* sv, req_t* q) {
    q->raw_cap = MCS_HTTPD_MAX_HEADER + 1;
    q->raw = (char*)xalloc(vm, NULL, 0, q->raw_cap);
    if (!q->raw) { mcs_take_exception(vm, NULL, 0); return 503; }
    size_t n = 0; char* he = NULL;
    while (!he) {
        if (n >= MCS_HTTPD_MAX_HEADER) return 413;
        int r = mcs__conn_read(vm, &q->c, (uint8_t*)q->raw + n, MCS_HTTPD_MAX_HEADER - n, sv->timeout);
        if (r <= 0) return n ? 408 : -1;
        n += (size_t)r; q->raw[n] = 0;
        he = strstr(q->raw, "\r\n\r\n");
    }
    size_t hend = (size_t)(he - q->raw) + 4;
    char* sp1 = strchr(q->raw, ' '); if (!sp1 || sp1 - q->raw >= 8) return 400;
    memcpy(q->method, q->raw, (size_t)(sp1 - q->raw)); q->method[sp1 - q->raw] = 0;
    char* target = sp1 + 1; char* sp2 = strchr(target, ' '); if (!sp2) return 400;
    char* qm = memchr(target, '?', (size_t)(sp2 - target));
    char* pe = qm ? qm : sp2;
    udec(target, (size_t)(pe - target), q->path, sizeof q->path);
    for (char* c = q->path; *c; c++) if (*c == '+') { /* '+' only means space in queries */ }
    if (qm) { q->query = qm + 1; q->qlen = (size_t)(sp2 - qm - 1); }
    char* eol = strstr(q->raw, "\r\n");
    q->hdr_off = (size_t)(eol - q->raw) + 2; q->hdr_len = hend - 2 - q->hdr_off;
    size_t cl_l; const char* cl = hfind(q->raw + q->hdr_off, q->hdr_len, "Content-Length", &cl_l);
    size_t blen = cl ? (size_t)strtoul(cl, NULL, 10) : 0;
    if (blen > sv->max_body) return 413;
    if (blen) {
        q->body_cap = blen + 1;
        q->body = (char*)xalloc(vm, NULL, 0, q->body_cap);
        if (!q->body) { mcs_take_exception(vm, NULL, 0); return 503; }
        size_t have = n - hend; if (have > blen) have = blen;
        memcpy(q->body, q->raw + hend, have);
        if (have < blen && read_n(vm, &q->c, (uint8_t*)q->body + have, blen - have, sv->timeout) < 0) return 408;
        q->body[blen] = 0; q->body_len = blen;
    }
    return 0;
}
static void ws_upgrade(mcs_vm_t* vm, srv_t* sv, req_t* q, mcs_value_t fn) {
    size_t kl; const char* key = hfind(q->raw + q->hdr_off, q->hdr_len, "Sec-WebSocket-Key", &kl);
    mcs_value_t list = mcs__kept(vm, sv->c_slot);
    uint32_t live = 0;
    for (uint32_t i = 0; i < mcs_len(list); i++) { ws_t* w = (ws_t*)mcs_userdata_of(mcs_index(list, i), &ws_def); if (w && w->open) live++; }
    if (!key) { send_status(vm, q, 400); return; }
    if (live >= MCS_HTTPD_MAX_WS) { send_status(vm, q, 503); return; }
    char acc[32]; ws_accept_key(key, kl, acc);
    char h[160]; int n = snprintf(h, sizeof h, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
    if (send_all(vm, &q->c, h, (size_t)n) < 0) return;
    q->sent = 1; q->status = 101;
    mcs_value_t o;
    if (mcs_new_object(vm, "WebSocket", 0, NULL, &o) != MCS_OK) return;
    mcs_push_root(vm, o);
    ws_t* w = (ws_t*)mcs_userdata(o);
    w->c = q->c; q->c.fd = -1; q->c.tls = NULL;
    w->server = 1; w->open = 1;
    snprintf(w->path, sizeof w->path, "%.127s", q->path); snprintf(w->raddr, sizeof w->raddr, "%s", q->raddr); w->rport = q->rport;
    /* reuse a closed slot */
    uint32_t i = 0;
    for (; i < mcs_len(list); i++) { ws_t* x = (ws_t*)mcs_userdata_of(mcs_index(list, i), &ws_def); if (!x || !x->open) { mcs_set_index(list, i, o); break; } }
    if (i == mcs_len(list)) mcs_list_add(vm, list, o);
    mcs_value_t a[2]; a[0] = o; a[1] = mcs_null();
    call_handler(vm, fn, 1, a, NULL, "HttpServer.WebSocket");
    mcs_pop_root(vm, 1);
}
static void serve(mcs_vm_t* vm, srv_t* sv, int fd, struct sockaddr_in* from) {
    mcs_value_t qo;
    if (mcs_new_object(vm, "HttpRequest", 0, NULL, &qo) != MCS_OK) { close(fd); mcs_take_exception(vm, NULL, 0); return; }
    mcs_push_root(vm, qo);
    req_t* q = (req_t*)mcs_userdata(qo);
    q->c.fd = fd; q->c.net = NULL; q->cors = sv->cors;
    mcs__ip_fmt((const uint8_t*)&from->sin_addr.s_addr, q->raddr); q->rport = ntohs(from->sin_port);
    sv->requests++;
    int st = read_request(vm, sv, q);
    if (st) { if (st > 0) send_status(vm, q, st); goto done; }
    if (sv->cors && !strcmp(q->method, "OPTIONS")) { req_send(vm, q, 204, "text/plain", NULL, 0, NULL); goto done; }
    mcs_value_t hl = mcs__kept(vm, sv->h_slot);
    bool path_known = false;
    size_t ul; const char* up = hfind(q->raw + q->hdr_off, q->hdr_len, "Upgrade", &ul);
    bool is_ws = up && ul == 9 && ci_eq(up, "websocket", 9);
    for (int i = 0; i < sv->nroutes; i++) {
        route_t* r = &sv->routes[i];
        if (r->kind == RT_404) continue;
        if ((r->kind == RT_WS) != is_ws) { if (r->kind == RT_WS && match(r->pat, q->path, q)) { q->plen = 0; q->params[0] = 0; path_known = true; } continue; }
        if (!match(r->pat, q->path, q)) continue;
        bool mok = r->method[0] == '*' || !strcmp(r->method, q->method) || (!strcmp(q->method, "HEAD") && !strcmp(r->method, "GET"));
        if (!mok) { path_known = true; q->plen = 0; q->params[0] = 0; continue; }
        mcs_value_t fn = mcs_index(hl, (uint32_t)i);
        if (r->kind == RT_WS) { ws_upgrade(vm, sv, q, fn); goto done; }
        char what[300]; snprintf(what, sizeof what, "HttpServer: %s %s", q->method, q->path);
        mcs_value_t a[1] = { qo }, res = mcs_null();
        if (call_handler(vm, fn, 1, a, &res, what)) respond_value(vm, q, res);
        else if (!mcs_has_exception(vm)) send_status(vm, q, 500);
        goto done;
    }
#if MCS_ENABLE_FS
    if (!strcmp(q->method, "GET") || !strcmp(q->method, "HEAD")) {
        for (int i = 0; i < sv->nst; i++) {
            static_t* s = &sv->st[i]; size_t pl = strlen(s->prefix);
            if (pl == 1 && s->prefix[0] == '/') pl = 0;
            if (strncmp(q->path, s->prefix, pl) || (q->path[pl] && q->path[pl] != '/')) continue;
            if (strstr(q->path, "..")) { send_status(vm, q, 403); goto done; }
            char fp[300]; const char* rest = q->path + pl;
            snprintf(fp, sizeof fp, "%s%s%s", s->folder, *rest ? rest : "/", (!*rest || rest[strlen(rest) - 1] == '/') ? "index.html" : "");
            if (send_file(vm, q, fp, NULL, s->max_age) == 0) goto done;
        }
    }
#endif
    for (int i = 0; i < sv->nroutes; i++) if (sv->routes[i].kind == RT_404) {
        mcs_value_t a[1] = { qo }, res = mcs_null();
        char what[300]; snprintf(what, sizeof what, "HttpServer: %s %s", q->method, q->path);
        if (call_handler(vm, mcs_index(hl, (uint32_t)i), 1, a, &res, what)) { if (!q->sent && mcs_is_null(res)) send_status(vm, q, 404); else respond_value(vm, q, res); }
        else if (!mcs_has_exception(vm)) send_status(vm, q, 500);
        goto done;
    }
    send_status(vm, q, path_known ? 405 : 404);
done:
    if (!q->sent && !mcs_has_exception(vm) && q->c.fd >= 0) send_status(vm, q, 500);
    mcs__conn_close(&q->c);
    mcs_pop_root(vm, 1);
}
NATIVE(sv_start) {
    SV();
    if (sv->fd >= 0) return self;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return mcs__sock_fail(vm, "HttpServer.Start", errno);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(sv->port); memcpy(&a.sin_addr.s_addr, sv->ip, 4);
    if (bind(fd, (struct sockaddr*)&a, sizeof a) < 0 || listen(fd, 4) < 0) { int e = errno; close(fd); return mcs__sock_fail(vm, "HttpServer.Start", e); }
    mcs__set_nonblock(fd);
    socklen_t al = sizeof a; if (getsockname(fd, (struct sockaddr*)&a, &al) == 0) sv->port = ntohs(a.sin_port);
    sv->fd = fd;
    return self;
}
NATIVE(sv_stop) {
    SV();
    if (sv->fd >= 0) close(sv->fd);
    sv->fd = -1;
    if (sv->c_slot >= 0) {
        mcs_value_t l = mcs__kept(vm, sv->c_slot);
        for (uint32_t i = 0; i < mcs_len(l); i++) { ws_t* w = (ws_t*)mcs_userdata_of(mcs_index(l, i), &ws_def); if (w && w->open) { uint8_t b[2] = { 0x03, 0xE9 }; ws_frame(vm, w, 8, b, 2); w->close_code = 1001; ws_drop(vm, w); } }
    }
    return mcs_null();
}
/* one pass: wait up to ms for a request / WebSocket message / DNS query and handle what arrived */
static int handle(mcs_vm_t* vm, srv_t* sv, uint32_t ms) {
    uint32_t t0 = mcs_ticks(vm);
    mcs_value_t cl = sv->c_slot >= 0 ? mcs__kept(vm, sv->c_slot) : mcs_null();
    for (;;) {
        fd_set s; FD_ZERO(&s); int mx = -1;
        if (sv->fd >= 0) { FD_SET(sv->fd, &s); mx = sv->fd; }
        for (uint32_t i = 0; !mcs_is_null(cl) && i < mcs_len(cl); i++) {
            ws_t* w = (ws_t*)mcs_userdata_of(mcs_index(cl, i), &ws_def);
            if (w && w->open && w->c.fd >= 0) { FD_SET(w->c.fd, &s); if (w->c.fd > mx) mx = w->c.fd; }
        }
        for (int k = 0; k < MAX_DNS; k++) if (g_dns[k] && g_dns[k]->fd >= 0) { FD_SET(g_dns[k]->fd, &s); if (g_dns[k]->fd > mx) mx = g_dns[k]->fd; }
        if (mx < 0) { if (!mcs__net_wait(vm, ms)) return -1; return 0; }
        uint32_t el = mcs_ticks(vm) - t0, left = ms > el ? ms - el : 0;
        if (left > MCS_NET_SLICE_MS) left = MCS_NET_SLICE_MS;
        struct timeval tv = { 0, (long)left * 1000 };
        int r = select(mx + 1, &s, NULL, NULL, &tv);
        if (r > 0) {
            for (int k = 0; k < MAX_DNS; k++) if (g_dns[k] && g_dns[k]->fd >= 0 && FD_ISSET(g_dns[k]->fd, &s)) dns_answer(g_dns[k]);
            for (uint32_t i = 0; !mcs_is_null(cl) && i < mcs_len(cl); i++) {
                mcs_value_t o = mcs_index(cl, i);
                ws_t* w = (ws_t*)mcs_userdata_of(o, &ws_def);
                if (!w || !w->open || w->c.fd < 0 || !FD_ISSET(w->c.fd, &s)) continue;
                mcs_push_root(vm, o);
                mcs_value_t m = mcs_null();
                if (ws_read(vm, w, o, 1, &m) == 1) ws_dispatch(vm, w, o, m);
                mcs_pop_root(vm, 1);
                if (mcs_has_exception(vm)) return -1;
            }
            if (sv->fd >= 0 && FD_ISSET(sv->fd, &s)) {
                struct sockaddr_in from; socklen_t fl = sizeof from;
                int fd = accept(sv->fd, (struct sockaddr*)&from, &fl);
                if (fd >= 0) { mcs__set_nonblock(fd); mcs__no_sigpipe(fd); serve(vm, sv, fd, &from); }
            }
            return mcs_has_exception(vm) ? -1 : 1;
        }
        if (mcs_safepoint(vm) || mcs_has_exception(vm)) return -1;
        if ((uint32_t)(mcs_ticks(vm) - t0) >= ms) return 0;
    }
}
NATIVE(sv_handle) {   /* Handle([ms]) -> true when something was handled */
    SV(); OPT_INT(0, ms, 0);
    int r = handle(vm, sv, (uint32_t)(ms < 0 ? 0 : ms));
    return mcs_bool(r > 0);
}
NATIVE(sv_run) {   /* Start (if needed) and serve until the program stops; optional loop callback */
    SV();
    if (sv->fd < 0) { sv_start(vm, self, 0, NULL); CHECK(); }
    mcs_value_t fn = argc > 0 ? argv[0] : mcs_null();
    for (;;) {
        if (handle(vm, sv, 50) < 0) break;
        if (!mcs_is_null(fn)) { mcs_value_t r = mcs_null(); if (mcs_call_value(vm, fn, 0, NULL, &r) != MCS_OK) break; if (mcs_is_bool(r) && !mcs_to_bool(vm, r)) break; }
        if (sv->fd < 0) break;
    }
    return mcs_null();
}
NATIVE(sv_broadcast) {   /* Broadcast(message[, path]) -> clients reached */
    SV(); OPT_STR(1, path);
    mcs_value_t l = mcs__kept(vm, sv->c_slot); int n = 0;
    mcs_value_t v = argv[0];
    const uint8_t* p; size_t len; uint8_t* heap = NULL; int op = 1;
    if (mcs_is_string(v)) { p = (const uint8_t*)mcs_cstr(v); len = mcs_len(v); }
    else { if (!mcs__bytes_arg(vm, v, &p, &len, &heap)) return mcs_null(); op = 2; }
    for (uint32_t i = 0; i < mcs_len(l); i++) {
        ws_t* w = (ws_t*)mcs_userdata_of(mcs_index(l, i), &ws_def);
        if (!w || !w->open || (path && strcmp(path, w->path))) continue;
        if (ws_frame(vm, w, op, p, len) == 0) n++; else { w->close_code = 1006; ws_drop(vm, w); }
    }
    mcs__bytes_free(vm, heap, len);
    return mcs_int(n);
}
NATIVE(sv_clients) {
    SV(); mcs_value_t out = mcs_new_list(vm); mcs_push_root(vm, out);
    mcs_value_t l = mcs__kept(vm, sv->c_slot);
    for (uint32_t i = 0; i < mcs_len(l); i++) { mcs_value_t o = mcs_index(l, i); ws_t* w = (ws_t*)mcs_userdata_of(o, &ws_def); if (w && w->open) mcs_list_add(vm, out, o); }
    mcs_pop_root(vm, 1); return out;
}
NATIVE(sv_ccount) { SV(); mcs_value_t l = mcs__kept(vm, sv->c_slot); int n = 0; for (uint32_t i = 0; i < mcs_len(l); i++) { ws_t* w = (ws_t*)mcs_userdata_of(mcs_index(l, i), &ws_def); if (w && w->open) n++; } return mcs_int(n); }
NATIVE(sv_port) { SV(); return mcs_int(sv->port); }
NATIVE(sv_active) { SV(); return mcs_bool(sv->fd >= 0); }
NATIVE(sv_reqs) { SV(); return mcs_int((mcs_int_t)sv->requests); }
NATIVE(sv_gcors) { SV(); return mcs_bool(sv->cors); }
NATIVE(sv_scors) { SV(); sv->cors = mcs_to_bool(vm, argv[0]); return mcs_null(); }
NATIVE(sv_gto) { SV(); return mcs_int((mcs_int_t)sv->timeout); }
NATIVE(sv_sto) { SV(); INT_ARG(0, t); sv->timeout = (uint32_t)(t < 100 ? 100 : t); return mcs_null(); }
NATIVE(sv_gmb) { SV(); return mcs_int((mcs_int_t)sv->max_body); }
NATIVE(sv_smb) { SV(); INT_ARG(0, t); sv->max_body = (uint32_t)(t < 0 ? 0 : t); return mcs_null(); }
static const mcs_reg_t srv_members[] = {
    MCS_FN("Get", sv_get, 2), MCS_FN("Post", sv_post, 2), MCS_FN("Put", sv_put, 2), MCS_FN("Patch", sv_patch, 2),
    MCS_FN("Delete", sv_delete, 2), MCS_FN("Any", sv_any, 2), MCS_FN("On", sv_on, 3), MCS_FN("Static", sv_static, -1),
    MCS_FN("NotFound", sv_notfound, 1), MCS_FN("WebSocket", sv_ws, 2), MCS_FN("Start", sv_start, 0), MCS_FN("Stop", sv_stop, 0),
    MCS_FN("Dispose", sv_stop, 0), MCS_FN("Handle", sv_handle, -1), MCS_FN("Run", sv_run, -1), MCS_FN("Broadcast", sv_broadcast, -1),
    MCS_GET("WebSocketClients", sv_clients), MCS_GET("WebSocketCount", sv_ccount), MCS_GET("Port", sv_port), MCS_GET("Active", sv_active),
    MCS_GET("Requests", sv_reqs), MCS_GET("Cors", sv_gcors), MCS_SET("Cors", sv_scors), MCS_GET("Timeout", sv_gto), MCS_SET("Timeout", sv_sto),
    MCS_GET("MaxBody", sv_gmb), MCS_SET("MaxBody", sv_smb), MCS_REG_END };
static const mcs_class_def_t srv_def = { "HttpServer", sizeof(srv_t), srv_ctor, srv_fin, srv_members, NULL };

void mcs__web_open(mcs_vm_t* vm) {
    mcs_register_class(vm, &ws_def);
    mcs_register_class(vm, &dnss_def);
    mcs_register_class(vm, &req_def);
    mcs_register_class(vm, &srv_def);
}
#endif
