/*
 * MicroCS - servers for C# scripts on top of the BSD sockets (MCS_ENABLE_SOCKETS):
 *
 *   HttpServer    routes (exact, {param} segments, trailing *), query / form / JSON bodies,
 *                 files from the VFS, Basic auth, CORS, WebSocket endpoints
 *   HttpRequest   one request + its response (Send, SendJson, SendFile, Redirect ...)
 *   WebSocket     RFC 6455 client (ws:// and wss://) and the server side of HttpServer
 *   DnsServer     answers every name with one address: the captive portal of a soft AP
 *
 *   var web = new HttpServer(80);
 *   web.Get("/", req => "<h1>Hello</h1>");
 *   web.WebSocket("/ws", (ws, msg) => ws.Send("echo " + msg));
 *   web.Run();                                   // or web.Poll(ms) inside your own loop
 *
 * Everything runs on the script thread: Poll / Run wait in short slices (MCS_NET_SLICE_MS),
 * so Stop, Ctrl-C and HAL events keep working while a server waits for clients.
 */
#include "mcs_net_internal.h"
#if MCS_ENABLE_NET && MCS_ENABLE_SOCKETS
#include "mcs_net_posix.h"
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif

#ifndef MCS_NET_SLICE_MS
#define MCS_NET_SLICE_MS 20
#endif
#define WS_FRAME_TIMEOUT 5000            /* rest of a frame once its first byte arrived (ms) */

/* ------------------------------------------------------------ SHA-1 + base64 (handshake) */
typedef struct { uint32_t h[5]; uint64_t n; uint8_t b[64]; size_t bl; } sha1_t;
#define ROL(x, k) (((x) << (k)) | ((x) >> (32 - (k))))
static void sha1_block(sha1_t* s, const uint8_t* p) {
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha1_init(sha1_t* s) {
    s->h[0] = 0x67452301; s->h[1] = 0xEFCDAB89; s->h[2] = 0x98BADCFE; s->h[3] = 0x10325476; s->h[4] = 0xC3D2E1F0;
    s->n = 0; s->bl = 0;
}
static void sha1_add(sha1_t* s, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    s->n += n;
    while (n--) { s->b[s->bl++] = *p++; if (s->bl == 64) { sha1_block(s, s->b); s->bl = 0; } }
}
static void sha1_done(sha1_t* s, uint8_t out[20]) {
    uint64_t bits = s->n * 8;
    uint8_t one = 0x80, zero = 0;
    sha1_add(s, &one, 1);
    while (s->bl != 56) sha1_add(s, &zero, 1);
    uint8_t L[8];
    for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_add(s, L, 8);
    for (int i = 0; i < 20; i++) out[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static size_t b64_enc(const uint8_t* p, size_t n, char* out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        out[o++] = B64[v >> 18 & 63]; out[o++] = B64[v >> 12 & 63];
        out[o++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? B64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}
static void ws_accept_key(const char* key, size_t kl, char out[32]) {
    sha1_t s; uint8_t d[20];
    sha1_init(&s); sha1_add(&s, key, kl); sha1_add(&s, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36); sha1_done(&s, d);
    b64_enc(d, 20, out);
}
static uint32_t rnd32(mcs_vm_t* vm) {
    static uint32_t x;
    if (!x) x = 0x9E3779B9u ^ mcs_ticks(vm) ^ (uint32_t)(uintptr_t)&x;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

/* ------------------------------------------------------------ small helpers */
/* header value (case-insensitive) inside "Name: value\r\n" lines */
static const char* hfind(const char* h, size_t hl, const char* name, size_t* vl) {
    size_t nl = strlen(name);
    const char* p = h; const char* end = h + hl;
    while (p < end) {
        const char* eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol) eol = end;
        if ((size_t)(eol - p) > nl && p[nl] == ':') {
            size_t i = 0;
            while (i < nl && (p[i] | 0x20) == (name[i] | 0x20)) i++;
            if (i == nl) {
                const char* v = p + nl + 1;
                while (v < eol && (*v == ' ' || *v == '\t')) v++;
                const char* ve = eol;
                while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
                *vl = (size_t)(ve - v);
                return v;
            }
        }
        p = eol + 1;
    }
    return NULL;
}
/* does the comma separated header value contain token (case-insensitive)? */
static bool has_token(const char* v, size_t vl, const char* tok) {
    size_t tl = strlen(tok);
    for (size_t i = 0; i + tl <= vl; i++) {
        size_t k = 0;
        while (k < tl && (v[i + k] | 0x20) == (tok[k] | 0x20)) k++;
        if (k == tl) return true;
    }
    return false;
}
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* URL-decode (+ = space) in place; returns the new length */
static size_t url_decode(char* s, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '+') s[o++] = ' ';
        else if (s[i] == '%' && i + 2 < n + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { s[o++] = (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
        else s[o++] = s[i];
    }
    return o;
}
/* value of `name` in "a=1&b=2" (decoded, as a C# string), or null; *found tells */
static mcs_value_t form_get(mcs_vm_t* vm, const char* s, size_t n, const char* name, bool* found) {
    size_t nl = strlen(name);
    const char* p = s; const char* end = s + n;
    while (p < end) {
        const char* amp = memchr(p, '&', (size_t)(end - p));
        if (!amp) amp = end;
        const char* eq = memchr(p, '=', (size_t)(amp - p));
        const char* ke = eq ? eq : amp;
        /* keys are compared decoded */
        char kb[64]; size_t kl = (size_t)(ke - p);
        if (kl < sizeof kb) {
            memcpy(kb, p, kl); kl = url_decode(kb, kl);
            if (kl == nl && !memcmp(kb, name, nl)) {
                *found = true;
                if (!eq) return mcs_string(vm, "");
                size_t vl = (size_t)(amp - eq - 1);
                char* tmp = (char*)mcs_mem_realloc(vm, NULL, 0, vl + 1);
                if (!tmp) return mcs_null();
                memcpy(tmp, eq + 1, vl);
                vl = url_decode(tmp, vl);
                mcs_value_t v = mcs_string_n(vm, tmp, vl);
                mcs_mem_realloc(vm, tmp, (size_t)(amp - eq - 1) + 1, 0);
                return v;
            }
        }
        p = amp + 1;
    }
    return mcs_null();
}
static bool raw_write(mcs_vm_t* vm, mcs__conn_t* c, const void* p, size_t n) {
    return mcs__conn_write(vm, c, (const uint8_t*)p, n, MCS_NET_TIMEOUT) >= 0;
}
static mcs_value_t peer_ip(mcs_vm_t* vm, int fd) {
    struct sockaddr_in a; socklen_t l = sizeof a; memset(&a, 0, sizeof a);
    if (fd >= 0) getpeername(fd, (struct sockaddr*)&a, &l);
    char b[16]; mcs__ip_fmt((const uint8_t*)&a.sin_addr, b);
    return mcs_string(vm, b);
}
static mcs_value_t call_json(mcs_vm_t* vm, const char* fn, mcs_value_t arg) {
    mcs_value_t out = mcs_null();
    if (mcs_call(vm, fn, 1, &arg, &out) != MCS_OK) return mcs_null();
    return out;
}

/* ============================================================ WebSocket */
enum { WS_NEW = 0, WS_OPEN = 1, WS_CLOSED = 3 };
enum { WS_OP_CONT = 0, WS_OP_TEXT = 1, WS_OP_BIN = 2, WS_OP_CLOSE = 8, WS_OP_PING = 9, WS_OP_PONG = 10 };
typedef struct {
    mcs__net_resource_t resource;
    mcs__conn_t c;
    uint8_t server;              /* accepted by an HttpServer: frames are not masked */
    uint8_t state;
    uint8_t mop;                 /* opcode of the fragmented message being assembled */
    uint16_t close_code;
    char close_reason[64];
    uint32_t timeout;            /* Receive() default (ms, 0 = forever) */
    int id;
    int handler_pin;             /* OnMessage handler (client Poll), -1 = none */
    char path[128];              /* request path (server side) / URL (client) */
    uint8_t* msg; size_t mlen, mcap;
} ws_t;
static const mcs_class_def_t ws_def;
static void ws_drop(mcs_vm_t* vm, ws_t* w) {
    mcs__conn_close(&w->c);
    w->state = WS_CLOSED;
    if (w->msg) mcs_mem_realloc(vm, w->msg, w->mcap, 0);
    w->msg = NULL; w->mlen = w->mcap = 0;
}
static void ws_fin(mcs_vm_t* vm, void* d) {
    ws_t* w = (ws_t*)d;
    mcs__net_untrack(&w->resource);
    ws_drop(vm, w);
    if (w->handler_pin >= 0) { mcs_unpin(vm, w->handler_pin); w->handler_pin = -1; }
}
/* one frame; 0 or -errno */
static int ws_frame(mcs_vm_t* vm, ws_t* w, int op, const uint8_t* p, size_t n) {
    if (w->state != WS_OPEN) return -ENOTCONN;
    uint8_t h[14]; size_t hl;
    uint8_t m = w->server ? 0 : 0x80;
    h[0] = (uint8_t)(0x80 | op);
    if (n < 126) { h[1] = (uint8_t)(m | n); hl = 2; }
    else if (n < 65536) { h[1] = m | 126; h[2] = (uint8_t)(n >> 8); h[3] = (uint8_t)n; hl = 4; }
    else { h[1] = m | 127; for (int i = 0; i < 8; i++) h[2 + i] = (uint8_t)((uint64_t)n >> (56 - 8 * i)); hl = 10; }
    uint8_t key[4] = { 0, 0, 0, 0 };
    if (m) { uint32_t r = rnd32(vm); memcpy(key, &r, 4); memcpy(h + hl, key, 4); hl += 4; }
    int r = mcs__conn_write(vm, &w->c, h, hl, MCS_NET_TIMEOUT);
    if (r < 0) return r;
    if (!m) return n ? (mcs__conn_write(vm, &w->c, p, n, MCS_NET_TIMEOUT) < 0 ? -EIO : 0) : 0;
    uint8_t buf[256];
    for (size_t off = 0; off < n;) {
        size_t k = n - off < sizeof buf ? n - off : sizeof buf;
        for (size_t i = 0; i < k; i++) buf[i] = p[off + i] ^ key[(off + i) & 3];
        r = mcs__conn_write(vm, &w->c, buf, k, MCS_NET_TIMEOUT);
        if (r < 0) return r;
        off += k;
    }
    return 0;
}
static void ws_send_close(mcs_vm_t* vm, ws_t* w, int code, const char* reason) {
    uint8_t b[125] = { 0 }; size_t n = 0;
    if (code) {
        b[0] = (uint8_t)(code >> 8); b[1] = (uint8_t)code; n = 2;
        size_t rl = reason ? strlen(reason) : 0;
        if (rl > 120) rl = 120;
        if (rl) { memcpy(b + 2, reason, rl); n += rl; }
    }
    if (w->state == WS_OPEN) ws_frame(vm, w, WS_OP_CLOSE, b, n);
    if (!w->close_code) w->close_code = (uint16_t)(code ? code : 1005);
}
/* read exactly n bytes: 1 done, 0 nothing arrived in time (only before the first byte),
 * -1 connection closed / failed (w closed), -2 aborted */
static int ws_read(mcs_vm_t* vm, ws_t* w, uint8_t* buf, size_t n, uint32_t timeout, bool nowait) {
    size_t got = 0;
    while (got < n) {
        int r;
        if (got == 0 && nowait) {
            if (w->c.tls) {
                r = w->c.net->tls_read(w->c.tls, buf, n, 1);
                if (r == MCS_HAL_ETIMEOUT) return 0;
                if (r < 0) r = -EIO;
            } else {
                fd_set s; FD_ZERO(&s); FD_SET(w->c.fd, &s);
                struct timeval tv = { 0, 0 };
                if (select(w->c.fd + 1, &s, NULL, NULL, &tv) <= 0) return 0;
                r = mcs__conn_read(vm, &w->c, buf, n, WS_FRAME_TIMEOUT);
            }
        } else r = mcs__conn_read(vm, &w->c, buf + got, n - got, got ? WS_FRAME_TIMEOUT : timeout);
        if (r == -EINTR) return -2;
        if (r == -ETIMEDOUT && got == 0) return 0;
        if (r <= 0) { if (!w->close_code) w->close_code = 1006; ws_drop(vm, w); return -1; }
        got += (size_t)r;
    }
    return 1;
}
/* next message: 1 (*out = string for text, byte[] for binary), 0 none in time, -1 closed, -2 aborted / raised */
static int ws_next(mcs_vm_t* vm, ws_t* w, uint32_t timeout, bool nowait, mcs_value_t* out) {
    for (;;) {
        if (w->state != WS_OPEN) return -1;
        uint8_t h[2];
        int r = ws_read(vm, w, h, 2, timeout, nowait);
        if (r <= 0) return r == 0 ? 0 : r;
        int fin = h[0] & 0x80, op = h[0] & 0x0F, masked = h[1] & 0x80;
        uint64_t len = h[1] & 0x7F;
        uint8_t ext[8], key[4] = { 0, 0, 0, 0 };
        if (len == 126) { if ((r = ws_read(vm, w, ext, 2, WS_FRAME_TIMEOUT, false)) <= 0) return r ? r : -1; len = (uint64_t)ext[0] << 8 | ext[1]; }
        else if (len == 127) {
            if ((r = ws_read(vm, w, ext, 8, WS_FRAME_TIMEOUT, false)) <= 0) return r ? r : -1;
            len = 0; for (int i = 0; i < 8; i++) len = len << 8 | ext[i];
        }
        if (masked && (r = ws_read(vm, w, key, 4, WS_FRAME_TIMEOUT, false)) <= 0) return r ? r : -1;
        if (op >= 8) {                          /* control frame */
            uint8_t b[125];
            if (len > 125 || !fin) { ws_send_close(vm, w, 1002, "bad control frame"); ws_drop(vm, w); return -1; }
            if (len && (r = ws_read(vm, w, b, (size_t)len, WS_FRAME_TIMEOUT, false)) <= 0) return r ? r : -1;
            for (size_t i = 0; i < len; i++) b[i] ^= key[i & 3];
            if (op == WS_OP_CLOSE) {
                int code = len >= 2 ? (b[0] << 8 | b[1]) : 1005;
                size_t rl = len > 2 ? (size_t)len - 2 : 0;
                if (rl >= sizeof w->close_reason) rl = sizeof w->close_reason - 1;
                memcpy(w->close_reason, b + 2, rl); w->close_reason[rl] = 0;
                w->close_code = (uint16_t)code;
                if (w->state == WS_OPEN) ws_frame(vm, w, WS_OP_CLOSE, b, len >= 2 ? 2 : 0);
                ws_drop(vm, w);
                return -1;
            }
            if (op == WS_OP_PING) ws_frame(vm, w, WS_OP_PONG, b, (size_t)len);
            continue;                           /* pong / ping handled: next frame */
        }
        if ((op == WS_OP_CONT) != (w->mlen != 0 || w->mop != 0) || op > WS_OP_BIN) {
            ws_send_close(vm, w, 1002, "bad frame"); ws_drop(vm, w); return -1;
        }
        if (len > (uint64_t)(MCS_WS_MAX_MESSAGE - w->mlen)) { ws_send_close(vm, w, 1009, "message too big"); ws_drop(vm, w); return -1; }
        if (op) w->mop = (uint8_t)op;
        size_t need = w->mlen + (size_t)len + 1;
        if (need > w->mcap) {
            size_t nc = need < 256 ? 256 : need;
            uint8_t* nb = (uint8_t*)mcs_mem_realloc(vm, w->msg, w->mcap, nc);
            if (!nb) return -2;
            w->msg = nb; w->mcap = nc;
        }
        if (len && (r = ws_read(vm, w, w->msg + w->mlen, (size_t)len, WS_FRAME_TIMEOUT, false)) <= 0) return r ? r : -1;
        for (size_t i = 0; i < len; i++) w->msg[w->mlen + i] ^= key[i & 3];
        w->mlen += (size_t)len;
        if (!fin) { nowait = false; timeout = WS_FRAME_TIMEOUT; continue; }
        *out = w->mop == WS_OP_TEXT ? mcs_string_n(vm, (const char*)w->msg, w->mlen) : mcs__bytes_new(vm, w->msg, w->mlen);
        w->mlen = 0; w->mop = 0;
        return mcs_has_exception(vm) ? -2 : 1;
    }
}

#define WS() ws_t* w = (ws_t*)mcs_check_userdata(vm, self, &ws_def); if (!w) return mcs_null()
typedef struct { bool tls; int port; char host[128]; char path[512]; } wsurl_t;
static bool ws_url(mcs_vm_t* vm, const char* u, wsurl_t* o) {
    memset(o, 0, sizeof *o);
    const char* p = u;
    if (!strncmp(p, "ws://", 5)) p += 5;
    else if (!strncmp(p, "wss://", 6)) { p += 6; o->tls = true; }
    else if (!strncmp(p, "http://", 7)) p += 7;
    else if (!strncmp(p, "https://", 8)) { p += 8; o->tls = true; }
    else { mcs_raise(vm, "ArgumentException", "WebSocket: URL must start with ws:// or wss:// ('%s')", u); return false; }
    o->port = o->tls ? 443 : 80;
    const char* h = p;
    while (*p && *p != '/' && *p != ':' && *p != '?') p++;
    size_t hl = (size_t)(p - h);
    if (!hl || hl >= sizeof o->host) { mcs_raise(vm, "ArgumentException", "WebSocket: bad host in '%s'", u); return false; }
    memcpy(o->host, h, hl);
    if (*p == ':') {
        long port = strtol(p + 1, (char**)&p, 10);
        if (port < 1 || port > 65535) { mcs_raise(vm, "ArgumentException", "WebSocket: bad port in '%s'", u); return false; }
        o->port = (int)port;
    }
    if (strlen(p) >= sizeof o->path - 1) { mcs_raise(vm, "ArgumentException", "WebSocket: URL too long"); return false; }
    snprintf(o->path, sizeof o->path, "%s%s", *p == '/' ? "" : "/", p);
    return true;
}
/* extra request headers: "Name: value" lines or Dictionary<string, string> */
static bool ws_headers(mcs_vm_t* vm, mcs_value_t hv, char* out, size_t cap) {
    size_t n = 0; out[0] = 0;
    if (mcs_is_null(hv)) return true;
    if (mcs_is_string(hv)) {
        const char* s = mcs_cstr(hv);
        while (*s) {
            const char* e = s; while (*e && *e != '\n' && *e != '\r') e++;
            if (e > s) { if (n + (size_t)(e - s) + 3 > cap) goto big; memcpy(out + n, s, (size_t)(e - s)); n += (size_t)(e - s); out[n++] = '\r'; out[n++] = '\n'; }
            while (*e == '\r' || *e == '\n') e++;
            s = e;
        }
        out[n] = 0;
        return true;
    }
    if (mcs_obj_kind(hv) == MCS_O_DICT) {
        mcs_value_t k, v;
        for (uint32_t i = 0; mcs_dict_at(hv, i, &k, &v); i++) {
            const char* ks = mcs_to_cstr(vm, k); if (mcs_has_exception(vm)) return false;
            mcs_value_t vs = mcs_tostring(vm, v); if (mcs_has_exception(vm)) return false;
            int w = snprintf(out + n, cap - n, "%s: %s\r\n", ks, mcs_cstr(vs) ? mcs_cstr(vs) : "");
            if (w < 0 || (size_t)w >= cap - n) goto big;
            n += (size_t)w;
        }
        return true;
    }
    mcs_raise(vm, "ArgumentException", "WebSocket.Connect: headers must be a string or Dictionary<string, string>");
    return false;
big:
    mcs_raise(vm, "ArgumentException", "WebSocket.Connect: headers too long");
    return false;
}
static void ws_connect(mcs_vm_t* vm, ws_t* w, int argc, mcs_value_t* argv) {
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "WebSocket.Connect(url[, timeoutMs[, headers]])"); return; }
    const char* url = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return;
    mcs_int_t to = argc > 1 && !mcs_is_null(argv[1]) ? mcs_to_int(vm, argv[1]) : (mcs_int_t)MCS_NET_TIMEOUT;
    if (mcs_has_exception(vm)) return;
    if (to <= 0) to = MCS_NET_TIMEOUT;
    wsurl_t u;
    if (!ws_url(vm, url, &u)) return;
    char extra[512];
    if (!ws_headers(vm, argc > 2 ? argv[2] : mcs_null(), extra, sizeof extra)) return;
    ws_drop(vm, w);
    w->server = 0; w->close_code = 0; w->close_reason[0] = 0; w->mop = 0;
    if (mcs__conn_open(vm, &w->c, u.host, u.port, u.tls, (uint32_t)to, NULL, "WebSocket.Connect") < 0) return;
    uint8_t nonce[16]; char key[32], expect[32];
    for (int i = 0; i < 16; i += 4) { uint32_t r = rnd32(vm); memcpy(nonce + i, &r, 4); }
    b64_enc(nonce, 16, key);
    ws_accept_key(key, strlen(key), expect);
    size_t rq = strlen(u.path) + strlen(u.host) + strlen(extra) + 200;
    char* req = (char*)mcs_mem_realloc(vm, NULL, 0, rq);
    if (!req) { mcs__conn_close(&w->c); return; }
    bool defport = u.port == (u.tls ? 443 : 80);
    int n = snprintf(req, rq, "GET %s HTTP/1.1\r\nHost: %s%s%.0d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: MicroCS/%s\r\n%s\r\n",
                     u.path, u.host, defport ? "" : ":", defport ? 0 : u.port, key, MCS_VERSION_STRING, extra);
    w->state = WS_OPEN;
    mcs__net_track(vm, &w->resource, w, ws_fin);                       /* lets ws_read work during the handshake */
    int wr = mcs__conn_write(vm, &w->c, (const uint8_t*)req, (size_t)n, (uint32_t)to);
    mcs_mem_realloc(vm, req, rq, 0);
    if (wr < 0) { ws_drop(vm, w); if (wr != -EINTR) mcs__sock_fail(vm, "WebSocket.Connect", -wr); return; }
    char hd[1024]; size_t hl = 0;
    for (;;) {                                /* response head, byte by byte: frames may follow at once */
        uint8_t ch;
        int r = ws_read(vm, w, &ch, 1, (uint32_t)to, false);
        if (r == -2) { ws_drop(vm, w); return; }
        if (r == 0) { ws_drop(vm, w); mcs_raise(vm, "TimeoutException", "WebSocket.Connect: no handshake answer from %s", u.host); return; }
        if (r < 0) { mcs_raise(vm, "WebSocketException", "WebSocket.Connect: %s closed the connection during the handshake", u.host); return; }
        if (hl < sizeof hd - 1) hd[hl++] = (char)ch;
        if (hl >= 4 && !memcmp(hd + hl - 4, "\r\n\r\n", 4)) break;
        if (hl >= sizeof hd - 1) { ws_drop(vm, w); mcs_raise(vm, "WebSocketException", "WebSocket.Connect: handshake answer too long"); return; }
    }
    hd[hl] = 0;
    int status = 0;
    sscanf(hd, "HTTP/%*d.%*d %d", &status);
    size_t vl; const char* acc = hfind(hd, hl, "Sec-WebSocket-Accept", &vl);
    if (status != 101 || !acc || vl != strlen(expect) || memcmp(acc, expect, vl)) {
        ws_drop(vm, w);
        mcs_raise(vm, "WebSocketException", "WebSocket.Connect: %s refused the upgrade (HTTP %d)", u.host, status);
        return;
    }
    snprintf(w->path, sizeof w->path, "%s", url);
    w->state = WS_OPEN;
    mcs__net_track(vm, &w->resource, w, ws_fin);
}
static void ws_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    ws_t* w = (ws_t*)mcs_userdata(self);
    w->c.fd = -1; w->handler_pin = -1; w->timeout = MCS_NET_TIMEOUT;
    mcs__net_state_t* s = mcs__net_state(vm);
    w->id = s ? ++s->ws_next_id : 0;
    if (argc) ws_connect(vm, w, argc, argv);
}
NATIVE(ws_connect_m) { WS(); ws_connect(vm, w, argc, argv); return mcs_null(); }
static mcs_value_t ws_send_val(mcs_vm_t* vm, ws_t* w, mcs_value_t v, int force_op, const char* op) {
    if (w->state != WS_OPEN) { mcs_raise(vm, "InvalidOperationException", "%s: the WebSocket is not open", op); return mcs_null(); }
    const uint8_t* p; size_t n; uint8_t* heap;
    int code = force_op;
    if (!mcs_is_string(v) && mcs_obj_kind(v) != MCS_O_ARRAY && mcs_obj_kind(v) != MCS_O_LIST) {
        v = mcs_tostring(vm, v); if (mcs_has_exception(vm)) return mcs_null();
    }
    if (!code) code = mcs_is_string(v) ? WS_OP_TEXT : WS_OP_BIN;
    if (!mcs__bytes_arg(vm, v, &p, &n, &heap)) return mcs_null();
    int r = ws_frame(vm, w, code, p, n);
    mcs__bytes_free(vm, heap, n);
    if (r < 0 && r != -EINTR) { if (!w->close_code) w->close_code = 1006; ws_drop(vm, w); return mcs__sock_fail(vm, op, -r); }
    return mcs_null();
}
NATIVE(ws_send) { WS(); return ws_send_val(vm, w, argv[0], 0, "WebSocket.Send"); }
NATIVE(ws_send_text) { WS(); return ws_send_val(vm, w, argv[0], WS_OP_TEXT, "WebSocket.SendText"); }
NATIVE(ws_send_bin) { WS(); return ws_send_val(vm, w, argv[0], WS_OP_BIN, "WebSocket.SendBinary"); }
NATIVE(ws_ping) {
    WS();
    if (w->state != WS_OPEN) { mcs_raise(vm, "InvalidOperationException", "WebSocket.Ping: the WebSocket is not open"); return mcs_null(); }
    const uint8_t* p = (const uint8_t*)""; size_t n = 0; uint8_t* heap = NULL;
    if (argc > 0 && !mcs__bytes_arg(vm, argv[0], &p, &n, &heap)) return mcs_null();
    if (n > 125) n = 125;
    ws_frame(vm, w, WS_OP_PING, p, n);
    mcs__bytes_free(vm, heap, n);
    return mcs_null();
}
NATIVE(ws_receive) {    /* ([timeoutMs]) -> string / byte[], null on timeout or when closed */
    WS();
    OPT_INT(0, to, w->timeout);
    if (w->state != WS_OPEN) return mcs_null();
    mcs_value_t out = mcs_null();
    int r = ws_next(vm, w, (uint32_t)(to < 0 ? 0 : to), false, &out);
    return r == 1 ? out : mcs_null();
}
NATIVE(ws_close) {
    WS();
    OPT_INT(0, code, 1000);
    OPT_STR(1, reason);
    if (w->state == WS_OPEN) ws_send_close(vm, w, (int)code, reason);
    ws_drop(vm, w);
    return mcs_null();
}
NATIVE(ws_onmessage) {
    WS();
    if (w->handler_pin >= 0) { mcs_unpin(vm, w->handler_pin); w->handler_pin = -1; }
    if (!mcs_is_null(argv[0])) w->handler_pin = mcs_pin(vm, argv[0]);
    return mcs_null();
}
static int ws_dispatch(mcs_vm_t* vm, mcs_value_t self, mcs_value_t fn, mcs_value_t msg) {
    mcs_value_t args[2] = { self, msg };
    int a = mcs_arity(fn);
    mcs_result_t r = a == 1 ? mcs_call_value(vm, fn, 1, &args[1], NULL) : mcs_call_value(vm, fn, 2, args, NULL);
    return r == MCS_OK && !mcs_has_exception(vm) ? 0 : -1;
}
NATIVE(ws_poll) {       /* ([ms]) -> messages handed to OnMessage */
    WS();
    OPT_INT(0, ms, 0);
    int n = 0;
    uint32_t t0 = mcs_ticks(vm);
    while (w->state == WS_OPEN) {
        uint32_t el = (uint32_t)(mcs_ticks(vm) - t0);
        bool nowait = n > 0 || ms <= 0 || el >= (uint32_t)ms;
        mcs_value_t out = mcs_null();
        int r = ws_next(vm, w, nowait ? 0 : (uint32_t)ms - el, nowait, &out);
        if (r != 1) break;
        n++;
        if (w->handler_pin >= 0) {
            mcs_push_root(vm, out);
            int e = ws_dispatch(vm, self, mcs_pinned(vm, w->handler_pin), out);
            mcs_pop_root(vm, 1);
            if (e) return mcs_null();
        }
    }
    return mcs_int(n);
}
NATIVE(ws_isopen) { WS(); return mcs_bool(w->state == WS_OPEN); }
NATIVE(ws_state) { WS(); return mcs_string(vm, w->state == WS_OPEN ? "Open" : w->state == WS_NEW ? "None" : "Closed"); }
NATIVE(ws_cstatus) { WS(); return mcs_int(w->close_code); }
NATIVE(ws_creason) { WS(); return mcs_string(vm, w->close_reason); }
NATIVE(ws_id) { WS(); return mcs_int(w->id); }
NATIVE(ws_path) { WS(); return mcs_string(vm, w->path); }
NATIVE(ws_remote) { WS(); return peer_ip(vm, w->c.fd); }
NATIVE(ws_gettimeout) { WS(); return mcs_int((mcs_int_t)w->timeout); }
NATIVE(ws_settimeout) { WS(); INT_ARG(0, v); w->timeout = (uint32_t)(v < 0 ? 0 : v); return mcs_null(); }
NATIVE(ws_istls) { WS(); return mcs_bool(w->c.tls != NULL); }
NATIVE(ws_tostring) {
    WS(); char b[176];
    snprintf(b, sizeof b, "WebSocket #%d %s (%s)", w->id, w->path, w->state == WS_OPEN ? "open" : "closed");
    return mcs_string(vm, b);
}
static const mcs_reg_t ws_members[] = {
    MCS_FN("Connect", ws_connect_m, -1), MCS_FN("Send", ws_send, 1), MCS_FN("SendText", ws_send_text, 1),
    MCS_FN("SendBinary", ws_send_bin, 1), MCS_FN("Ping", ws_ping, -1), MCS_FN("Receive", ws_receive, -1),
    MCS_FN("Close", ws_close, -1), MCS_FN("Dispose", ws_close, 0), MCS_FN("OnMessage", ws_onmessage, 1),
    MCS_FN("Poll", ws_poll, -1), MCS_GET("IsOpen", ws_isopen), MCS_GET("State", ws_state),
    MCS_GET("CloseStatus", ws_cstatus), MCS_GET("CloseReason", ws_creason), MCS_GET("Id", ws_id),
    MCS_GET("Path", ws_path), MCS_GET("Url", ws_path), MCS_GET("RemoteAddress", ws_remote),
    MCS_GET("Timeout", ws_gettimeout), MCS_SET("Timeout", ws_settimeout), MCS_GET("IsTls", ws_istls),
    MCS_FN("ToString", ws_tostring, 0), MCS_REG_END };
static const mcs_class_def_t ws_def = { "WebSocket", sizeof(ws_t), ws_ctor, ws_fin, ws_members, NULL };
/* ============================================================ DnsServer */
typedef struct {
    mcs__net_resource_t resource; int fd; uint8_t ip[4]; uint16_t port; uint32_t ttl, queries; char domain[64]; } dns_t;
static const mcs_class_def_t dns_def;
static void dns_unreg(mcs_vm_t* vm, dns_t* d) {
    mcs__net_state_t* s = mcs__net_state(vm);
    if (s) for (int i = 0; i < 2; i++) if (s->dns[i] == d) s->dns[i] = NULL;
}
static void dns_stop(mcs_vm_t* vm, dns_t* d) { if (d->fd >= 0) close(d->fd); d->fd = -1; dns_unreg(vm, d); }
static void dns_fin(mcs_vm_t* vm, void* p) { dns_t* d = (dns_t*)p; mcs__net_untrack(&d->resource); dns_stop(vm, d); }
static void dns_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    dns_t* d = (dns_t*)mcs_userdata(self);
    d->fd = -1; d->ttl = 60; d->port = 53; strcpy(d->domain, "*");
    d->ip[0] = 192; d->ip[1] = 168; d->ip[2] = 4; d->ip[3] = 1;
    mcs__net_state_t* s = mcs__net_state(vm);
    mcs_net_ip_t ip;
    if (s && s->net && s->net->ip_get && s->net->ip_get(s->net->ctx, MCS_NET_IF_AP, &ip) >= 0 && ip.ip[0]) memcpy(d->ip, ip.ip, 4);
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "new DnsServer([address])"); return; }
    if (argc) {
        const char* a = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return;
        if (!mcs__ip_parse(a, d->ip)) mcs_raise(vm, "ArgumentException", "DnsServer: '%s' is not an IPv4 address", a);
    }
}
#define DN() dns_t* d = (dns_t*)mcs_check_userdata(vm, self, &dns_def); if (!d) return mcs_null()
/* answer one query in q (n bytes) into a (cap); returns the answer length, 0 = drop */
static size_t dns_answer(dns_t* d, const uint8_t* q, size_t n, uint8_t* a, size_t cap) {
    if (n < 12 || (q[2] & 0x80) || q[4] != 0 || q[5] < 1) return 0;   /* a query with a question */
    size_t p = 12; char name[256]; size_t nl = 0;
    while (p < n && q[p]) {
        size_t l = q[p];
        if (l > 63 || p + 1 + l > n) return 0;
        if (nl && nl < sizeof name - 1) name[nl++] = '.';
        for (size_t i = 0; i < l && nl < sizeof name - 1; i++) { char c = (char)q[p + 1 + i]; name[nl++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
        p += 1 + l;
    }
    name[nl] = 0;
    if (p + 5 > n) return 0;
    size_t qend = p + 5;
    int qtype = q[p + 1] << 8 | q[p + 2];
    bool match = !strcmp(d->domain, "*") || !strcmp(d->domain, name);
    bool answer = match && (qtype == 1 || qtype == 255);
    if (qend + 16 > cap) return 0;
    memcpy(a, q, 2);                                   /* id */
    a[2] = (uint8_t)(0x84 | (q[2] & 0x01));            /* response, authoritative, RD copied */
    a[3] = (uint8_t)(0x80 | (match ? 0 : 3));          /* RA; NXDOMAIN for other names */
    a[4] = 0; a[5] = 1; a[6] = 0; a[7] = answer ? 1 : 0; a[8] = a[9] = a[10] = a[11] = 0;
    memcpy(a + 12, q + 12, qend - 12);
    size_t o = qend;
    if (answer) {
        static const uint8_t rr[] = { 0xC0, 0x0C, 0, 1, 0, 1 };
        memcpy(a + o, rr, 6); o += 6;
        a[o++] = (uint8_t)(d->ttl >> 24); a[o++] = (uint8_t)(d->ttl >> 16); a[o++] = (uint8_t)(d->ttl >> 8); a[o++] = (uint8_t)d->ttl;
        a[o++] = 0; a[o++] = 4; memcpy(a + o, d->ip, 4); o += 4;
    }
    return o;
}
static int dns_service(dns_t* d) {      /* answer everything waiting; returns the count */
    int n = 0;
    while (d->fd >= 0) {
        uint8_t q[512], a[600];
        struct sockaddr_in from; socklen_t fl = sizeof from;
        ssize_t r = recvfrom(d->fd, q, sizeof q, 0, (struct sockaddr*)&from, &fl);
        if (r <= 0) break;
        size_t al = dns_answer(d, q, (size_t)r, a, sizeof a);
        if (al) { sendto(d->fd, a, al, 0, (struct sockaddr*)&from, fl); d->queries++; n++; }
    }
    return n;
}
NATIVE(dns_start) {     /* ([address[, port]]) */
    DN();
    if (argc > 0 && !mcs_is_null(argv[0])) {
        const char* a = mcs_to_cstr(vm, argv[0]); CHECK();
        if (!mcs__ip_parse(a, d->ip)) { mcs_raise(vm, "ArgumentException", "DnsServer.Start: '%s' is not an IPv4 address", a); return mcs_null(); }
    }
    OPT_INT(1, port, d->port);
    if (port < 1 || port > 65535) { mcs_raise(vm, "ArgumentOutOfRangeException", "DnsServer.Start: port 1..65535"); return mcs_null(); }
    dns_stop(vm, d);
    d->port = (uint16_t)port;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return mcs__sock_fail(vm, "DnsServer.Start", errno);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons(d->port);
    int b = bind(fd, (struct sockaddr*)&sa, sizeof sa);
    if (b < 0 && mcs__host_port(errno, &d->port)) { sa.sin_port = htons(d->port); b = bind(fd, (struct sockaddr*)&sa, sizeof sa); }
    if (b < 0) { int e = errno; close(fd); return mcs__sock_fail(vm, "DnsServer.Start", e); }
    mcs__sock_prep(fd);
    d->fd = fd;
    mcs__net_track(vm, &d->resource, d, dns_fin);
    mcs__net_state_t* s = mcs__net_state(vm);
    if (s) { for (int i = 0; i < 2; i++) if (!s->dns[i]) { s->dns[i] = d; break; } }
    return mcs_null();
}
NATIVE(dns_stop_m) { DN(); dns_stop(vm, d); return mcs_null(); }
NATIVE(dns_poll) { DN(); return mcs_int(dns_service(d)); }
NATIVE(dns_running) { DN(); return mcs_bool(d->fd >= 0); }
NATIVE(dns_queries) { DN(); return mcs_int((mcs_int_t)d->queries); }
NATIVE(dns_getttl) { DN(); return mcs_int((mcs_int_t)d->ttl); }
NATIVE(dns_setttl) { DN(); INT_ARG(0, v); d->ttl = (uint32_t)(v < 0 ? 0 : v); return mcs_null(); }
NATIVE(dns_getdomain) { DN(); return mcs_string(vm, d->domain); }
NATIVE(dns_setdomain) {
    DN(); STR_ARG(0, v);
    if (!*v || strlen(v) >= sizeof d->domain) { mcs_raise(vm, "ArgumentException", "DnsServer.Domain: 1..63 characters (\"*\" = every name)"); return mcs_null(); }
    size_t i = 0; for (; v[i]; i++) d->domain[i] = (v[i] >= 'A' && v[i] <= 'Z') ? (char)(v[i] + 32) : v[i];
    d->domain[i] = 0;
    return mcs_null();
}
NATIVE(dns_getip) { DN(); char b[16]; mcs__ip_fmt(d->ip, b); return mcs_string(vm, b); }
NATIVE(dns_getport) { DN(); return mcs_int(d->port); }
static const mcs_reg_t dnss_members[] = {
    MCS_FN("Start", dns_start, -1), MCS_FN("Stop", dns_stop_m, 0), MCS_FN("Dispose", dns_stop_m, 0),
    MCS_FN("Poll", dns_poll, 0), MCS_GET("Running", dns_running), MCS_GET("Queries", dns_queries),
    MCS_GET("Ttl", dns_getttl), MCS_SET("Ttl", dns_setttl), MCS_GET("Domain", dns_getdomain), MCS_SET("Domain", dns_setdomain),
    MCS_GET("Address", dns_getip), MCS_GET("Port", dns_getport), MCS_REG_END };
static const mcs_class_def_t dns_def = { "DnsServer", sizeof(dns_t), dns_ctor, dns_fin, dnss_members, NULL };

/* ============================================================ HttpRequest */
typedef struct {
    mcs__conn_t c;               /* the client; fd -1 once the server finished the request */
    char method[8];
    char* head; size_t head_cap; /* request line + headers, NUL-split in place */
    const char* path; const char* query; const char* hdrs; size_t hdrs_len;
    char* body; size_t body_len, body_cap;
    char pp[160]; size_t pp_len; /* {param} captures: name\0value\0 ... */
    int status;
    uint8_t sent, head_only, cors;
    char* xh; size_t xh_len, xh_cap;   /* response headers from SetHeader */
} req_t;
static const mcs_class_def_t req_def;
static void req_free(mcs_vm_t* vm, req_t* r) {
    if (r->head) mcs_mem_realloc(vm, r->head, r->head_cap, 0);
    if (r->body) mcs_mem_realloc(vm, r->body, r->body_cap, 0);
    if (r->xh) mcs_mem_realloc(vm, r->xh, r->xh_cap, 0);
    r->head = r->body = r->xh = NULL; r->head_cap = r->body_cap = r->xh_cap = 0; r->body_len = r->xh_len = 0;
    r->path = r->query = r->hdrs = NULL; r->hdrs_len = 0;
}
static void req_fin(mcs_vm_t* vm, void* d) { req_t* r = (req_t*)d; mcs__conn_close(&r->c); req_free(vm, r); }
static void req_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)argv;
    req_t* r = (req_t*)mcs_userdata(self);
    r->c.fd = -1; r->status = 200;
    if (argc) mcs_raise(vm, "InvalidOperationException", "HttpRequest objects come from HttpServer handlers");
}
#define RQ() req_t* r = (req_t*)mcs_check_userdata(vm, self, &req_def); if (!r) return mcs_null()
static const char* reason_of(int s) {
    switch (s) {
    case 101: return "Switching Protocols"; case 200: return "OK"; case 201: return "Created"; case 202: return "Accepted";
    case 204: return "No Content"; case 301: return "Moved Permanently"; case 302: return "Found"; case 303: return "See Other";
    case 304: return "Not Modified"; case 307: return "Temporary Redirect"; case 308: return "Permanent Redirect";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 408: return "Request Timeout"; case 409: return "Conflict";
    case 411: return "Length Required"; case 413: return "Payload Too Large"; case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests"; case 500: return "Internal Server Error"; case 501: return "Not Implemented";
    case 503: return "Service Unavailable"; default: return s < 300 ? "OK" : s < 400 ? "Redirect" : s < 500 ? "Client Error" : "Server Error";
    }
}
/* status line + headers; len < 0 = no Content-Length (the body ends with the connection) */
static bool req_head(mcs_vm_t* vm, req_t* r, int status, const char* ctype, long len, const char* extra) {
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\n", status, reason_of(status));
    if (ctype && *ctype) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Type: %s\r\n", ctype);
    if (len >= 0) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Length: %ld\r\n", len);
    if (r->cors) n += snprintf(h + n, sizeof h - (size_t)n, "Access-Control-Allow-Origin: *\r\n");
    n += snprintf(h + n, sizeof h - (size_t)n, "%sConnection: close\r\n", extra ? extra : "");
    if (n >= (int)sizeof h) n = (int)sizeof h - 1;
    r->sent = 1;
    if (!raw_write(vm, &r->c, h, (size_t)n)) return false;
    if (r->xh_len && !raw_write(vm, &r->c, r->xh, r->xh_len)) return false;
    return raw_write(vm, &r->c, "\r\n", 2);
}
static bool req_send(mcs_vm_t* vm, req_t* r, int status, const char* ctype, const uint8_t* body, size_t n, const char* extra) {
    if (!req_head(vm, r, status, ctype, (long)n, extra)) return false;
    return r->head_only || !n || raw_write(vm, &r->c, body, n);
}
static bool req_usable(mcs_vm_t* vm, req_t* r, const char* op) {
    if (r->c.fd < 0) { mcs_raise(vm, "InvalidOperationException", "%s: the request is finished (answer inside the handler)", op); return false; }
    if (r->sent) { mcs_raise(vm, "InvalidOperationException", "%s: the response was already sent", op); return false; }
    return true;
}
/* text + content type of a C# value: strings as they are, byte[] binary, other values as JSON */
static bool body_of(mcs_vm_t* vm, mcs_value_t v, mcs_value_t* text, const char** ctype) {
    if (mcs_is_string(v)) { *text = v; if (!*ctype) *ctype = "text/html; charset=utf-8"; return true; }
    int k = mcs_obj_kind(v);
    if (k == MCS_O_ARRAY && mcs_len(v) && mcs_is_int(mcs_index(v, 0))) { *text = v; if (!*ctype) *ctype = "application/octet-stream"; return true; }
    if (mcs_is_null(v)) { *text = mcs_string(vm, ""); if (!*ctype) *ctype = "text/plain"; return true; }
    if (k == MCS_O_DICT || k == MCS_O_LIST || k == MCS_O_ARRAY || k == MCS_O_INSTANCE) {
        *text = call_json(vm, "Json.Stringify", v);
        if (mcs_has_exception(vm)) return false;
        if (!*ctype) *ctype = "application/json";
        return true;
    }
    *text = mcs_tostring(vm, v);
    if (!*ctype) *ctype = "text/plain; charset=utf-8";
    return !mcs_has_exception(vm);
}
static mcs_value_t req_send_value(mcs_vm_t* vm, req_t* r, int status, const char* ctype, mcs_value_t v, const char* op) {
    if (!req_usable(vm, r, op)) return mcs_null();
    mcs_value_t text;
    if (!body_of(vm, v, &text, &ctype)) return mcs_null();
    mcs_push_root(vm, text);
    const uint8_t* p; size_t n; uint8_t* heap;
    if (mcs__bytes_arg(vm, text, &p, &n, &heap)) { req_send(vm, r, status, ctype, p, n, NULL); mcs__bytes_free(vm, heap, n); }
    mcs_pop_root(vm, 1);
    return mcs_null();
}
NATIVE(rq_send) {       /* (body) | (status, body) | (status, contentType, body) */
    RQ();
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "HttpRequest.Send(body), Send(status, body) or Send(status, contentType, body)"); return mcs_null(); }
    int status = r->status; const char* ct = NULL; mcs_value_t body = argv[argc - 1];
    if (argc >= 2) { status = (int)mcs_to_int(vm, argv[0]); CHECK(); }
    if (argc == 3 && !mcs_is_null(argv[1])) { ct = mcs_to_cstr(vm, argv[1]); CHECK(); }
    if (status < 100 || status > 599) { mcs_raise(vm, "ArgumentOutOfRangeException", "HttpRequest.Send: status 100..599"); return mcs_null(); }
    return req_send_value(vm, r, status, ct, body, "HttpRequest.Send");
}
NATIVE(rq_sendjson) {   /* (value[, status]) */
    RQ();
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "HttpRequest.SendJson(value[, status])"); return mcs_null(); }
    OPT_INT(1, status, r->status);
    if (!req_usable(vm, r, "HttpRequest.SendJson")) return mcs_null();
    mcs_value_t t = call_json(vm, "Json.Stringify", argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    return req_send_value(vm, r, (int)status, "application/json", t, "HttpRequest.SendJson");
}
NATIVE(rq_status_send) {    /* (status) -> empty body with the reason phrase */
    RQ(); INT_ARG(0, status);
    if (!req_usable(vm, r, "HttpRequest.SendStatus")) return mcs_null();
    const char* t = status == 204 || status == 304 ? "" : reason_of((int)status);
    req_send(vm, r, (int)status, *t ? "text/plain" : NULL, (const uint8_t*)t, strlen(t), NULL);
    return mcs_null();
}
NATIVE(rq_redirect) {   /* (url[, status = 302]) */
    RQ(); STR_ARG(0, url); OPT_INT(1, status, 302);
    if (!req_usable(vm, r, "HttpRequest.Redirect")) return mcs_null();
    if (strpbrk(url, "\r\n") || strlen(url) > 400) { mcs_raise(vm, "ArgumentException", "HttpRequest.Redirect: bad URL"); return mcs_null(); }
    char extra[420]; snprintf(extra, sizeof extra, "Location: %s\r\n", url);
    req_send(vm, r, (int)status, NULL, NULL, 0, extra);
    return mcs_null();
}
NATIVE(rq_setheader) {
    RQ(); STR_ARG(0, name); STR_ARG(1, value);
    if (r->sent) { mcs_raise(vm, "InvalidOperationException", "HttpRequest.SetHeader: the response was already sent"); return mcs_null(); }
    if (!*name || strpbrk(name, "\r\n: ") || strpbrk(value, "\r\n")) { mcs_raise(vm, "ArgumentException", "HttpRequest.SetHeader: bad header"); return mcs_null(); }
    size_t add = strlen(name) + strlen(value) + 4;
    if (r->xh_len + add > 1024) { mcs_raise(vm, "ArgumentException", "HttpRequest.SetHeader: too many headers"); return mcs_null(); }
    if (r->xh_len + add + 1 > r->xh_cap) {
        size_t nc = r->xh_len + add + 64;
        char* nb = (char*)mcs_mem_realloc(vm, r->xh, r->xh_cap, nc);
        if (!nb) return mcs_null();
        r->xh = nb; r->xh_cap = nc;
    }
    r->xh_len += (size_t)snprintf(r->xh + r->xh_len, r->xh_cap - r->xh_len, "%s: %s\r\n", name, value);
    return mcs_null();
}
NATIVE(rq_getstatus) { RQ(); return mcs_int(r->status); }
NATIVE(rq_setstatus) { RQ(); INT_ARG(0, v); if (v < 100 || v > 599) { mcs_raise(vm, "ArgumentOutOfRangeException", "HttpRequest.Status: 100..599"); return mcs_null(); } r->status = (int)v; return mcs_null(); }

#if MCS_ENABLE_FS
static const char* mime_of(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    static const char* const t[][2] = {
        { ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" }, { ".css", "text/css" },
        { ".js", "application/javascript" }, { ".mjs", "application/javascript" }, { ".json", "application/json" },
        { ".txt", "text/plain; charset=utf-8" }, { ".cs", "text/plain; charset=utf-8" }, { ".csv", "text/csv" },
        { ".xml", "application/xml" }, { ".svg", "image/svg+xml" }, { ".png", "image/png" }, { ".jpg", "image/jpeg" },
        { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" }, { ".ico", "image/x-icon" }, { ".webp", "image/webp" },
        { ".wasm", "application/wasm" }, { ".bin", "application/octet-stream" }, { ".pdf", "application/pdf" },
        { ".woff2", "font/woff2" }, { ".mp3", "audio/mpeg" }, { ".wav", "audio/wav" } };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        const char* a = t[i][0]; const char* b = dot; size_t k = 0;
        while (a[k] && b[k] && (b[k] | 0x20) == a[k]) k++;
        if (!a[k] && !b[k]) return t[i][1];
    }
    return "application/octet-stream";
}
/* stream a file: 1 sent, 0 not found (nothing sent), -1 error raised */
static int send_file(mcs_vm_t* vm, req_t* r, const char* path0, const char* ctype) {
    mcs_vfs_t* vfs = (mcs_vfs_t*)mcs_get_ext(vm, MCS_EXT_VFS);
    if (!vfs) { mcs_raise(vm, "NotSupportedException", "HttpRequest.SendFile: no filesystem"); return -1; }
    char path[192], gz[200];
    if (strstr(path0, "..")) return 0;
    snprintf(path, sizeof path, "%s", path0);
    mcs_vfs_stat_t st;
    if (mcs_vfs_stat(vfs, path, &st) == 0 && st.is_dir) {
        size_t l = strlen(path);
        if (!l || path[l - 1] != '/') path[l++] = '/';
        if (l + sizeof "index.html" > sizeof path) return 0;
        memcpy(path + l, "index.html", sizeof "index.html");
    }
    const char* ct = ctype ? ctype : mime_of(path);
    bool zipped = false;
    snprintf(gz, sizeof gz, "%s.gz", path);
    size_t vl; const char* ae = r->hdrs ? hfind(r->hdrs, r->hdrs_len, "Accept-Encoding", &vl) : NULL;
    if (ae && has_token(ae, vl, "gzip") && mcs_vfs_stat(vfs, gz, &st) == 0 && !st.is_dir) zipped = true;
    else if (mcs_vfs_stat(vfs, path, &st) != 0 || st.is_dir) return 0;
    mcs_vfs_file_t f;
    if (mcs_vfs_open(vfs, zipped ? gz : path, MCS_VFS_READ, &f) != 0) return 0;
    bool ok = req_head(vm, r, r->status, ct, (long)st.size, zipped ? "Content-Encoding: gzip\r\n" : NULL);
    if (!r->head_only) {
        uint8_t buf[512];
        while (ok) {
            int n = mcs_vfs_read(&f, buf, sizeof buf);
            if (n <= 0) break;
            ok = raw_write(vm, &r->c, buf, (size_t)n);
            if (mcs__net_yield(vm)) break;
        }
    }
    mcs_vfs_close(&f);
    return 1;
}
NATIVE(rq_sendfile) {   /* (path[, contentType]) -> false when the file does not exist */
    RQ(); STR_ARG(0, path); OPT_STR(1, ct);
    if (!req_usable(vm, r, "HttpRequest.SendFile")) return mcs_null();
    int k = send_file(vm, r, path, ct);
    return k < 0 ? mcs_null() : mcs_bool(k == 1);
}
#endif

static bool is_form(req_t* r) {
    size_t vl; const char* ct = r->hdrs ? hfind(r->hdrs, r->hdrs_len, "Content-Type", &vl) : NULL;
    return ct && has_token(ct, vl, "application/x-www-form-urlencoded");
}
static mcs_value_t param(mcs_vm_t* vm, req_t* r, const char* name, bool* found) {
    *found = false;
    for (size_t i = 0; i < r->pp_len;) {          /* {param} captures */
        const char* n = r->pp + i; const char* v = n + strlen(n) + 1;
        if (!strcmp(n, name)) { *found = true; return mcs_string(vm, v); }
        i = (size_t)(v - r->pp) + strlen(v) + 1;
    }
    if (r->query) { mcs_value_t v = form_get(vm, r->query, strlen(r->query), name, found); if (*found) return v; }
    if (r->body && is_form(r)) return form_get(vm, r->body, r->body_len, name, found);
    return mcs_null();
}
NATIVE(rq_param) {      /* (name[, default]) -> string, default (null) when missing */
    RQ(); STR_ARG(0, name);
    bool f; mcs_value_t v = param(vm, r, name, &f);
    return f ? v : (argc > 1 ? argv[1] : mcs_null());
}
NATIVE(rq_hasparam) { RQ(); STR_ARG(0, name); bool f; param(vm, r, name, &f); return mcs_bool(f); }
NATIVE(rq_method) { RQ(); return mcs_string(vm, r->method); }
NATIVE(rq_path) { RQ(); return mcs_string(vm, r->path ? r->path : ""); }
NATIVE(rq_query) { RQ(); return mcs_string(vm, r->query ? r->query : ""); }
NATIVE(rq_url) {
    RQ(); char b[640];
    snprintf(b, sizeof b, "%s%s%s", r->path ? r->path : "", r->query && *r->query ? "?" : "", r->query ? r->query : "");
    return mcs_string(vm, b);
}
NATIVE(rq_body) { RQ(); return mcs_string_n(vm, r->body ? r->body : "", r->body_len); }
NATIVE(rq_bytes) { RQ(); return mcs__bytes_new(vm, (const uint8_t*)r->body, r->body ? r->body_len : 0); }
NATIVE(rq_json) {
    RQ();
    if (!r->body_len) return mcs_null();
    mcs_value_t b = mcs_string_n(vm, r->body, r->body_len);
    mcs_push_root(vm, b);
    mcs_value_t v = call_json(vm, "Json.Parse", b);
    mcs_pop_root(vm, 1);
    return v;
}
NATIVE(rq_header) {
    RQ(); STR_ARG(0, name);
    size_t vl; const char* v = r->hdrs ? hfind(r->hdrs, r->hdrs_len, name, &vl) : NULL;
    return v ? mcs_string_n(vm, v, vl) : mcs_null();
}
NATIVE(rq_headers) { RQ(); return mcs_string_n(vm, r->hdrs ? r->hdrs : "", r->hdrs_len); }
NATIVE(rq_ctype) {
    RQ(); size_t vl; const char* v = r->hdrs ? hfind(r->hdrs, r->hdrs_len, "Content-Type", &vl) : NULL;
    return v ? mcs_string_n(vm, v, vl) : mcs_string(vm, "");
}
NATIVE(rq_clen) { RQ(); return mcs_int((mcs_int_t)r->body_len); }
NATIVE(rq_remote) { RQ(); return peer_ip(vm, r->c.fd); }
NATIVE(rq_sent) { RQ(); return mcs_bool(r->sent); }
NATIVE(rq_host) {
    RQ(); size_t vl; const char* v = r->hdrs ? hfind(r->hdrs, r->hdrs_len, "Host", &vl) : NULL;
    return v ? mcs_string_n(vm, v, vl) : mcs_string(vm, "");
}
NATIVE(rq_auth) {       /* (user, password[, realm]) -> true, or false after answering 401 */
    RQ(); STR_ARG(0, user); STR_ARG(1, pass); OPT_STR(2, realm);
    char want[200], enc[272];
    int n = snprintf(want, sizeof want, "%s:%s", user, pass);
    if (n < 0 || n >= (int)sizeof want) { mcs_raise(vm, "ArgumentException", "HttpRequest.Authenticate: user / password too long"); return mcs_null(); }
    b64_enc((const uint8_t*)want, (size_t)n, enc);
    size_t vl; const char* v = r->hdrs ? hfind(r->hdrs, r->hdrs_len, "Authorization", &vl) : NULL;
    if (v && vl > 6 && !strncmp(v, "Basic ", 6) && vl - 6 == strlen(enc) && !memcmp(v + 6, enc, vl - 6)) return mcs_bool(true);
    if (!req_usable(vm, r, "HttpRequest.Authenticate")) return mcs_null();
    char extra[128];
    snprintf(extra, sizeof extra, "WWW-Authenticate: Basic realm=\"%s\"\r\n", realm && !strpbrk(realm, "\"\r\n") ? realm : "MicroCS");
    req_send(vm, r, 401, "text/plain", (const uint8_t*)"Unauthorized", 12, extra);
    return mcs_bool(false);
}
NATIVE(rq_tostring) { RQ(); char b[300]; snprintf(b, sizeof b, "%s %s", r->method, r->path ? r->path : ""); return mcs_string(vm, b); }
static const mcs_reg_t req_members[] = {
    MCS_GET("Method", rq_method), MCS_GET("Path", rq_path), MCS_GET("Query", rq_query), MCS_GET("Url", rq_url),
    MCS_GET("Body", rq_body), MCS_GET("Bytes", rq_bytes), MCS_GET("Json", rq_json), MCS_GET("ContentType", rq_ctype),
    MCS_GET("ContentLength", rq_clen), MCS_GET("Headers", rq_headers), MCS_FN("Header", rq_header, 1),
    MCS_GET("Host", rq_host), MCS_FN("Param", rq_param, -1), MCS_FN("HasParam", rq_hasparam, 1),
    MCS_GET("RemoteAddress", rq_remote), MCS_GET("Sent", rq_sent),
    MCS_GET("Status", rq_getstatus), MCS_SET("Status", rq_setstatus),
    MCS_FN("Send", rq_send, -1), MCS_FN("SendJson", rq_sendjson, -1), MCS_FN("SendStatus", rq_status_send, 1),
#if MCS_ENABLE_FS
    MCS_FN("SendFile", rq_sendfile, -1),
#endif
    MCS_FN("Redirect", rq_redirect, -1), MCS_FN("SetHeader", rq_setheader, 2), MCS_FN("Authenticate", rq_auth, -1),
    MCS_FN("ToString", rq_tostring, 0), MCS_REG_END };
static const mcs_class_def_t req_def = { "HttpRequest", sizeof(req_t), req_ctor, req_fin, req_members, NULL };

/* ============================================================ HttpServer */
enum { RT_HTTP = 1, RT_WS = 2, RT_FILES = 3 };
enum { M_GET = 1, M_POST = 2, M_PUT = 4, M_DELETE = 8, M_PATCH = 16, M_HEAD = 32, M_OPTIONS = 64, M_ANY = 0x7F };
typedef struct { uint8_t kind, methods; int16_t h, h_open, h_close; char path[64]; char dir[64]; } route_t;
typedef struct {
    mcs__net_resource_t resource;
    int fd; uint16_t port; uint8_t ip[4];
    uint8_t cors, stop_req;
    int pin;                     /* pinned list: [not-found handler, route handlers ...] */
    int wpin;                    /* pinned array of open WebSockets (MCS_WS_MAX_CLIENTS) */
    int16_t not_found;
    int nroutes;
    uint32_t requests;
    int8_t ws_route[MCS_WS_MAX_CLIENTS];   /* route of each WebSocket slot */
    route_t routes[MCS_HTTP_SERVER_MAX_ROUTES];
} srv_t;
static const mcs_class_def_t srv_def;
static void srv_close(srv_t* s) { if (s->fd >= 0) close(s->fd); s->fd = -1; }
static void srv_fin(mcs_vm_t* vm, void* d) {
    srv_t* s = (srv_t*)d;
    mcs__net_untrack(&s->resource);
    srv_close(s); s->stop_req = 1;
    if (s->pin >= 0) mcs_unpin(vm, s->pin);
    if (s->wpin >= 0) mcs_unpin(vm, s->wpin);
    s->pin = s->wpin = -1;
}
/* Script-scope disposal runs before GC; pinned children are still valid here.
 * A GC/free finalizer must not dereference children already swept by mcs_free_objects. */
static void srv_scope_close(mcs_vm_t* vm, void* d) {
    srv_t* s = (srv_t*)d;
    if (s->wpin >= 0) {
        mcs_value_t arr = mcs_pinned(vm, s->wpin);
        for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) {
            ws_t* w = (ws_t*)mcs_userdata_of(mcs_index(arr, (uint32_t)i), &ws_def);
            if (w) ws_fin(vm, w);
        }
    }
    srv_fin(vm, d);
}
static void srv_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    srv_t* s = (srv_t*)mcs_userdata(self);
    s->fd = -1; s->pin = s->wpin = -1; s->not_found = -1; s->port = 80;
    mcs__net_track(vm, &s->resource, s, srv_scope_close);
    if (argc > 2) { mcs_raise(vm, "ArgumentException", "new HttpServer([port]) or new HttpServer(address, port)"); return; }
    mcs_int_t port = 80;
    if (argc == 2) {
        const char* a = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return;
        if (!mcs__ip_parse(a, s->ip)) { mcs_raise(vm, "ArgumentException", "HttpServer: '%s' is not an IPv4 address", a); return; }
        port = mcs_to_int(vm, argv[1]);
    } else if (argc == 1) port = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return;
    if (port < 0 || port > 65535) { mcs_raise(vm, "ArgumentOutOfRangeException", "HttpServer: port 0..65535"); return; }
    s->port = (uint16_t)port;
    mcs_value_t list = mcs_new_list(vm);
    mcs_push_root(vm, list);
    mcs_list_add(vm, list, mcs_null());
    s->pin = mcs_pin(vm, list);
    mcs_value_t arr = mcs_new_array(vm, MCS_WS_MAX_CLIENTS);
    mcs_push_root(vm, arr);
    s->wpin = mcs_pin(vm, arr);
    mcs_pop_root(vm, 2);
    if (s->pin < 0 || s->wpin < 0) mcs_raise(vm, "OutOfMemoryException", "HttpServer: no free pin handles (MCS_MAX_PINS)");
}
#define SV() srv_t* s = (srv_t*)mcs_check_userdata(vm, self, &srv_def); if (!s) return mcs_null()
static int16_t add_handler(mcs_vm_t* vm, srv_t* s, mcs_value_t fn) {
    if (mcs_is_null(fn)) return -1;
    if (mcs_arity(fn) == -2) { mcs_raise(vm, "ArgumentException", "HttpServer: the handler must be a function"); return -1; }
    mcs_value_t list = mcs_pinned(vm, s->pin);
    mcs_list_add(vm, list, fn);
    return (int16_t)(mcs_len(list) - 1);
}
static route_t* add_route(mcs_vm_t* vm, srv_t* s, int kind, int methods, const char* path, const char* op) {
    if (s->nroutes >= MCS_HTTP_SERVER_MAX_ROUTES) { mcs_raise(vm, "InvalidOperationException", "%s: more than %d routes (MCS_HTTP_SERVER_MAX_ROUTES)", op, MCS_HTTP_SERVER_MAX_ROUTES); return NULL; }
    if (path[0] != '/' || strlen(path) >= sizeof s->routes[0].path) { mcs_raise(vm, "ArgumentException", "%s: the path must start with / (up to 63 characters)", op); return NULL; }
    route_t* r = &s->routes[s->nroutes++];
    memset(r, 0, sizeof *r);
    r->kind = (uint8_t)kind; r->methods = (uint8_t)methods; r->h = r->h_open = r->h_close = -1;
    snprintf(r->path, sizeof r->path, "%s", path);
    return r;
}
static int method_bit(const char* m) {
    static const char* const n[] = { "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS" };
    if (!strcmp(m, "*") || !strcmp(m, "ANY")) return M_ANY;
    for (int i = 0; i < 7; i++) {
        const char* a = n[i]; const char* b = m; while (*a && (*b & ~0x20) == *a) { a++; b++; }
        if (!*a && !*b) return 1 << i;
    }
    return 0;
}
static mcs_value_t route_m(mcs_vm_t* vm, mcs_value_t self, int methods, mcs_value_t path, mcs_value_t fn, const char* op) {
    SV();
    const char* p = mcs_to_cstr(vm, path); CHECK();
    route_t* r = add_route(vm, s, RT_HTTP, methods, p, op);
    if (!r) return mcs_null();
    r->h = add_handler(vm, s, fn);
    if (r->h < 0) { s->nroutes--; if (!mcs_has_exception(vm)) mcs_raise(vm, "ArgumentNullException", "%s: handler", op); }
    return self;
}
NATIVE(sv_on) {         /* (path, handler) or (method, path, handler) */
    if (argc == 2) return route_m(vm, self, M_ANY, argv[0], argv[1], "HttpServer.On");
    if (argc != 3) { mcs_raise(vm, "ArgumentException", "HttpServer.On([method, ]path, handler)"); return mcs_null(); }
    STR_ARG(0, m);
    int bits = 0;
    char tmp[64]; snprintf(tmp, sizeof tmp, "%s", m);
    for (char* t = strtok(tmp, ",| "); t; t = strtok(NULL, ",| ")) {
        int b = method_bit(t);
        if (!b) { mcs_raise(vm, "ArgumentException", "HttpServer.On: unknown method '%s'", t); return mcs_null(); }
        bits |= b;
    }
    return route_m(vm, self, bits, argv[1], argv[2], "HttpServer.On");
}
NATIVE(sv_get) { return route_m(vm, self, M_GET | M_HEAD, argv[0], argv[1], "HttpServer.Get"); }
NATIVE(sv_post) { return route_m(vm, self, M_POST, argv[0], argv[1], "HttpServer.Post"); }
NATIVE(sv_put) { return route_m(vm, self, M_PUT, argv[0], argv[1], "HttpServer.Put"); }
NATIVE(sv_delete) { return route_m(vm, self, M_DELETE, argv[0], argv[1], "HttpServer.Delete"); }
NATIVE(sv_patch) { return route_m(vm, self, M_PATCH, argv[0], argv[1], "HttpServer.Patch"); }
NATIVE(sv_notfound) {
    SV();
    mcs_value_t list = mcs_pinned(vm, s->pin);
    mcs_set_index(list, 0, argv[0]);
    s->not_found = mcs_is_null(argv[0]) ? -1 : 0;
    return self;
}
NATIVE(sv_websocket) {  /* (path, onMessage[, onOpen[, onClose]]) */
    SV();
    if (argc < 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "HttpServer.WebSocket(path, onMessage[, onOpen[, onClose]])"); return mcs_null(); }
    STR_ARG(0, p);
    route_t* r = add_route(vm, s, RT_WS, M_GET, p, "HttpServer.WebSocket");
    if (!r) return mcs_null();
    r->h = add_handler(vm, s, argv[1]); CHECK();
    if (argc > 2) { r->h_open = add_handler(vm, s, argv[2]); CHECK(); }
    if (argc > 3) { r->h_close = add_handler(vm, s, argv[3]); CHECK(); }
    return self;
}
#if MCS_ENABLE_FS
NATIVE(sv_files) {      /* (urlPrefix, directory) */
    SV(); STR_ARG(0, prefix); STR_ARG(1, dir);
    if (strlen(dir) >= sizeof s->routes[0].dir) { mcs_raise(vm, "ArgumentException", "HttpServer.ServeFiles: directory name too long"); return mcs_null(); }
    route_t* r = add_route(vm, s, RT_FILES, M_GET | M_HEAD, prefix, "HttpServer.ServeFiles");
    if (!r) return mcs_null();
    size_t l = strlen(r->path);
    if (l > 1 && r->path[l - 1] == '/') r->path[l - 1] = 0;          /* "/static/" -> "/static" */
    snprintf(r->dir, sizeof r->dir, "%s", dir);
    l = strlen(r->dir);
    if (l > 1 && r->dir[l - 1] == '/') r->dir[l - 1] = 0;
    return self;
}
#endif
NATIVE(sv_start) {
    SV();
    if (s->pin < 0) { mcs_raise(vm, "ObjectDisposedException", "HttpServer: script scope ended; create a new server"); return mcs_null(); }
    if (s->fd >= 0) return self;
    uint16_t port = s->port;
    int fd = mcs__tcp_listen(vm, s->ip, &port, 4, "HttpServer.Start");
    if (fd < 0) return mcs_null();
    s->fd = fd; s->port = port; s->stop_req = 0;
    mcs__net_track(vm, &s->resource, s, srv_scope_close);
    return self;
}
static void ws_slot_close(mcs_vm_t* vm, srv_t* s, int i, bool call) {
    mcs_value_t arr = mcs_pinned(vm, s->wpin);
    mcs_value_t wv = mcs_index(arr, (uint32_t)i);
    if (mcs_is_null(wv)) return;
    ws_t* w = (ws_t*)mcs_userdata_of(wv, &ws_def);
    int ri = s->ws_route[i];
    mcs_set_index(arr, (uint32_t)i, mcs_null());
    if (w && w->state == WS_OPEN) { ws_send_close(vm, w, 1001, "server stopped"); ws_drop(vm, w); }
    if (call && ri >= 0 && ri < s->nroutes && s->routes[ri].h_close >= 0) {
        mcs_push_root(vm, wv);
        mcs_value_t fn = mcs_index(mcs_pinned(vm, s->pin), (uint32_t)s->routes[ri].h_close);
        mcs_call_value(vm, fn, mcs_arity(fn) == 0 ? 0 : 1, &wv, NULL);
        mcs_pop_root(vm, 1);
    }
}
NATIVE(sv_stop) {
    SV();
    srv_close(s);
    s->stop_req = 1;
    for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) ws_slot_close(vm, s, i, false);
    return mcs_null();
}

NATIVE(sv_dispose) { SV(); srv_scope_close(vm, s); return mcs_null(); }

/* does `path` match the route pattern? {name} segments are captured into r->pp */
static bool path_match(const char* pat, const char* path, req_t* rq) {
    size_t pp = 0;
    const char* a = pat; const char* b = path;
    while (*a) {
        if (a[0] == '*' && !a[1]) return true;                         /* trailing * */
        if (*a == '{') {
            const char* e = strchr(a, '}');
            if (!e) return false;
            const char* v = b; while (*b && *b != '/') b++;
            if (b == v) return false;
            size_t nl = (size_t)(e - a - 1), vl = (size_t)(b - v);
            if (rq && pp + nl + vl + 2 <= sizeof rq->pp) {
                memcpy(rq->pp + pp, a + 1, nl); pp += nl; rq->pp[pp++] = 0;
                memcpy(rq->pp + pp, v, vl); vl = url_decode(rq->pp + pp, vl); pp += vl; rq->pp[pp++] = 0;
            }
            a = e + 1;
            continue;
        }
        if (*a != *b) return false;
        a++; b++;
    }
    if (*b == '/' && !b[1] && a > pat && a[-1] != '/') b++;          /* "/led/" matches "/led" */
    if (*b) return false;
    if (rq) rq->pp_len = pp;
    return true;
}
#if MCS_ENABLE_FS
static bool files_match(const route_t* r, const char* path) {
    size_t l = strlen(r->path);
    if (l == 1) return true;                                          /* "/" serves everything */
    return !strncmp(path, r->path, l) && (path[l] == '/' || !path[l]);
}
#endif
/* read the request head (+ the start of the body) into a heap buffer; true = complete */
static bool read_head(mcs_vm_t* vm, mcs__conn_t* c, char** buf, size_t* cap, size_t* len, size_t* head_end) {
    size_t c0 = 1024;
    *buf = (char*)mcs_mem_realloc(vm, NULL, 0, c0); if (!*buf) return false;
    *cap = c0; *len = 0;
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        uint32_t el = (uint32_t)(mcs_ticks(vm) - t0);
        if (el >= MCS_HTTP_SERVER_TIMEOUT) return false;
        if (*len + 1 >= *cap) {
            if (*cap >= MCS_HTTP_MAX_HEADER + 1024) return false;
            size_t nc = *cap * 2;
            char* nb = (char*)mcs_mem_realloc(vm, *buf, *cap, nc); if (!nb) return false;
            *buf = nb; *cap = nc;
        }
        int r = mcs__conn_read(vm, c, (uint8_t*)*buf + *len, *cap - *len - 1, MCS_HTTP_SERVER_TIMEOUT - el);
        if (r <= 0) return false;
        size_t from = *len >= 3 ? *len - 3 : 0;
        *len += (size_t)r;
        (*buf)[*len] = 0;
        char* e = strstr(*buf + from, "\r\n\r\n");
        if (e) { *head_end = (size_t)(e - *buf) + 4; return true; }
    }
}
static void simple(mcs_vm_t* vm, mcs__conn_t* c, int status, const char* msg) {
    char b[256];
    int n = snprintf(b, sizeof b, "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                     status, reason_of(status), (int)strlen(msg), msg);
    raw_write(vm, c, b, (size_t)n);
}
static int ws_upgrade(mcs_vm_t* vm, srv_t* s, int ri, req_t* rq, mcs__conn_t* c);
/* one client: 0 done, -1 an exception / abort is pending */
static int handle(mcs_vm_t* vm, srv_t* s, int fd) {
    mcs__conn_t c = { fd, NULL, NULL };
    mcs__sock_prep(fd);
    char* buf; size_t cap = 0, len = 0, he = 0;
    if (!read_head(vm, &c, &buf, &cap, &len, &he)) {
        if (buf) { if (len) simple(vm, &c, mcs_has_exception(vm) ? 500 : 408, "request timeout or too large"); mcs_mem_realloc(vm, buf, cap, 0); }
        mcs__conn_close(&c);
        return mcs_has_exception(vm) ? -1 : 0;
    }
    s->requests++;
    /* request line: METHOD SP target SP version */
    char* sp1 = strchr(buf, ' ');
    char* sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
    char* eol = strstr(buf, "\r\n");
    if (!sp1 || !sp2 || sp2 > eol || sp1 - buf >= 8 || sp1[1] != '/') { simple(vm, &c, 400, "bad request"); mcs_mem_realloc(vm, buf, cap, 0); mcs__conn_close(&c); return 0; }
    mcs_value_t ro;
    if (mcs_new_object(vm, "HttpRequest", 0, NULL, &ro) != MCS_OK) { mcs_mem_realloc(vm, buf, cap, 0); mcs__conn_close(&c); return -1; }
    mcs_push_root(vm, ro);
    req_t* rq = (req_t*)mcs_userdata(ro);
    rq->c = c; rq->cors = s->cors; rq->head = buf; rq->head_cap = cap;
    memcpy(rq->method, buf, (size_t)(sp1 - buf)); rq->method[sp1 - buf] = 0;
    *sp2 = 0;
    char* target = sp1 + 1;
    char* q = strchr(target, '?');
    if (q) { *q = 0; rq->query = q + 1; } else rq->query = "";
    char* hash = strchr(target, '#'); if (hash) *hash = 0;
    rq->path = target;
    rq->hdrs = eol + 2; rq->hdrs_len = he - 2 - (size_t)(eol + 2 - buf);
    int mbit = method_bit(rq->method);
    rq->head_only = mbit == M_HEAD;
    /* body */
    size_t vl; const char* cl = hfind(rq->hdrs, rq->hdrs_len, "Content-Length", &vl);
    const char* te = hfind(rq->hdrs, rq->hdrs_len, "Transfer-Encoding", &vl);
    long blen = cl ? strtol(cl, NULL, 10) : 0;
    int rc = 0;
    if (te && has_token(te, vl, "chunked")) { simple(vm, &rq->c, 411, "send a Content-Length"); goto done; }
    if (blen < 0 || blen > MCS_HTTP_MAX_BODY) { simple(vm, &rq->c, 413, "body too large (MCS_HTTP_MAX_BODY)"); goto done; }
    if (blen > 0) {
        rq->body = (char*)mcs_mem_realloc(vm, NULL, 0, (size_t)blen + 1);
        if (!rq->body) { rc = -1; goto done; }
        rq->body_cap = (size_t)blen + 1;
        size_t have = len - he; if (have > (size_t)blen) have = (size_t)blen;
        memcpy(rq->body, buf + he, have);
        while (have < (size_t)blen) {
            int r = mcs__conn_read(vm, &rq->c, (uint8_t*)rq->body + have, (size_t)blen - have, MCS_HTTP_SERVER_TIMEOUT);
            if (r <= 0) { if (r == -EINTR) rc = -1; else simple(vm, &rq->c, 408, "body timeout"); goto done; }
            have += (size_t)r;
        }
        rq->body[blen] = 0; rq->body_len = (size_t)blen;
    }
    /* routes, in the order they were added */
    {
        bool path_known = false;
        for (int i = 0; i < s->nroutes; i++) {
            route_t* rt = &s->routes[i];
            if (rt->kind == RT_FILES) {
#if MCS_ENABLE_FS
                if (!(mbit & rt->methods) || !files_match(rt, rq->path)) continue;
                char fp[192];
                const char* rest = rq->path + (strlen(rt->path) == 1 ? 0 : strlen(rt->path));
                snprintf(fp, sizeof fp, "%s%s%s", rt->dir, *rest == '/' ? "" : "/", rest);
                fp[url_decode(fp, strlen(fp))] = 0;
                int k = send_file(vm, rq, fp, NULL);
                if (k < 0) { rc = -1; goto done; }
                if (k == 1) goto done;
#endif
                continue;
            }
            if (!path_match(rt->path, rq->path, rq)) continue;
            path_known = true;
            if (!(mbit & rt->methods)) continue;
            if (rt->kind == RT_WS) {
                size_t ul; const char* up = hfind(rq->hdrs, rq->hdrs_len, "Upgrade", &ul);
                if (!up || !has_token(up, ul, "websocket")) { simple(vm, &rq->c, 426, "WebSocket endpoint: upgrade required"); goto done; }
                rc = ws_upgrade(vm, s, i, rq, &rq->c);
                goto done;
            }
            mcs_value_t fn = mcs_index(mcs_pinned(vm, s->pin), (uint32_t)rt->h);
            mcs_value_t out = mcs_null();
            mcs_result_t r = mcs_call_value(vm, fn, mcs_arity(fn) == 0 ? 0 : 1, &ro, &out);
            if (r != MCS_OK || mcs_has_exception(vm)) { if (!rq->sent && rq->c.fd >= 0) simple(vm, &rq->c, 500, "handler failed"); rc = -1; goto done; }
            if (!rq->sent && rq->c.fd >= 0) {
                if (!mcs_is_null(out)) { mcs_push_root(vm, out); req_send_value(vm, rq, rq->status, NULL, out, "HttpServer"); mcs_pop_root(vm, 1); }
                else req_send(vm, rq, rq->status, "text/plain", NULL, 0, NULL);
                if (mcs_has_exception(vm)) rc = -1;
            }
            goto done;
        }
        if (mbit == M_OPTIONS && s->cors) {
            req_send(vm, rq, 204, NULL, NULL, 0, "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\nAccess-Control-Allow-Headers: *\r\n");
            goto done;
        }
        if (path_known) { simple(vm, &rq->c, 405, "method not allowed"); goto done; }
        if (s->not_found >= 0) {
            rq->status = 404;
            mcs_value_t fn = mcs_index(mcs_pinned(vm, s->pin), 0);
            mcs_value_t out = mcs_null();
            mcs_result_t r = mcs_call_value(vm, fn, mcs_arity(fn) == 0 ? 0 : 1, &ro, &out);
            if (r != MCS_OK || mcs_has_exception(vm)) { if (!rq->sent && rq->c.fd >= 0) simple(vm, &rq->c, 500, "handler failed"); rc = -1; goto done; }
            if (!rq->sent && rq->c.fd >= 0) {
                if (!mcs_is_null(out)) { mcs_push_root(vm, out); req_send_value(vm, rq, rq->status, NULL, out, "HttpServer"); mcs_pop_root(vm, 1); }
                else simple(vm, &rq->c, 404, "not found");
            }
            goto done;
        }
        simple(vm, &rq->c, 404, "not found");
    }
done:
    mcs__conn_close(&rq->c);       /* WebSocket upgrades took the fd over (rq->c.fd = -1) */
    req_free(vm, rq);
    mcs_pop_root(vm, 1);
    return rc ? -1 : (mcs_has_exception(vm) ? -1 : 0);
}
static int ws_upgrade(mcs_vm_t* vm, srv_t* s, int ri, req_t* rq, mcs__conn_t* c) {
    size_t kl; const char* key = hfind(rq->hdrs, rq->hdrs_len, "Sec-WebSocket-Key", &kl);
    if (!key || kl > 60) { simple(vm, c, 400, "missing Sec-WebSocket-Key"); return 0; }
    mcs_value_t arr = mcs_pinned(vm, s->wpin);
    int slot = -1;
    for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) {
        mcs_value_t v = mcs_index(arr, (uint32_t)i);
        ws_t* w = mcs_is_null(v) ? NULL : (ws_t*)mcs_userdata_of(v, &ws_def);
        if (!w || w->state != WS_OPEN) { if (w) ws_slot_close(vm, s, i, true); slot = i; break; }
    }
    if (slot < 0) { simple(vm, c, 503, "too many WebSocket clients (MCS_WS_MAX_CLIENTS)"); return 0; }
    char acc[32]; ws_accept_key(key, kl, acc);
    char h[192];
    int n = snprintf(h, sizeof h, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
    if (!raw_write(vm, c, h, (size_t)n)) return 0;
    mcs_value_t wv;
    if (mcs_new_object(vm, "WebSocket", 0, NULL, &wv) != MCS_OK) return -1;
    ws_t* w = (ws_t*)mcs_userdata(wv);
    w->c = *c; c->fd = -1;                       /* the WebSocket owns the socket now */
    mcs__net_track(vm, &w->resource, w, ws_fin);
    w->server = 1; w->state = WS_OPEN; w->timeout = 0;
    snprintf(w->path, sizeof w->path, "%s", rq->path);
    mcs_set_index(arr, (uint32_t)slot, wv);
    s->ws_route[slot] = (int8_t)ri;
    route_t* rt = &s->routes[ri];
    if (rt->h_open >= 0) {
        mcs_value_t fn = mcs_index(mcs_pinned(vm, s->pin), (uint32_t)rt->h_open);
        if (mcs_call_value(vm, fn, mcs_arity(fn) == 0 ? 0 : 1, &wv, NULL) != MCS_OK || mcs_has_exception(vm)) return -1;
    }
    return 0;
}
/* messages of one server-side WebSocket: >= 0 handled, -1 exception */
static int ws_service(mcs_vm_t* vm, srv_t* s, int i) {
    mcs_value_t wv = mcs_index(mcs_pinned(vm, s->wpin), (uint32_t)i);
    ws_t* w = mcs_is_null(wv) ? NULL : (ws_t*)mcs_userdata_of(wv, &ws_def);
    if (!w) return 0;
    int n = 0;
    mcs_push_root(vm, wv);
    for (;;) {
        mcs_value_t msg = mcs_null();
        int r = ws_next(vm, w, 0, true, &msg);
        if (r == -2) { mcs_pop_root(vm, 1); return -1; }
        if (r == -1) { ws_slot_close(vm, s, i, true); break; }
        if (r == 0) break;
        n++;
        int ri = s->ws_route[i];
        if (ri >= 0 && ri < s->nroutes && s->routes[ri].h >= 0) {
            mcs_push_root(vm, msg);
            int e = ws_dispatch(vm, wv, mcs_index(mcs_pinned(vm, s->pin), (uint32_t)s->routes[ri].h), msg);
            mcs_pop_root(vm, 1);
            if (e) { mcs_pop_root(vm, 1); return -1; }
        }
        if (w->state != WS_OPEN) { ws_slot_close(vm, s, i, true); break; }
    }
    mcs_pop_root(vm, 1);
    return mcs_has_exception(vm) ? -1 : n;
}
/* wait up to ms for clients / messages / DNS queries and handle them: count, -1 exception */
static int srv_poll(mcs_vm_t* vm, srv_t* s, uint32_t ms) {
    mcs__net_state_t* st = mcs__net_state(vm);
    uint32_t t0 = mcs_ticks(vm);
    int done = 0;
    for (;;) {
        if (mcs__net_yield(vm)) return -1;   /* also yield for Poll(0) / continuous ready traffic */
        fd_set rs; FD_ZERO(&rs);
        int mx = -1;
        if (s->fd >= 0) { FD_SET(s->fd, &rs); mx = s->fd; }
        mcs_value_t arr = mcs_pinned(vm, s->wpin);
        bool tls_ws = false;
        for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) {
            mcs_value_t v = mcs_index(arr, (uint32_t)i);
            ws_t* w = mcs_is_null(v) ? NULL : (ws_t*)mcs_userdata_of(v, &ws_def);
            if (!w) continue;
            if (w->state != WS_OPEN) { ws_slot_close(vm, s, i, true); if (mcs_has_exception(vm)) return -1; continue; }
            if (w->c.tls) { tls_ws = true; continue; }
            FD_SET(w->c.fd, &rs); if (w->c.fd > mx) mx = w->c.fd;
        }
        for (int i = 0; st && i < 2; i++) {
            dns_t* d = (dns_t*)st->dns[i];
            if (d && d->fd >= 0) { FD_SET(d->fd, &rs); if (d->fd > mx) mx = d->fd; }
        }
        if (mx < 0) return done;
        uint32_t el = (uint32_t)(mcs_ticks(vm) - t0);
        uint32_t left = el >= ms ? 0 : ms - el;
        if (left > MCS_NET_SLICE_MS) left = MCS_NET_SLICE_MS;
        if (tls_ws) left = 0;
        struct timeval tv = { 0, (long)left * 1000 };
        int r = select(mx + 1, &rs, NULL, NULL, &tv);
        int got = 0;
        if (r > 0 || tls_ws) {
            for (int i = 0; st && i < 2; i++) {
                dns_t* d = (dns_t*)st->dns[i];
                if (d && d->fd >= 0 && FD_ISSET(d->fd, &rs)) got += dns_service(d);
            }
            for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) {
                mcs_value_t v = mcs_index(mcs_pinned(vm, s->wpin), (uint32_t)i);
                ws_t* w = mcs_is_null(v) ? NULL : (ws_t*)mcs_userdata_of(v, &ws_def);
                if (!w || w->state != WS_OPEN || (!w->c.tls && !FD_ISSET(w->c.fd, &rs))) continue;
                int k = ws_service(vm, s, i);
                if (k < 0) return -1;
                got += k;
            }
            if (s->fd >= 0 && FD_ISSET(s->fd, &rs)) {
                int cfd = accept(s->fd, NULL, NULL);
                if (cfd >= 0) { got++; if (handle(vm, s, cfd) < 0) return -1; }
            }
        }
        done += got;
        if (s->stop_req) return done;
        el = (uint32_t)(mcs_ticks(vm) - t0);
        if (el >= ms || (done && el + MCS_NET_SLICE_MS > ms)) return done;
        if (mcs__net_yield(vm)) return -1;
        if (mcs_has_exception(vm)) return -1;
        if (s->fd < 0 && !st) return done;
    }
}
NATIVE(sv_poll) {       /* ([ms = 0]) -> requests + messages handled */
    SV(); OPT_INT(0, ms, 0);
    if (s->fd < 0) { mcs_raise(vm, "InvalidOperationException", "HttpServer.Poll: call Start() first"); return mcs_null(); }
    int n = srv_poll(vm, s, (uint32_t)(ms < 0 ? 0 : ms));
    return n < 0 ? mcs_null() : mcs_int(n);
}
NATIVE(sv_run) {        /* serve until Stop() (from a handler) or the script is stopped */
    SV();
    if (s->fd < 0) { sv_start(vm, self, 0, NULL); CHECK(); }
    s->stop_req = 0;
    while (!s->stop_req && s->fd >= 0) {
        if (srv_poll(vm, s, 200) < 0) return mcs_null();
        if (mcs_has_exception(vm)) return mcs_null();
    }
    return mcs_null();
}
NATIVE(sv_broadcast) {  /* (text | byte[]) -> clients reached */
    SV();
    mcs_value_t arr = mcs_pinned(vm, s->wpin);
    int n = 0;
    for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) {
        mcs_value_t v = mcs_index(arr, (uint32_t)i);
        ws_t* w = mcs_is_null(v) ? NULL : (ws_t*)mcs_userdata_of(v, &ws_def);
        if (!w || w->state != WS_OPEN) continue;
        ws_send_val(vm, w, argv[0], 0, "HttpServer.Broadcast");
        if (mcs_has_exception(vm)) return mcs_null();
        if (w->state == WS_OPEN) n++;
    }
    return mcs_int(n);
}
NATIVE(sv_sockets) {
    SV();
    mcs_value_t arr = mcs_pinned(vm, s->wpin);
    int n = 0;
    for (int i = 0; i < MCS_WS_MAX_CLIENTS; i++) {
        mcs_value_t v = mcs_index(arr, (uint32_t)i);
        ws_t* w = mcs_is_null(v) ? NULL : (ws_t*)mcs_userdata_of(v, &ws_def);
        if (w && w->state == WS_OPEN) n++;
    }
    mcs_value_t out = mcs_new_array(vm, (uint32_t)n);
    for (int i = 0, k = 0; i < MCS_WS_MAX_CLIENTS; i++) {
        mcs_value_t v = mcs_index(arr, (uint32_t)i);
        ws_t* w = mcs_is_null(v) ? NULL : (ws_t*)mcs_userdata_of(v, &ws_def);
        if (w && w->state == WS_OPEN) mcs_set_index(out, (uint32_t)k++, v);
    }
    return out;
}
NATIVE(sv_port) { SV(); return mcs_int(s->port); }
NATIVE(sv_running) { SV(); return mcs_bool(s->fd >= 0); }
NATIVE(sv_requests) { SV(); return mcs_int((mcs_int_t)s->requests); }
NATIVE(sv_getcors) { SV(); return mcs_bool(s->cors); }
NATIVE(sv_setcors) { SV(); s->cors = (uint8_t)mcs_truthy(argv[0]); return mcs_null(); }
static const mcs_reg_t srv_members[] = {
    MCS_FN("On", sv_on, -1), MCS_FN("Get", sv_get, 2), MCS_FN("Post", sv_post, 2), MCS_FN("Put", sv_put, 2),
    MCS_FN("Delete", sv_delete, 2), MCS_FN("Patch", sv_patch, 2), MCS_FN("NotFound", sv_notfound, 1),
    MCS_FN("WebSocket", sv_websocket, -1),
#if MCS_ENABLE_FS
    MCS_FN("ServeFiles", sv_files, 2),
#endif
    MCS_FN("Start", sv_start, 0), MCS_FN("Stop", sv_stop, 0), MCS_FN("Dispose", sv_dispose, 0),
    MCS_FN("Poll", sv_poll, -1), MCS_FN("Run", sv_run, 0), MCS_FN("Broadcast", sv_broadcast, 1),
    MCS_GET("WebSockets", sv_sockets), MCS_GET("Port", sv_port), MCS_GET("Running", sv_running),
    MCS_GET("Requests", sv_requests), MCS_GET("Cors", sv_getcors), MCS_SET("Cors", sv_setcors), MCS_REG_END };
static const mcs_class_def_t srv_def = { "HttpServer", sizeof(srv_t), srv_ctor, srv_fin, srv_members, NULL };

void mcs__srv_open(mcs_vm_t* vm) {
    mcs_register_class(vm, &srv_def);
    mcs_register_class(vm, &req_def);
    mcs_register_class(vm, &ws_def);
    mcs_register_class(vm, &dns_def);
}
#endif
