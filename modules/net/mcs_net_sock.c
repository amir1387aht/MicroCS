/*
 * MicroCS - BSD sockets for C# (MCS_ENABLE_SOCKETS): TcpClient, TcpListener, UdpClient,
 * Dns and Http. Runs on POSIX hosts and on lwIP (ESP-IDF); HTTPS and TcpClient.ConnectTls
 * go through the port's TLS hooks (mcs_net_t.tls_*, esp-tls on the ESP32).
 *
 *   var c = new TcpClient("example.com", 80);
 *   c.Write("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
 *   Console.WriteLine(c.ReadLine());                 // HTTP/1.0 200 OK
 *   var r = Http.Get("http://example.com/");         // HttpResponse: StatusCode, Body, Header(..)
 *
 * Every wait is sliced (MCS_NET_SLICE_MS) and calls mcs_safepoint(), so a blocked Read,
 * Accept or Http call stays abortable (Ctrl+C, time limits, Studio's Stop button).
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
#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif
#define RBUF 512

/* ------------------------------------------------------------ low level */
#if MCS_NET_LWIP && !defined(ESP_PLATFORM)
static void set_nonblock(int fd) { int one = 1; ioctl(fd, FIONBIO, &one); }   /* libc's O_NONBLOCK != lwIP's */
#else
static void set_nonblock(int fd) { int f = fcntl(fd, F_GETFL, 0); if (f >= 0) fcntl(fd, F_SETFL, f | O_NONBLOCK); }
#endif
static void no_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    (void)fd;
#endif
}
/* wait until fd is readable / writable: 1 ready, 0 timeout (timeout_ms 0 = forever), -1 aborted */
static int wait_fd(mcs_vm_t* vm, int fd, bool wr, uint32_t timeout_ms) {
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        uint32_t left = MCS_NET_SLICE_MS;
        if (timeout_ms) {
            uint32_t el = (uint32_t)(mcs_ticks(vm) - t0);
            if (el >= timeout_ms) return 0;
            if (timeout_ms - el < left) left = timeout_ms - el;
        }
        fd_set s; FD_ZERO(&s); FD_SET(fd, &s);
        struct timeval tv = { 0, (long)left * 1000 };
        int r = select(fd + 1, wr ? NULL : &s, wr ? &s : NULL, NULL, &tv);
        if (r > 0) return 1;
        if (r < 0 && errno != EINTR) return 1;            /* let the next call report the error */
        if (mcs_safepoint(vm)) return -1;
    }
}
static mcs_value_t sock_fail(mcs_vm_t* vm, const char* op, int e) {
    if (e == ETIMEDOUT || e == EAGAIN || e == EWOULDBLOCK) mcs_raise(vm, "TimeoutException", "%s: timed out", op);
    else mcs_raise(vm, "SocketException", "%s: %s (errno %d)", op, strerror(e), e);
    return mcs_null();
}

int mcs__resolve4(mcs_vm_t* vm, const char* host, uint8_t ip[4], uint32_t timeout_ms) {
    if (!host || !*host) return MCS_HAL_EINVAL;
    if (mcs__ip_parse(host, ip)) return 0;
    if (!strcmp(host, "localhost")) { ip[0] = 127; ip[1] = 0; ip[2] = 0; ip[3] = 1; return 0; }
    size_t n = strlen(host);
    mcs__net_state_t* s = mcs__net_state(vm);
    if (n > 6 && !strcmp(host + n - 6, ".local") && s && s->net && s->net->mdns_resolve) {
        int rc = s->net->mdns_resolve(s->net->ctx, host, timeout_ms ? timeout_ms : 2000, ip);
        if (rc >= 0) return 0;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return MCS_HAL_ENODEV;
    memcpy(ip, &((struct sockaddr_in*)res->ai_addr)->sin_addr, 4);
    freeaddrinfo(res);
    return 0;
}
static bool resolve_arg(mcs_vm_t* vm, const char* host, uint8_t ip[4], const char* op) {
    int rc = mcs__resolve4(vm, host, ip, MCS_NET_TIMEOUT);
    if (rc == 0) return true;
    if (!mcs_has_exception(vm)) mcs_raise(vm, "SocketException", "%s: cannot resolve host '%s'", op, host);
    return false;
}
static void mk_addr(struct sockaddr_in* a, const uint8_t ip[4], int port) {
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET; a->sin_port = htons((uint16_t)port);
    memcpy(&a->sin_addr, ip, 4);
}
static bool port_ok(mcs_vm_t* vm, mcs_int_t p, bool zero) {
    if (p < (zero ? 0 : 1) || p > 65535) { mcs_raise(vm, "ArgumentOutOfRangeException", "port: %d..65535", zero ? 0 : 1); return false; }
    return true;
}
/* non-blocking connect with timeout: fd or -1 with errno set (exception raised when aborted) */
static int tcp_connect(mcs_vm_t* vm, const uint8_t ip[4], int port, uint32_t timeout, int* err) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { *err = errno; return -1; }
    set_nonblock(fd); no_sigpipe(fd);
    struct sockaddr_in a; mk_addr(&a, ip, port);
    if (connect(fd, (struct sockaddr*)&a, sizeof a) < 0) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) { *err = errno; close(fd); return -1; }
        int w = wait_fd(vm, fd, true, timeout);
        if (w <= 0) { close(fd); *err = w == 0 ? ETIMEDOUT : EINTR; return -1; }
        int e = 0; socklen_t el = sizeof e;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el);
        if (e) { *err = e; close(fd); return -1; }
    }
    return fd;
}

/* ------------------------------------------------------------ connections (TCP or TLS) */
typedef mcs__conn_t conn_t;
static void conn_close(conn_t* c) {
    if (c->tls && c->net && c->net->tls_close) c->net->tls_close(c->tls);
    if (c->fd >= 0) close(c->fd);
    c->tls = NULL; c->fd = -1;
}
/* bytes written, or -errno */
static int conn_write(mcs_vm_t* vm, conn_t* c, const uint8_t* p, size_t n, uint32_t timeout) {
    size_t done = 0;
    while (done < n) {
        if (c->tls) {
            int r = c->net->tls_write(c->tls, p + done, n - done);
            if (r < 0) return r == MCS_HAL_ETIMEOUT ? -ETIMEDOUT : -EIO;
            done += (size_t)r;
            if (r == 0 && mcs_safepoint(vm)) return -EINTR;
            continue;
        }
        int w = wait_fd(vm, c->fd, true, timeout);
        if (w < 0) return -EINTR;
        if (w == 0) return -ETIMEDOUT;
        ssize_t r = send(c->fd, p + done, n - done, SEND_FLAGS);
        if (r < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue; return -errno; }
        done += (size_t)r;
    }
    return (int)done;
}
/* bytes read (0 = closed), or -errno (-ETIMEDOUT) */
static int conn_read(mcs_vm_t* vm, conn_t* c, uint8_t* buf, size_t n, uint32_t timeout) {
    if (c->fd < 0 && !c->tls) return 0;
    if (c->tls) {
        uint32_t t0 = mcs_ticks(vm);
        for (;;) {
            int r = c->net->tls_read(c->tls, buf, n, MCS_NET_SLICE_MS * 5);
            if (r >= 0) return r;
            if (r != MCS_HAL_ETIMEOUT) return -EIO;
            if (timeout && (uint32_t)(mcs_ticks(vm) - t0) >= timeout) return -ETIMEDOUT;
            if (mcs_safepoint(vm)) return -EINTR;
        }
    }
    for (;;) {
        int w = wait_fd(vm, c->fd, false, timeout);
        if (w < 0) return -EINTR;
        if (w == 0) return -ETIMEDOUT;
        ssize_t r = recv(c->fd, buf, n, 0);
        if (r >= 0) return (int)r;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
        if (errno == ECONNRESET) return 0;
        return -errno;
    }
}
static int conn_open(mcs_vm_t* vm, conn_t* c, const char* host, int port, bool tls, uint32_t timeout, const char* ca, const char* op) {
    mcs__net_state_t* s = mcs__net_state(vm);
    c->fd = -1; c->tls = NULL; c->net = s ? s->net : NULL;
    if (tls) {
        if (!c->net || !c->net->tls_open) {
            mcs_raise(vm, "NotSupportedException", "%s: TLS is not available on this port (Net.Has(\"Tls\"))", op);
            return -1;
        }
        int err = 0;
        c->tls = c->net->tls_open(c->net->ctx, host, port, timeout ? timeout : MCS_NET_TIMEOUT, ca, &err);
        if (!c->tls) {
            if (err == MCS_HAL_ETIMEOUT) mcs_raise(vm, "TimeoutException", "%s: TLS connection to %s:%d timed out", op, host, port);
            else mcs_raise(vm, "SocketException", "%s: TLS connection to %s:%d failed (%d)", op, host, port, err);
            return -1;
        }
        return 0;
    }
    uint8_t ip[4];
    if (!resolve_arg(vm, host, ip, op)) return -1;
    int err = 0;
    c->fd = tcp_connect(vm, ip, port, timeout ? timeout : MCS_NET_TIMEOUT, &err);
    if (c->fd < 0) {
        if (!mcs_has_exception(vm) && err != EINTR) {
            if (err == ETIMEDOUT) mcs_raise(vm, "TimeoutException", "%s: connecting to %s:%d timed out", op, host, port);
            else mcs_raise(vm, "SocketException", "%s: cannot connect to %s:%d: %s", op, host, port, strerror(err));
        }
        return -1;
    }
    return 0;
}

/* ---- shared with mcs_net_srv.c (HttpServer, WebSocket, DnsServer) */
int mcs__wait_fd(mcs_vm_t* vm, int fd, bool wr, uint32_t timeout_ms) { return wait_fd(vm, fd, wr, timeout_ms); }
void mcs__sock_prep(int fd) { set_nonblock(fd); no_sigpipe(fd); }
mcs_value_t mcs__sock_fail(mcs_vm_t* vm, const char* op, int e) { return sock_fail(vm, op, e); }
void mcs__conn_close(mcs__conn_t* c) { conn_close(c); }
int mcs__conn_write(mcs_vm_t* vm, mcs__conn_t* c, const uint8_t* p, size_t n, uint32_t timeout) { return conn_write(vm, c, p, n, timeout); }
int mcs__conn_read(mcs_vm_t* vm, mcs__conn_t* c, uint8_t* buf, size_t n, uint32_t timeout) { return conn_read(vm, c, buf, n, timeout); }
int mcs__conn_open(mcs_vm_t* vm, mcs__conn_t* c, const char* host, int port, bool tls, uint32_t timeout, const char* ca, const char* op) {
    return conn_open(vm, c, host, port, tls, timeout, ca, op);
}
/* PC simulator: ports below 1024 need root there, so HttpServer(80) / DnsServer (53) move to
 * port + 8000 (8080, 8053) with a note instead of failing. Boards never get here. */
bool mcs__host_port(int err, uint16_t* port) {
#if !defined(ESP_PLATFORM) && !MCS_NET_LWIP
    if (err != EACCES || *port == 0 || *port >= 1024) return false;
    fprintf(stderr, "[net] port %u needs root on this PC: using %u\n", (unsigned)*port, (unsigned)(*port + 8000));
    *port = (uint16_t)(*port + 8000);
    return true;
#else
    (void)err; (void)port; return false;
#endif
}
int mcs__tcp_listen(mcs_vm_t* vm, const uint8_t ip[4], uint16_t* port, int backlog, const char* op) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { sock_fail(vm, op, errno); return -1; }
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; mk_addr(&a, ip, *port);
    int b = bind(fd, (struct sockaddr*)&a, sizeof a);
    if (b < 0 && mcs__host_port(errno, port)) { mk_addr(&a, ip, *port); b = bind(fd, (struct sockaddr*)&a, sizeof a); }
    if (b < 0 || listen(fd, backlog < 1 ? 1 : backlog) < 0) {
        int e = errno; close(fd); sock_fail(vm, op, e); return -1;
    }
    set_nonblock(fd);
    socklen_t al = sizeof a;
    if (getsockname(fd, (struct sockaddr*)&a, &al) == 0) *port = ntohs(a.sin_port);
    return fd;
}

/* ============================================================ TcpClient */
typedef struct {
    conn_t c;
    uint32_t timeout;            /* ms, 0 = wait forever */
    uint16_t rpos, rlen;
    uint8_t rbuf[RBUF];
    uint8_t is_tls;
} tcp_t;
static const mcs_class_def_t tcp_def;
static void tcp_fin(mcs_vm_t* vm, void* d) { (void)vm; conn_close(&((tcp_t*)d)->c); }
#define TC() tcp_t* t = (tcp_t*)mcs_check_userdata(vm, self, &tcp_def); if (!t) return mcs_null()
#define OPEN(op) if (t->c.fd < 0 && !t->c.tls) { mcs_raise(vm, "InvalidOperationException", "%s: the TcpClient is not connected", op); return mcs_null(); }

static void tcp_connect_args(mcs_vm_t* vm, tcp_t* t, int argc, mcs_value_t* argv, bool tls, const char* op) {
    if (argc < 2 || argc > (tls ? 4 : 3)) { mcs_raise(vm, "ArgumentException", tls ? "%s(host, port[, timeoutMs[, caPem]])" : "%s(host, port[, timeoutMs])", op); return; }
    const char* host = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return;
    mcs_int_t port = mcs_to_int(vm, argv[1]); if (mcs_has_exception(vm) || !port_ok(vm, port, false)) return;
    mcs_int_t to = argc > 2 && !mcs_is_null(argv[2]) ? mcs_to_int(vm, argv[2]) : (mcs_int_t)MCS_NET_TIMEOUT;
    if (mcs_has_exception(vm)) return;
    const char* ca = argc > 3 && !mcs_is_null(argv[3]) ? mcs_to_cstr(vm, argv[3]) : NULL;
    if (mcs_has_exception(vm)) return;
    conn_close(&t->c);
    t->rpos = t->rlen = 0;
    if (conn_open(vm, &t->c, host, (int)port, tls, (uint32_t)(to < 0 ? 0 : to), ca, op) == 0) t->is_tls = tls;
}
static void tcp_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    tcp_t* t = (tcp_t*)mcs_userdata(self);
    t->c.fd = -1; t->timeout = MCS_NET_TIMEOUT;
    if (argc) tcp_connect_args(vm, t, argc, argv, false, "TcpClient");
}
NATIVE(tcp_connect_m) { TC(); tcp_connect_args(vm, t, argc, argv, false, "TcpClient.Connect"); return mcs_null(); }
NATIVE(tcp_connect_tls) { TC(); tcp_connect_args(vm, t, argc, argv, true, "TcpClient.ConnectTls"); return mcs_null(); }
static mcs_value_t tcp_write_bytes(mcs_vm_t* vm, tcp_t* t, const uint8_t* p, size_t n, const char* op) {
    int r = conn_write(vm, &t->c, p, n, t->timeout);
    if (r == -EINTR) return mcs_null();
    if (r < 0) return sock_fail(vm, op, -r);
    return mcs_int(r);
}
NATIVE(tcp_write) {
    TC(); OPEN("TcpClient.Write");
    const uint8_t* p; size_t n; uint8_t* heap;
    if (!mcs__bytes_arg(vm, argv[0], &p, &n, &heap)) return mcs_null();
    mcs_value_t r = tcp_write_bytes(vm, t, p, n, "TcpClient.Write");
    mcs__bytes_free(vm, heap, n);
    return r;
}
NATIVE(tcp_writeline) {
    TC(); OPEN("TcpClient.WriteLine");
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "TcpClient.WriteLine([text])"); return mcs_null(); }
    if (argc == 1) {
        const char* s = mcs_to_cstr(vm, argv[0]); CHECK();
        mcs_value_t r = tcp_write_bytes(vm, t, (const uint8_t*)s, strlen(s), "TcpClient.WriteLine");
        if (mcs_has_exception(vm)) return r;
    }
    return tcp_write_bytes(vm, t, (const uint8_t*)"\r\n", 2, "TcpClient.WriteLine");
}
/* refill the line buffer: >0 bytes, 0 closed, <0 error (exception raised) */
static int tcp_fill(mcs_vm_t* vm, tcp_t* t, const char* op) {
    if (t->rpos < t->rlen) return t->rlen - t->rpos;
    t->rpos = t->rlen = 0;
    int r = conn_read(vm, &t->c, t->rbuf, RBUF, t->timeout);
    if (r == -EINTR) return -1;
    if (r < 0) { sock_fail(vm, op, -r); return -1; }
    t->rlen = (uint16_t)r;
    return r;
}
NATIVE(tcp_read) {      /* ([max]) -> byte[]; empty = closed by the peer */
    TC(); OPEN("TcpClient.Read");
    OPT_INT(0, max, MCS_NET_MAX_READ);
    if (max < 1 || max > MCS_NET_MAX_READ) { mcs_raise(vm, "ArgumentOutOfRangeException", "TcpClient.Read: 1..%d bytes", MCS_NET_MAX_READ); return mcs_null(); }
    if (t->rpos < t->rlen) {
        size_t n = (size_t)(t->rlen - t->rpos); if (n > (size_t)max) n = (size_t)max;
        mcs_value_t a = mcs__bytes_new(vm, t->rbuf + t->rpos, n);
        t->rpos = (uint16_t)(t->rpos + n);
        return a;
    }
    uint8_t* b = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, (size_t)max);
    if (!b) return mcs_null();
    int r = conn_read(vm, &t->c, b, (size_t)max, t->timeout);
    mcs_value_t a = r >= 0 ? mcs__bytes_new(vm, b, (size_t)r) : mcs_null();
    mcs_mem_realloc(vm, b, (size_t)max, 0);
    if (r == -EINTR) return mcs_null();
    if (r < 0) return sock_fail(vm, "TcpClient.Read", -r);
    return a;
}
NATIVE(tcp_readstring) {   /* ([max]) -> string; "" = closed */
    TC(); OPEN("TcpClient.ReadString");
    OPT_INT(0, max, MCS_NET_MAX_READ);
    if (max < 1 || max > MCS_NET_MAX_READ) { mcs_raise(vm, "ArgumentOutOfRangeException", "TcpClient.ReadString: 1..%d bytes", MCS_NET_MAX_READ); return mcs_null(); }
    int r = tcp_fill(vm, t, "TcpClient.ReadString");
    if (r < 0) return mcs_null();
    size_t n = (size_t)(t->rlen - t->rpos); if (n > (size_t)max) n = (size_t)max;
    mcs_value_t s = mcs_string_n(vm, (const char*)t->rbuf + t->rpos, n);
    t->rpos = (uint16_t)(t->rpos + n);
    return s;
}
NATIVE(tcp_readbyte) {
    TC(); OPEN("TcpClient.ReadByte");
    int r = tcp_fill(vm, t, "TcpClient.ReadByte");
    if (r < 0) return mcs_null();
    if (r == 0) return mcs_int(-1);
    return mcs_int(t->rbuf[t->rpos++]);
}
/* read until '\n' (stripped with a trailing '\r'); null when the peer closed first */
static mcs_value_t read_line(mcs_vm_t* vm, tcp_t* t, size_t limit, const char* op) {
    size_t cap = 128, len = 0;
    char* b = (char*)mcs_mem_realloc(vm, NULL, 0, cap);
    if (!b) return mcs_null();
    mcs_value_t out = mcs_null();
    for (;;) {
        int r = tcp_fill(vm, t, op);
        if (r < 0) goto done;
        if (r == 0) { if (len) out = mcs_string_n(vm, b, len); goto done; }
        while (t->rpos < t->rlen) {
            char ch = (char)t->rbuf[t->rpos++];
            if (ch == '\n') { if (len && b[len - 1] == '\r') len--; out = mcs_string_n(vm, b, len); goto done; }
            if (len + 1 >= cap) {
                if (cap >= limit) { mcs_raise(vm, "IOException", "%s: line longer than %d bytes", op, (int)limit); goto done; }
                char* nb = (char*)mcs_mem_realloc(vm, b, cap, cap * 2);
                if (!nb) goto done;
                b = nb; cap *= 2;
            }
            b[len++] = ch;
        }
    }
done:
    mcs_mem_realloc(vm, b, cap, 0);
    return out;
}
NATIVE(tcp_readline) { TC(); OPEN("TcpClient.ReadLine"); return read_line(vm, t, MCS_HTTP_MAX_BODY, "TcpClient.ReadLine"); }
NATIVE(tcp_readtoend) {
    TC(); OPEN("TcpClient.ReadToEnd");
    size_t cap = 256, len = 0;
    char* b = (char*)mcs_mem_realloc(vm, NULL, 0, cap);
    if (!b) return mcs_null();
    mcs_value_t out = mcs_null();
    for (;;) {
        int r = tcp_fill(vm, t, "TcpClient.ReadToEnd");
        if (r < 0) break;
        if (r == 0) { out = mcs_string_n(vm, b, len); break; }
        size_t n = (size_t)(t->rlen - t->rpos);
        if (len + n > cap) {
            size_t nc = cap; while (nc < len + n) nc *= 2;
            if (nc > MCS_HTTP_MAX_BODY) { mcs_raise(vm, "IOException", "TcpClient.ReadToEnd: more than MCS_HTTP_MAX_BODY (%d) bytes", MCS_HTTP_MAX_BODY); break; }
            char* nb = (char*)mcs_mem_realloc(vm, b, cap, nc);
            if (!nb) break;
            b = nb; cap = nc;
        }
        memcpy(b + len, t->rbuf + t->rpos, n); len += n; t->rpos = t->rlen;
    }
    mcs_mem_realloc(vm, b, cap, 0);
    return out;
}
NATIVE(tcp_available) {
    TC();
    int n = t->rlen - t->rpos;
    if (t->c.fd >= 0 && !t->c.tls) {
        int k = 0;
        if (ioctl(t->c.fd, FIONREAD, &k) == 0 && k > 0) n += k;
    }
    return mcs_int(n);
}
NATIVE(tcp_connected) {
    TC();
    if (t->c.tls) return mcs_bool(true);
    if (t->c.fd < 0) return mcs_bool(false);
    if (t->rpos < t->rlen) return mcs_bool(true);
    fd_set s; FD_ZERO(&s); FD_SET(t->c.fd, &s);
    struct timeval tv = { 0, 0 };
    if (select(t->c.fd + 1, &s, NULL, NULL, &tv) > 0) {       /* readable: data or EOF */
        ssize_t r = recv(t->c.fd, t->rbuf, RBUF, 0);
        if (r > 0) { t->rpos = 0; t->rlen = (uint16_t)r; return mcs_bool(true); }
        if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return mcs_bool(false);
    }
    return mcs_bool(true);
}
NATIVE(tcp_close) { TC(); conn_close(&t->c); t->rpos = t->rlen = 0; return mcs_null(); }
NATIVE(tcp_flush) { TC(); return mcs_null(); }
NATIVE(tcp_gettimeout) { TC(); return mcs_int((mcs_int_t)t->timeout); }
NATIVE(tcp_settimeout) {
    TC(); INT_ARG(0, v);
    if (v < 0) { mcs_raise(vm, "ArgumentOutOfRangeException", "Timeout: ms >= 0 (0 = wait forever)"); return mcs_null(); }
    t->timeout = (uint32_t)v;
    return mcs_null();
}
static int sockopt_get(int fd, int level, int name) { int v = 0; socklen_t l = sizeof v; if (fd < 0 || getsockopt(fd, level, name, &v, &l) < 0) return 0; return v; }
NATIVE(tcp_getnodelay) { TC(); return mcs_bool(sockopt_get(t->c.fd, IPPROTO_TCP, TCP_NODELAY) != 0); }
NATIVE(tcp_setnodelay) { TC(); OPEN("TcpClient.NoDelay"); int v = mcs_truthy(argv[0]); setsockopt(t->c.fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof v); return mcs_null(); }
NATIVE(tcp_getka) { TC(); return mcs_bool(sockopt_get(t->c.fd, SOL_SOCKET, SO_KEEPALIVE) != 0); }
NATIVE(tcp_setka) { TC(); OPEN("TcpClient.KeepAlive"); int v = mcs_truthy(argv[0]); setsockopt(t->c.fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof v); return mcs_null(); }
static mcs_value_t endpoint(mcs_vm_t* vm, int fd, bool peer, bool port) {
    struct sockaddr_in a; socklen_t l = sizeof a; memset(&a, 0, sizeof a);
    if (fd >= 0) { if (peer) getpeername(fd, (struct sockaddr*)&a, &l); else getsockname(fd, (struct sockaddr*)&a, &l); }
    if (port) return mcs_int(ntohs(a.sin_port));
    char b[16]; mcs__ip_fmt((const uint8_t*)&a.sin_addr, b);
    return mcs_string(vm, b);
}
NATIVE(tcp_raddr) { TC(); return endpoint(vm, t->c.fd, true, false); }
NATIVE(tcp_rport) { TC(); return endpoint(vm, t->c.fd, true, true); }
NATIVE(tcp_laddr) { TC(); return endpoint(vm, t->c.fd, false, false); }
NATIVE(tcp_lport) { TC(); return endpoint(vm, t->c.fd, false, true); }
NATIVE(tcp_istls) { TC(); return mcs_bool(t->c.tls != NULL); }
static const mcs_reg_t tcp_members[] = {
    MCS_FN("Connect", tcp_connect_m, -1), MCS_FN("ConnectTls", tcp_connect_tls, -1),
    MCS_FN("Write", tcp_write, 1), MCS_FN("WriteLine", tcp_writeline, -1), MCS_FN("Flush", tcp_flush, 0),
    MCS_FN("Read", tcp_read, -1), MCS_FN("ReadString", tcp_readstring, -1), MCS_FN("ReadByte", tcp_readbyte, 0),
    MCS_FN("ReadLine", tcp_readline, 0), MCS_FN("ReadToEnd", tcp_readtoend, 0),
    MCS_GET("Available", tcp_available), MCS_GET("Connected", tcp_connected),
    MCS_FN("Close", tcp_close, 0), MCS_FN("Dispose", tcp_close, 0),
    MCS_GET("Timeout", tcp_gettimeout), MCS_SET("Timeout", tcp_settimeout),
    MCS_GET("NoDelay", tcp_getnodelay), MCS_SET("NoDelay", tcp_setnodelay),
    MCS_GET("KeepAlive", tcp_getka), MCS_SET("KeepAlive", tcp_setka),
    MCS_GET("RemoteAddress", tcp_raddr), MCS_GET("RemotePort", tcp_rport),
    MCS_GET("LocalAddress", tcp_laddr), MCS_GET("LocalPort", tcp_lport), MCS_GET("IsTls", tcp_istls),
    MCS_REG_END };
static const mcs_class_def_t tcp_def = { "TcpClient", sizeof(tcp_t), tcp_ctor, tcp_fin, tcp_members, NULL };

/* ============================================================ TcpListener */
typedef struct { int fd; uint8_t ip[4]; uint16_t port; } lst_t;
static const mcs_class_def_t lst_def;
static void lst_fin(mcs_vm_t* vm, void* d) { (void)vm; lst_t* l = (lst_t*)d; if (l->fd >= 0) close(l->fd); l->fd = -1; }
#define LS() lst_t* l = (lst_t*)mcs_check_userdata(vm, self, &lst_def); if (!l) return mcs_null()
static void lst_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    lst_t* l = (lst_t*)mcs_userdata(self);
    l->fd = -1;
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "new TcpListener(port) or new TcpListener(address, port)"); return; }
    mcs_int_t port;
    if (argc == 2) {
        const char* a = mcs_to_cstr(vm, argv[0]); if (mcs_has_exception(vm)) return;
        if (!mcs__ip_parse(a, l->ip)) { mcs_raise(vm, "ArgumentException", "TcpListener: '%s' is not an IPv4 address", a); return; }
        port = mcs_to_int(vm, argv[1]);
    } else port = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm) || !port_ok(vm, port, true)) return;
    l->port = (uint16_t)port;
}
NATIVE(lst_start) {
    LS();
    OPT_INT(0, backlog, 4);
    if (l->fd >= 0) return mcs_null();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return sock_fail(vm, "TcpListener.Start", errno);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; mk_addr(&a, l->ip, l->port);
    if (bind(fd, (struct sockaddr*)&a, sizeof a) < 0 || listen(fd, (int)(backlog < 1 ? 1 : backlog)) < 0) {
        int e = errno; close(fd);
        return sock_fail(vm, "TcpListener.Start", e);
    }
    set_nonblock(fd);
    l->fd = fd;
    socklen_t al = sizeof a;
    if (getsockname(fd, (struct sockaddr*)&a, &al) == 0) l->port = ntohs(a.sin_port);   /* port 0: the one picked */
    return mcs_null();
}
NATIVE(lst_stop) { LS(); if (l->fd >= 0) close(l->fd); l->fd = -1; return mcs_null(); }
NATIVE(lst_pending) {
    LS(); if (l->fd < 0) return mcs_bool(false);
    fd_set s; FD_ZERO(&s); FD_SET(l->fd, &s);
    struct timeval tv = { 0, 0 };
    return mcs_bool(select(l->fd + 1, &s, NULL, NULL, &tv) > 0);
}
NATIVE(lst_accept) {    /* ([timeoutMs]) -> TcpClient, null on timeout (default: wait forever) */
    LS();
    if (l->fd < 0) { mcs_raise(vm, "InvalidOperationException", "TcpListener.AcceptTcpClient: call Start() first"); return mcs_null(); }
    OPT_INT(0, timeout, 0);
    if (timeout < 0) timeout = 0;
    for (;;) {
        int w = wait_fd(vm, l->fd, false, (uint32_t)timeout);
        if (w < 0) return mcs_null();
        if (w == 0) return mcs_null();
        int fd = accept(l->fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED) { if (timeout) return mcs_null(); continue; }
            return sock_fail(vm, "TcpListener.AcceptTcpClient", errno);
        }
        set_nonblock(fd); no_sigpipe(fd);
        mcs_value_t o;
        if (mcs_new_object(vm, "TcpClient", 0, NULL, &o) != MCS_OK) { close(fd); return mcs_null(); }
        tcp_t* t = (tcp_t*)mcs_userdata(o);
        t->c.fd = fd; t->c.net = mcs__net_state(vm) ? mcs__net_state(vm)->net : NULL;
        return o;
    }
}
NATIVE(lst_lport) { LS(); if (l->fd < 0) return mcs_int(l->port); return endpoint(vm, l->fd, false, true); }
NATIVE(lst_active) { LS(); return mcs_bool(l->fd >= 0); }
static const mcs_reg_t lst_members[] = {
    MCS_FN("Start", lst_start, -1), MCS_FN("Stop", lst_stop, 0), MCS_FN("Pending", lst_pending, 0),
    MCS_FN("AcceptTcpClient", lst_accept, -1), MCS_FN("Accept", lst_accept, -1),
    MCS_GET("LocalPort", lst_lport), MCS_GET("Active", lst_active), MCS_FN("Dispose", lst_stop, 0), MCS_REG_END };
static const mcs_class_def_t lst_def = { "TcpListener", sizeof(lst_t), lst_ctor, lst_fin, lst_members, NULL };

/* ============================================================ UdpClient */
typedef struct { int fd; uint32_t timeout; struct sockaddr_in dst, from; uint8_t has_dst; } udp_t;
static const mcs_class_def_t udp_def;
static void udp_fin(mcs_vm_t* vm, void* d) { (void)vm; udp_t* u = (udp_t*)d; if (u->fd >= 0) close(u->fd); u->fd = -1; }
#define UD() udp_t* u = (udp_t*)mcs_check_userdata(vm, self, &udp_def); if (!u) return mcs_null(); \
    if (u->fd < 0) { mcs_raise(vm, "ObjectDisposedException", "the UdpClient is closed"); return mcs_null(); }
static void udp_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    udp_t* u = (udp_t*)mcs_userdata(self);
    u->fd = -1;
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "new UdpClient([localPort])"); return; }
    mcs_int_t port = argc ? mcs_to_int(vm, argv[0]) : 0;
    if (mcs_has_exception(vm) || !port_ok(vm, port, true)) return;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { sock_fail(vm, "UdpClient", errno); return; }
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    static const uint8_t any[4];
    struct sockaddr_in a; mk_addr(&a, any, (int)port);
    if (bind(fd, (struct sockaddr*)&a, sizeof a) < 0) { int e = errno; close(fd); sock_fail(vm, "UdpClient", e); return; }
    set_nonblock(fd);
    u->fd = fd; u->timeout = 0;
}
static bool udp_target(mcs_vm_t* vm, mcs_value_t h, mcs_value_t p, struct sockaddr_in* a, const char* op) {
    const char* host = mcs_to_cstr(vm, h); if (mcs_has_exception(vm)) return false;
    mcs_int_t port = mcs_to_int(vm, p); if (mcs_has_exception(vm) || !port_ok(vm, port, false)) return false;
    uint8_t ip[4];
    if (!strcmp(host, "255.255.255.255") || !strcmp(host, "broadcast")) memset(ip, 255, 4);
    else if (!resolve_arg(vm, host, ip, op)) return false;
    mk_addr(a, ip, (int)port);
    return true;
}
NATIVE(udp_connect) { UD(); if (!udp_target(vm, argv[0], argv[1], &u->dst, "UdpClient.Connect")) return mcs_null(); u->has_dst = 1; return mcs_null(); }
NATIVE(udp_send) {      /* (data) or (data, host, port) -> bytes sent */
    UD();
    if (argc != 1 && argc != 3) { mcs_raise(vm, "ArgumentException", "UdpClient.Send(data) or Send(data, host, port)"); return mcs_null(); }
    struct sockaddr_in a = u->dst;
    if (argc == 3) { if (!udp_target(vm, argv[1], argv[2], &a, "UdpClient.Send")) return mcs_null(); }
    else if (!u->has_dst) { mcs_raise(vm, "InvalidOperationException", "UdpClient.Send(data): call Connect(host, port) first"); return mcs_null(); }
    const uint8_t* p; size_t n; uint8_t* heap;
    if (!mcs__bytes_arg(vm, argv[0], &p, &n, &heap)) return mcs_null();
    ssize_t r = sendto(u->fd, p, n, 0, (struct sockaddr*)&a, sizeof a);
    int e = errno;
    mcs__bytes_free(vm, heap, n);
    if (r < 0) return sock_fail(vm, "UdpClient.Send", e);
    return mcs_int((mcs_int_t)r);
}
static int udp_recv(mcs_vm_t* vm, udp_t* u, int argc, mcs_value_t* argv, uint8_t* buf, size_t cap) {
    mcs_int_t to = argc > 0 && !mcs_is_null(argv[0]) ? mcs_to_int(vm, argv[0]) : (mcs_int_t)u->timeout;
    if (mcs_has_exception(vm)) return -2;
    int w = wait_fd(vm, u->fd, false, (uint32_t)(to < 0 ? 0 : to));
    if (w <= 0) return w < 0 ? -2 : -1;
    socklen_t l = sizeof u->from;
    ssize_t r = recvfrom(u->fd, buf, cap, 0, (struct sockaddr*)&u->from, &l);
    if (r < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) return -1; sock_fail(vm, "UdpClient.Receive", errno); return -2; }
    return (int)r;
}
NATIVE(udp_receive) {   /* ([timeoutMs]) -> byte[] or null on timeout */
    UD();
    uint8_t* b = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, MCS_NET_MAX_READ);
    if (!b) return mcs_null();
    int r = udp_recv(vm, u, argc, argv, b, MCS_NET_MAX_READ);
    mcs_value_t a = r >= 0 ? mcs__bytes_new(vm, b, (size_t)r) : mcs_null();
    mcs_mem_realloc(vm, b, MCS_NET_MAX_READ, 0);
    return a;
}
NATIVE(udp_receive_str) {
    UD();
    uint8_t* b = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, MCS_NET_MAX_READ);
    if (!b) return mcs_null();
    int r = udp_recv(vm, u, argc, argv, b, MCS_NET_MAX_READ);
    mcs_value_t a = r >= 0 ? mcs_string_n(vm, (const char*)b, (size_t)r) : mcs_null();
    mcs_mem_realloc(vm, b, MCS_NET_MAX_READ, 0);
    return a;
}
NATIVE(udp_raddr) { UD(); char b[16]; mcs__ip_fmt((const uint8_t*)&u->from.sin_addr, b); return mcs_string(vm, b); }
NATIVE(udp_rport) { UD(); return mcs_int(ntohs(u->from.sin_port)); }
NATIVE(udp_lport) { UD(); return endpoint(vm, u->fd, false, true); }
NATIVE(udp_available) {
    UD(); int k = 0;
#if defined(ESP_PLATFORM) || MCS_NET_LWIP
    if (ioctl(u->fd, FIONREAD, &k) < 0) k = 0;
#else
    if (ioctl(u->fd, FIONREAD, &k) < 0) k = 0;
#endif
    return mcs_int(k);
}
NATIVE(udp_setbcast) { UD(); int v = mcs_truthy(argv[0]); setsockopt(u->fd, SOL_SOCKET, SO_BROADCAST, &v, sizeof v); return mcs_null(); }
NATIVE(udp_getbcast) { UD(); return mcs_bool(sockopt_get(u->fd, SOL_SOCKET, SO_BROADCAST) != 0); }
static mcs_value_t udp_group(mcs_vm_t* vm, mcs_value_t self, mcs_value_t g, int opt, const char* op) {
    UD();
    const char* s = mcs_to_cstr(vm, g); CHECK();
    uint8_t ip[4];
    if (!mcs__ip_parse(s, ip) || ip[0] < 224 || ip[0] > 239) { mcs_raise(vm, "ArgumentException", "%s: '%s' is not an IPv4 multicast address (224.0.0.0 .. 239.255.255.255)", op, s); return mcs_null(); }
    struct ip_mreq m; memset(&m, 0, sizeof m);
    memcpy(&m.imr_multiaddr, ip, 4); m.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(u->fd, IPPROTO_IP, opt, &m, sizeof m) < 0) return sock_fail(vm, op, errno);
    return mcs_null();
}
NATIVE(udp_join) { return udp_group(vm, self, argv[0], IP_ADD_MEMBERSHIP, "UdpClient.JoinMulticastGroup"); }
NATIVE(udp_drop) { return udp_group(vm, self, argv[0], IP_DROP_MEMBERSHIP, "UdpClient.DropMulticastGroup"); }
NATIVE(udp_gettimeout) { UD(); return mcs_int((mcs_int_t)u->timeout); }
NATIVE(udp_settimeout) { UD(); INT_ARG(0, v); if (v < 0) v = 0; u->timeout = (uint32_t)v; return mcs_null(); }
NATIVE(udp_close) {
    udp_t* u = (udp_t*)mcs_check_userdata(vm, self, &udp_def); if (!u) return mcs_null();
    if (u->fd >= 0) close(u->fd);
    u->fd = -1;
    return mcs_null();
}
static const mcs_reg_t udp_members[] = {
    MCS_FN("Connect", udp_connect, 2), MCS_FN("Send", udp_send, -1),
    MCS_FN("Receive", udp_receive, -1), MCS_FN("ReceiveString", udp_receive_str, -1),
    MCS_GET("RemoteAddress", udp_raddr), MCS_GET("RemotePort", udp_rport), MCS_GET("LocalPort", udp_lport),
    MCS_GET("Available", udp_available),
    MCS_GET("EnableBroadcast", udp_getbcast), MCS_SET("EnableBroadcast", udp_setbcast),
    MCS_FN("JoinMulticastGroup", udp_join, 1), MCS_FN("DropMulticastGroup", udp_drop, 1),
    MCS_GET("Timeout", udp_gettimeout), MCS_SET("Timeout", udp_settimeout),
    MCS_FN("Close", udp_close, 0), MCS_FN("Dispose", udp_close, 0), MCS_REG_END };
static const mcs_class_def_t udp_def = { "UdpClient", sizeof(udp_t), udp_ctor, udp_fin, udp_members, NULL };

/* ============================================================ Dns */
NATIVE(dns_resolve) {   /* (host) -> "a.b.c.d" or null */
    STR_ARG(0, host);
    uint8_t ip[4];
    if (mcs__resolve4(vm, host, ip, MCS_NET_TIMEOUT) < 0) return mcs_null();
    char b[16]; mcs__ip_fmt(ip, b);
    return mcs_string(vm, b);
}
NATIVE(dns_all) {       /* (host) -> string[] (every IPv4 address) */
    STR_ARG(0, host);
    uint8_t ips[8][4]; int n = 0;
    if (mcs__ip_parse(host, ips[0])) n = 1;
    else {
        size_t hl = strlen(host);
        if (hl > 6 && !strcmp(host + hl - 6, ".local") && mcs__resolve4(vm, host, ips[0], MCS_NET_TIMEOUT) == 0) n = 1;
        else {
            struct addrinfo hints, *res = NULL, *p;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(host, NULL, &hints, &res) == 0) {
                for (p = res; p && n < 8; p = p->ai_next) {
                    const uint8_t* a = (const uint8_t*)&((struct sockaddr_in*)p->ai_addr)->sin_addr;
                    bool dup = false;
                    for (int i = 0; i < n; i++) if (!memcmp(ips[i], a, 4)) dup = true;
                    if (!dup) memcpy(ips[n++], a, 4);
                }
                freeaddrinfo(res);
            }
        }
    }
    if (n == 0) { mcs_raise(vm, "SocketException", "Dns.GetHostAddresses: cannot resolve host '%s'", host); return mcs_null(); }
    mcs_value_t arr = mcs_new_array(vm, (uint32_t)n);
    mcs_push_root(vm, arr);
    for (int i = 0; i < n; i++) { char b[16]; mcs__ip_fmt(ips[i], b); mcs_set_index(arr, (uint32_t)i, mcs_string(vm, b)); }
    mcs_pop_root(vm, 1);
    return arr;
}
NATIVE(dns_hostname) {
    char b[64] = "microcs";
    mcs__net_state_t* s = mcs__net_state(vm);
    if (s && s->net && s->net->str_get && s->net->str_get(s->net->ctx, MCS_WIFI_STR_HOSTNAME, b, sizeof b) >= 0 && b[0]) return mcs_string(vm, b);
#if !defined(ESP_PLATFORM) && !MCS_NET_LWIP
    if (gethostname(b, sizeof b) != 0) snprintf(b, sizeof b, "microcs");
    b[sizeof b - 1] = 0;
#endif
    return mcs_string(vm, b);
}
static const mcs_reg_t dns_fns[] = {
    MCS_FN("Resolve", dns_resolve, 1), MCS_FN("GetHostAddresses", dns_all, 1), MCS_GET("HostName", dns_hostname), MCS_REG_END };

/* ============================================================ Http */
typedef struct {
    int status;
    char reason[40];
    char url[900];
    char* hdr; size_t hdr_len;       /* raw header lines, "\r\n" separated */
    char* body; size_t body_len;     /* mcs_mem_realloc'd (body_cap bytes) */
    size_t hdr_cap, body_cap;
} resp_t;
static const mcs_class_def_t resp_def;
static void plain_ctor_resp(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argv;
    if (argc) mcs_raise(vm, "InvalidOperationException", "HttpResponse objects come from Http.Get / Post / Request");
}
static void resp_fin(mcs_vm_t* vm, void* d) {
    resp_t* r = (resp_t*)d;
    if (r->hdr) mcs_mem_realloc(vm, r->hdr, r->hdr_cap, 0);
    if (r->body) mcs_mem_realloc(vm, r->body, r->body_cap, 0);
    r->hdr = r->body = NULL;
}
#define RS() resp_t* r = (resp_t*)mcs_check_userdata(vm, self, &resp_def); if (!r) return mcs_null()
/* header value (case-insensitive name) -> pointer + length into r->hdr */
static const char* hdr_find(const char* h, size_t hl, const char* name, size_t* vl) {
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
NATIVE(rs_status) { RS(); return mcs_int(r->status); }
NATIVE(rs_reason) { RS(); return mcs_string(vm, r->reason); }
NATIVE(rs_ok) { RS(); return mcs_bool(r->status >= 200 && r->status < 300); }
NATIVE(rs_body) { RS(); return mcs_string_n(vm, r->body ? r->body : "", r->body_len); }
NATIVE(rs_bytes) { RS(); return mcs__bytes_new(vm, (const uint8_t*)r->body, r->body ? r->body_len : 0); }
NATIVE(rs_len) { RS(); return mcs_int((mcs_int_t)r->body_len); }
NATIVE(rs_url) { RS(); return mcs_string(vm, r->url); }
NATIVE(rs_headers) { RS(); return mcs_string_n(vm, r->hdr ? r->hdr : "", r->hdr_len); }
NATIVE(rs_header) {
    RS(); STR_ARG(0, name);
    size_t vl; const char* v = r->hdr ? hdr_find(r->hdr, r->hdr_len, name, &vl) : NULL;
    return v ? mcs_string_n(vm, v, vl) : mcs_null();
}
NATIVE(rs_ctype) {
    RS(); size_t vl; const char* v = r->hdr ? hdr_find(r->hdr, r->hdr_len, "Content-Type", &vl) : NULL;
    return v ? mcs_string_n(vm, v, vl) : mcs_string(vm, "");
}
NATIVE(rs_tostring) { RS(); char b[64]; snprintf(b, sizeof b, "%d %s (%d bytes)", r->status, r->reason, (int)r->body_len); return mcs_string(vm, b); }
NATIVE(rs_ensure) {
    RS();
    if (r->status < 200 || r->status >= 300) { mcs_raise(vm, "HttpRequestException", "HTTP %d %s: %s", r->status, r->reason, r->url); return mcs_null(); }
    return self;
}
static const mcs_reg_t resp_members[] = {
    MCS_GET("StatusCode", rs_status), MCS_GET("Reason", rs_reason), MCS_GET("IsSuccess", rs_ok),
    MCS_GET("Body", rs_body), MCS_GET("Bytes", rs_bytes), MCS_GET("ContentLength", rs_len),
    MCS_GET("ContentType", rs_ctype), MCS_GET("Url", rs_url), MCS_GET("Headers", rs_headers),
    MCS_FN("Header", rs_header, 1), MCS_FN("EnsureSuccess", rs_ensure, 0), MCS_FN("ToString", rs_tostring, 0), MCS_REG_END };
static const mcs_class_def_t resp_def = { "HttpResponse", sizeof(resp_t), plain_ctor_resp, resp_fin, resp_members, NULL };

/* ---- URL */
typedef struct { bool tls; int port; char host[128]; char path[768]; } url_t;
static bool url_parse(mcs_vm_t* vm, const char* u, url_t* o, const char* op) {
    memset(o, 0, sizeof *o);
    const char* p = u;
    if (!strncmp(p, "http://", 7)) p += 7;
    else if (!strncmp(p, "https://", 8)) { p += 8; o->tls = true; }
    else if (strstr(p, "://")) { mcs_raise(vm, "ArgumentException", "%s: only http:// and https:// URLs ('%s')", op, u); return false; }
    o->port = o->tls ? 443 : 80;
    const char* h = p;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') p++;
    size_t hl = (size_t)(p - h);
    if (!hl || hl >= sizeof o->host) { mcs_raise(vm, "ArgumentException", "%s: bad host in URL '%s'", op, u); return false; }
    memcpy(o->host, h, hl);
    if (*p == ':') {
        long port = strtol(p + 1, (char**)&p, 10);
        if (port < 1 || port > 65535) { mcs_raise(vm, "ArgumentException", "%s: bad port in URL '%s'", op, u); return false; }
        o->port = (int)port;
    }
    const char* path = *p ? p : "/";
    if (*path == '?') { o->path[0] = '/'; snprintf(o->path + 1, sizeof o->path - 1, "%s", path); }
    else if (strlen(path) >= sizeof o->path) { mcs_raise(vm, "ArgumentException", "%s: URL longer than %d characters", op, (int)sizeof o->path); return false; }
    else snprintf(o->path, sizeof o->path, "%s", path);
    char* hash = strchr(o->path, '#');
    if (hash) *hash = 0;
    return true;
}

/* ---- buffered reader over a connection */
typedef struct { mcs_vm_t* vm; conn_t* c; uint32_t timeout; size_t pos, len; uint8_t buf[RBUF]; } rd_t;
/* >0 bytes available, 0 closed, -1 error (exception raised) */
static int rd_fill(rd_t* r) {
    if (r->pos < r->len) return (int)(r->len - r->pos);
    r->pos = r->len = 0;
    int n = conn_read(r->vm, r->c, r->buf, RBUF, r->timeout);
    if (n == -EINTR) return -1;
    if (n < 0) { sock_fail(r->vm, "Http", -n); return -1; }
    r->len = (size_t)n;
    return n;
}
/* line without CRLF into out (cap); -1 error / closed early */
static int rd_line(rd_t* r, char* out, size_t cap, const char* phase) {
    size_t n = 0;
    for (;;) {
        int f = rd_fill(r);
        if (f < 0) return -1;
        if (f == 0) {
            if (!mcs_has_exception(r->vm)) {
                if (!n && !strcmp(phase, "status line"))
                    mcs_raise(r->vm, "IOException", "Http: server closed before sending an HTTP response (try HTTPS or another endpoint)");
                else mcs_raise(r->vm, "IOException", "Http: connection closed while reading %s", phase);
            }
            return -1;
        }
        char ch = (char)r->buf[r->pos++];
        if (ch == '\n') { if (n && out[n - 1] == '\r') n--; out[n] = 0; return (int)n; }
        if (n + 1 >= cap) { mcs_raise(r->vm, "IOException", "Http: %s too long", phase); return -1; }
        out[n++] = ch;
    }
}

/* ---- body sinks */
typedef struct {
    mcs_vm_t* vm;
    resp_t* mem;                 /* memory body, or */
#if MCS_ENABLE_FS
    mcs_vfs_file_t* file;        /* a file (Http.Download) */
#endif
    size_t total;
} sink_t;
static bool sink_put(sink_t* k, const uint8_t* p, size_t n) {
    if (!n) return true;
#if MCS_ENABLE_FS
    if (k->file) {
        int w = mcs_vfs_write(k->file, p, n);
        if (w < 0 || (size_t)w != n) { mcs_raise(k->vm, "IOException", "Http.Download: write failed: %s", w < 0 ? mcs_vfs_strerror(w) : "disk full"); return false; }
        k->total += n;
        return true;
    }
#endif
    resp_t* r = k->mem;
    if (r->body_len + n > MCS_HTTP_MAX_BODY) {
        mcs_raise(k->vm, "IOException", "Http: response body larger than MCS_HTTP_MAX_BODY (%d bytes) - use Http.Download(url, path) for big files", MCS_HTTP_MAX_BODY);
        return false;
    }
    if (r->body_len + n > r->body_cap) {
        size_t nc = r->body_cap ? r->body_cap : 256;
        while (nc < r->body_len + n) nc *= 2;
        if (nc > MCS_HTTP_MAX_BODY) nc = MCS_HTTP_MAX_BODY;
        char* nb = (char*)mcs_mem_realloc(k->vm, r->body, r->body_cap, nc);
        if (!nb) return false;
        r->body = nb; r->body_cap = nc;
    }
    memcpy(r->body + r->body_len, p, n);
    r->body_len += n; k->total += n;
    return true;
}
/* copy n bytes (or until close when n == SIZE_MAX) from the reader to the sink */
static bool copy_body(rd_t* r, sink_t* k, size_t n) {
    while (n) {
        int f = rd_fill(r);
        if (f < 0) return false;
        if (f == 0) {
            if (n == (size_t)-1) return true;
            mcs_raise(r->vm, "IOException", "Http: connection closed before the whole body arrived");
            return false;
        }
        size_t take = (size_t)f; if (take > n) take = n;
        if (!sink_put(k, r->buf + r->pos, take)) return false;
        r->pos += take;
        if (n != (size_t)-1) n -= take;
    }
    return true;
}
static bool chunked_body(rd_t* r, sink_t* k) {
    char line[256];
    for (;;) {
        if (rd_line(r, line, sizeof line, "chunk size") < 0) return false;
        /* Reject invalid / overflowing sizes, rather than treating them as a zero chunk. */
        const char* p = line;
        size_t n = 0; int digits = 0;
        for (;;) {
            int d = *p >= '0' && *p <= '9' ? *p - '0' :
                    *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
                    *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
            if (d < 0) break;
            if (n > ((size_t)-1 - (size_t)d) / 16) break;
            n = n * 16 + (size_t)d; digits++; p++;
        }
        while (*p == ' ' || *p == '\t') p++;
        if (!digits || (*p && *p != ';') || n == (size_t)-1) {
            mcs_raise(r->vm, "IOException", "Http: invalid chunk size"); return false;
        }
        if (n == 0) {                                   /* trailers until the empty line */
            for (int i = 0; i < 32; i++) {
                int l = rd_line(r, line, sizeof line, "chunk trailers");
                if (l < 0) return false;
                if (l == 0) return true;
            }
            mcs_raise(r->vm, "IOException", "Http: too many chunk trailers"); return false;
        }
        if (!copy_body(r, k, n)) return false;
        if (rd_line(r, line, sizeof line, "chunk terminator") < 0) return false;
        if (line[0]) { mcs_raise(r->vm, "IOException", "Http: invalid chunk terminator"); return false; }
    }
}

/* ---- request */
static bool put_str(mcs_vm_t* vm, char** b, size_t* len, size_t* cap, const char* s, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t nc = *cap; while (nc < *len + n + 1) nc *= 2;
        char* nb = (char*)mcs_mem_realloc(vm, *b, *cap, nc);
        if (!nb) return false;
        *b = nb; *cap = nc;
    }
    memcpy(*b + *len, s, n); *len += n; (*b)[*len] = 0;
    return true;
}
#define PUTS(s) put_str(vm, &req, &rl, &rc, (s), strlen(s))
static bool add_header_line(mcs_vm_t* vm, const char* s, size_t l, char** b, size_t* len, size_t* cap, bool* has_ct, bool* has_ua) {
    if (!memchr(s, ':', l)) { mcs_raise(vm, "ArgumentException", "Http header '%.*s' has no ':' (\"Name: value\")", (int)l, s); return false; }
    if (l >= 13 && !strncasecmp(s, "Content-Type:", 13)) *has_ct = true;
    if (l >= 11 && !strncasecmp(s, "User-Agent:", 11)) *has_ua = true;
    return put_str(vm, b, len, cap, s, l) && put_str(vm, b, len, cap, "\r\n", 2);
}
/* user headers: Dictionary<string, string>, "Name: value" string (lines separated by \n)
 * or string[] / List<string> of such lines */
static bool add_headers(mcs_vm_t* vm, mcs_value_t h, char** b, size_t* len, size_t* cap, bool* has_ct, bool* has_ua) {
    if (mcs_is_null(h)) return true;
    int k = mcs_obj_kind(h);
    if (k == MCS_O_DICT) {
        mcs_value_t key, val;
        for (uint32_t i = 0; mcs_dict_at(h, i, &key, &val); i++) {
            char line[512];
            const char* ks = mcs_to_cstr(vm, key);
            if (mcs_has_exception(vm)) return false;
            size_t kl = strlen(ks);
            if (!kl || memchr(ks, ':', kl) || strpbrk(ks, "\r\n")) { mcs_raise(vm, "ArgumentException", "bad Http header name '%s'", ks); return false; }
            if (kl > sizeof line - 4) kl = sizeof line - 4;
            memcpy(line, ks, kl); line[kl] = ':'; line[kl + 1] = ' ';
            const char* vs = mcs_is_null(val) ? "" : mcs_to_cstr(vm, val);
            if (mcs_has_exception(vm)) return false;
            if (strpbrk(vs, "\r\n")) { mcs_raise(vm, "ArgumentException", "Http header '%s': value contains a line break", ks); return false; }
            size_t vl = strlen(vs);
            if (vl > sizeof line - kl - 3) vl = sizeof line - kl - 3;
            memcpy(line + kl + 2, vs, vl);
            if (!add_header_line(vm, line, kl + 2 + vl, b, len, cap, has_ct, has_ua)) return false;
        }
        return true;
    }
    uint32_t n = (k == MCS_O_ARRAY || k == MCS_O_LIST) ? mcs_len(h) : 1;
    for (uint32_t i = 0; i < n; i++) {
        mcs_value_t v = (k == MCS_O_ARRAY || k == MCS_O_LIST) ? mcs_index(h, i) : h;
        if (mcs_is_null(v)) continue;
        const char* s = mcs_to_cstr(vm, v);
        if (mcs_has_exception(vm)) return false;
        while (*s) {
            const char* e = s; while (*e && *e != '\n' && *e != '\r') e++;
            size_t l = (size_t)(e - s);
            if (l && !add_header_line(vm, s, l, b, len, cap, has_ct, has_ua)) return false;
            s = e; while (*s == '\r' || *s == '\n') s++;
        }
    }
    return true;
}
/* Run a request, following redirects. Fills r (status, reason, url, headers) and the sink. */
static bool http_do(mcs_vm_t* vm, const char* method, const char* url0, const uint8_t* body, size_t blen,
                    const char* ctype, mcs_value_t headers, resp_t* r, sink_t* k) {
    mcs__net_state_t* st = mcs__net_state(vm);
    uint32_t timeout = st ? st->http_timeout : MCS_NET_TIMEOUT;
    url_t* u = (url_t*)mcs_mem_realloc(vm, NULL, 0, sizeof(url_t));
    rd_t* rd = (rd_t*)mcs_mem_realloc(vm, NULL, 0, sizeof(rd_t));
    if (!u || !rd) { if (u) mcs_mem_realloc(vm, u, sizeof(url_t), 0); if (rd) mcs_mem_realloc(vm, rd, sizeof(rd_t), 0); return false; }
    char meth[12]; snprintf(meth, sizeof meth, "%s", method);
    if (strlen(url0) >= sizeof r->url) {
        mcs_raise(vm, "ArgumentException", "Http: URL longer than %d characters", (int)sizeof r->url - 1);
        mcs_mem_realloc(vm, u, sizeof(url_t), 0); mcs_mem_realloc(vm, rd, sizeof(rd_t), 0);
        return false;
    }
    memcpy(r->url, url0, strlen(url0) + 1);
    bool ok = false;
    conn_t c; c.fd = -1; c.tls = NULL; c.net = NULL;
    char* req = NULL; size_t rl = 0, rc = 0;
    for (int hop = 0; ; hop++) {
        if (!url_parse(vm, r->url, u, "Http")) break;
        if (conn_open(vm, &c, u->host, u->port, u->tls, timeout, NULL, "Http") < 0) break;
        rc = 256; rl = 0;
        req = (char*)mcs_mem_realloc(vm, NULL, 0, rc);
        if (!req) break;
        char line[200];
        bool std_port = u->port == (u->tls ? 443 : 80);
        snprintf(line, sizeof line, "%s ", meth);
        if (!PUTS(line) || !PUTS(u->path)) break;
        if (std_port) snprintf(line, sizeof line, " HTTP/1.1\r\nHost: %s\r\n", u->host);
        else snprintf(line, sizeof line, " HTTP/1.1\r\nHost: %s:%d\r\n", u->host, u->port);
        if (!PUTS(line)) break;
        bool has_ct = false, has_ua = false;
        if (!add_headers(vm, headers, &req, &rl, &rc, &has_ct, &has_ua)) break;
        if (!has_ua) { snprintf(line, sizeof line, "User-Agent: %s\r\n", st ? st->user_agent : "MicroCS"); if (!PUTS(line)) break; }
        if (!PUTS("Accept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n")) break;
        if (body) {
            if (!has_ct) {
                const char* ct = ctype;
                if (!ct) ct = blen && (body[0] == '{' || body[0] == '[') ? "application/json" : "text/plain; charset=utf-8";
                snprintf(line, sizeof line, "Content-Type: %s\r\n", ct);
                if (!PUTS(line)) break;
            }
            snprintf(line, sizeof line, "Content-Length: %u\r\n", (unsigned)blen);
            if (!PUTS(line)) break;
        }
        if (!PUTS("\r\n")) break;
        int w = conn_write(vm, &c, (const uint8_t*)req, rl, timeout);
        if (w >= 0 && body && blen) w = conn_write(vm, &c, body, blen, timeout);
        mcs_mem_realloc(vm, req, rc, 0); req = NULL;
        if (w < 0) { if (w != -EINTR) sock_fail(vm, "Http", -w); break; }

        /* status line + headers */
        memset(rd, 0, sizeof *rd);
        rd->vm = vm; rd->c = &c; rd->timeout = timeout;
        char sl[160];
        if (rd_line(rd, sl, sizeof sl, "status line") < 0) break;
        if (strncmp(sl, "HTTP/", 5) || !strchr(sl, ' ')) { mcs_raise(vm, "IOException", "Http: not an HTTP response from %s", u->host); break; }
        const char* sp = strchr(sl, ' ');
        r->status = atoi(sp + 1);
        const char* rs = strchr(sp + 1, ' ');
        snprintf(r->reason, sizeof r->reason, "%s", rs ? rs + 1 : "");
        if (!r->hdr) { r->hdr_cap = MCS_HTTP_MAX_HEADER; r->hdr = (char*)mcs_mem_realloc(vm, NULL, 0, r->hdr_cap); if (!r->hdr) break; }
        r->hdr_len = 0;
        bool hdr_ok = true;
        for (;;) {
            char hl[256];
            int n = rd_line(rd, hl, sizeof hl, "response headers");
            if (n < 0) { hdr_ok = false; break; }
            if (n == 0) break;
            if (r->hdr_len + (size_t)n + 2 <= r->hdr_cap) { memcpy(r->hdr + r->hdr_len, hl, (size_t)n); r->hdr_len += (size_t)n; memcpy(r->hdr + r->hdr_len, "\r\n", 2); r->hdr_len += 2; }
        }
        if (!hdr_ok) break;
        size_t vl; const char* v;
        bool redirect = (r->status == 301 || r->status == 302 || r->status == 303 || r->status == 307 || r->status == 308);
        if (redirect && (!st || !st->no_redirect) && hop < MCS_HTTP_MAX_REDIRECTS
            && (v = hdr_find(r->hdr, r->hdr_len, "Location", &vl)) != NULL && vl > 0) {
            char loc[sizeof r->url];
            if (vl >= sizeof loc) vl = sizeof loc - 1;
            memcpy(loc, v, vl); loc[vl] = 0;
            char nu[sizeof r->url + sizeof u->path + sizeof u->host + 32];
            if (loc[0] == '/') snprintf(nu, sizeof nu, "%s://%s:%d%s", u->tls ? "https" : "http", u->host, u->port, loc);
            else if (strstr(loc, "://")) snprintf(nu, sizeof nu, "%s", loc);
            else {                                       /* relative to the current directory */
                char base[sizeof u->path]; snprintf(base, sizeof base, "%s", u->path);
                char* q = strchr(base, '?'); if (q) *q = 0;
                char* sl2 = strrchr(base, '/'); if (sl2) sl2[1] = 0;
                snprintf(nu, sizeof nu, "%s://%s:%d%s%s", u->tls ? "https" : "http", u->host, u->port, base, loc);
            }
            if (strlen(nu) >= sizeof r->url) { mcs_raise(vm, "IOException", "Http: redirect URL too long"); break; }
            memcpy(r->url, nu, strlen(nu) + 1);
            if (r->status == 303 || ((r->status == 301 || r->status == 302) && strcmp(meth, "GET") && strcmp(meth, "HEAD"))) {
                snprintf(meth, sizeof meth, "GET"); body = NULL; blen = 0;
            }
            conn_close(&c);
            continue;
        }
        /* body */
        if (!strcmp(meth, "HEAD") || r->status == 204 || r->status == 304 || (r->status >= 100 && r->status < 200)) { ok = true; break; }
        if ((v = hdr_find(r->hdr, r->hdr_len, "Transfer-Encoding", &vl)) != NULL && vl >= 7 && !strncasecmp(v, "chunked", 7))
            ok = chunked_body(rd, k);
        else if ((v = hdr_find(r->hdr, r->hdr_len, "Content-Length", &vl)) != NULL)
            ok = copy_body(rd, k, (size_t)strtoul(v, NULL, 10));
        else ok = copy_body(rd, k, (size_t)-1);
        break;
    }
    if (req) mcs_mem_realloc(vm, req, rc, 0);
    conn_close(&c);
    mcs_mem_realloc(vm, u, sizeof(url_t), 0);
    mcs_mem_realloc(vm, rd, sizeof(rd_t), 0);
    return ok && !mcs_has_exception(vm);
}
/* Http.X(...) -> HttpResponse */
static mcs_value_t http_call(mcs_vm_t* vm, const char* method, mcs_value_t url, mcs_value_t body, mcs_value_t ctype, mcs_value_t headers) {
    const char* u = mcs_to_cstr(vm, url); CHECK();
    const char* ct = mcs_is_null(ctype) ? NULL : mcs_to_cstr(vm, ctype); CHECK();
    const uint8_t* p = NULL; size_t n = 0; uint8_t* heap = NULL;
    if (!mcs_is_null(body) && !mcs__bytes_arg(vm, body, &p, &n, &heap)) return mcs_null();
    mcs_value_t o;
    if (mcs_new_object(vm, "HttpResponse", 0, NULL, &o) != MCS_OK) { mcs__bytes_free(vm, heap, n); return mcs_null(); }
    mcs_push_root(vm, o);
    resp_t* r = (resp_t*)mcs_userdata(o);
    sink_t k; memset(&k, 0, sizeof k); k.vm = vm; k.mem = r;
    bool ok = http_do(vm, method, u, mcs_is_null(body) ? NULL : (p ? p : (const uint8_t*)""), n, ct, headers, r, &k);
    mcs__bytes_free(vm, heap, n);
    mcs_pop_root(vm, 1);
    return ok ? o : mcs_null();
}
#define A(i) (argc > (i) ? argv[i] : mcs_null())
NATIVE(http_get) { if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Http.Get(url[, headers])"); return mcs_null(); } return http_call(vm, "GET", argv[0], mcs_null(), mcs_null(), A(1)); }
NATIVE(http_head) { if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Http.Head(url[, headers])"); return mcs_null(); } return http_call(vm, "HEAD", argv[0], mcs_null(), mcs_null(), A(1)); }
NATIVE(http_delete) { if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Http.Delete(url[, headers])"); return mcs_null(); } return http_call(vm, "DELETE", argv[0], mcs_null(), mcs_null(), A(1)); }
#define BODYCALL(name, M) NATIVE(name) { \
    if (argc < 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "Http." #M "(url, body[, contentType[, headers]])"); return mcs_null(); } \
    mcs_value_t b = mcs_is_null(argv[1]) ? mcs_string(vm, "") : argv[1]; \
    return http_call(vm, #M, argv[0], b, A(2), A(3)); }
BODYCALL(http_post, POST)
BODYCALL(http_put, PUT)
BODYCALL(http_patch, PATCH)
NATIVE(http_request) {  /* (method, url[, body[, contentType[, headers]]]) */
    if (argc < 2 || argc > 5) { mcs_raise(vm, "ArgumentException", "Http.Request(method, url[, body[, contentType[, headers]]])"); return mcs_null(); }
    const char* m = mcs_to_cstr(vm, argv[0]); CHECK();
    char mu[12]; size_t i = 0;
    for (; m[i] && i < sizeof mu - 1; i++) mu[i] = (char)(m[i] >= 'a' && m[i] <= 'z' ? m[i] - 32 : m[i]);
    mu[i] = 0;
    if (!i || m[i]) { mcs_raise(vm, "ArgumentException", "Http.Request: bad method '%s'", m); return mcs_null(); }
    return http_call(vm, mu, argv[1], A(2), A(3), A(4));
}
NATIVE(http_getstring) {
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Http.GetString(url[, headers])"); return mcs_null(); }
    mcs_value_t o = http_call(vm, "GET", argv[0], mcs_null(), mcs_null(), A(1));
    if (mcs_is_null(o)) return o;
    mcs_push_root(vm, o);
    resp_t* r = (resp_t*)mcs_userdata(o);
    mcs_value_t s = mcs_null();
    if (r->status < 200 || r->status >= 300) mcs_raise(vm, "HttpRequestException", "HTTP %d %s: %s", r->status, r->reason, r->url);
    else s = mcs_string_n(vm, r->body ? r->body : "", r->body_len);
    mcs_pop_root(vm, 1);
    return s;
}
NATIVE(http_download) { /* (url, path[, headers]) -> bytes written */
#if MCS_ENABLE_FS
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "Http.Download(url, path[, headers])"); return mcs_null(); }
    mcs_vfs_t* vfs = (mcs_vfs_t*)mcs_get_ext(vm, MCS_EXT_VFS);
    if (!vfs) { mcs_raise(vm, "NotSupportedException", "Http.Download: no filesystem"); return mcs_null(); }
    STR_ARG(0, url); STR_ARG(1, path);
    char tmp[MCS_VFS_PATH_MAX + 8];
    if (strlen(path) + 5 > MCS_VFS_PATH_MAX) { mcs_raise(vm, "ArgumentException", "Http.Download: path too long"); return mcs_null(); }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    mcs_vfs_file_t f;
    int e = mcs_vfs_open(vfs, tmp, MCS_VFS_WRITE, &f);
    if (e < 0) { mcs_raise(vm, "IOException", "Http.Download: cannot create '%s': %s", tmp, mcs_vfs_strerror(e)); return mcs_null(); }
    mcs_value_t o;
    if (mcs_new_object(vm, "HttpResponse", 0, NULL, &o) != MCS_OK) { mcs_vfs_close(&f); mcs_vfs_remove(vfs, tmp); return mcs_null(); }
    mcs_push_root(vm, o);
    resp_t* r = (resp_t*)mcs_userdata(o);
    sink_t k; memset(&k, 0, sizeof k); k.vm = vm; k.mem = r; k.file = &f;
    bool ok = http_do(vm, "GET", url, NULL, 0, NULL, A(2), r, &k);
    mcs_vfs_close(&f);
    if (ok && (r->status < 200 || r->status >= 300)) { mcs_raise(vm, "HttpRequestException", "HTTP %d %s: %s", r->status, r->reason, r->url); ok = false; }
    if (ok) {
        mcs_vfs_stat_t stt;
        if (mcs_vfs_stat(vfs, path, &stt) == 0) mcs_vfs_remove(vfs, path);
        e = mcs_vfs_rename(vfs, tmp, path);
        if (e < 0) { mcs_raise(vm, "IOException", "Http.Download: cannot rename '%s' to '%s': %s", tmp, path, mcs_vfs_strerror(e)); ok = false; }
    }
    if (!ok) mcs_vfs_remove(vfs, tmp);
    mcs_pop_root(vm, 1);
    return ok ? mcs_int((mcs_int_t)k.total) : mcs_null();
#else
    (void)argc; (void)argv;
    mcs_raise(vm, "NotSupportedException", "Http.Download: built without MCS_ENABLE_FS");
    return mcs_null();
#endif
}
NATIVE(http_gettimeout) { mcs__net_state_t* s = mcs__net_state(vm); return mcs_int(s ? (mcs_int_t)s->http_timeout : MCS_NET_TIMEOUT); }
NATIVE(http_settimeout) {
    mcs__net_state_t* s = mcs__net_state(vm); INT_ARG(0, v);
    if (v < 1) { mcs_raise(vm, "ArgumentOutOfRangeException", "Http.Timeout: ms >= 1"); return mcs_null(); }
    if (s) s->http_timeout = (uint32_t)v;
    return mcs_null();
}
NATIVE(http_getua) { mcs__net_state_t* s = mcs__net_state(vm); return mcs_string(vm, s ? s->user_agent : ""); }
NATIVE(http_setua) {
    mcs__net_state_t* s = mcs__net_state(vm); STR_ARG(0, v);
    if (strlen(v) >= sizeof s->user_agent) { mcs_raise(vm, "ArgumentException", "Http.UserAgent: at most %d characters", (int)sizeof s->user_agent - 1); return mcs_null(); }
    if (s) snprintf(s->user_agent, sizeof s->user_agent, "%s", v);
    return mcs_null();
}
NATIVE(http_getredir) { mcs__net_state_t* s = mcs__net_state(vm); return mcs_bool(!s || !s->no_redirect); }
NATIVE(http_setredir) { mcs__net_state_t* s = mcs__net_state(vm); if (s) s->no_redirect = !mcs_truthy(argv[0]); return mcs_null(); }
NATIVE(http_urlencode) {
    STR_ARG(0, in);
    size_t n = strlen(in), cap = n * 3 + 1;
    char* b = (char*)mcs_mem_realloc(vm, NULL, 0, cap);
    if (!b) return mcs_null();
    size_t o = 0;
    static const char hx[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)in[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') b[o++] = (char)ch;
        else { b[o++] = '%'; b[o++] = hx[ch >> 4]; b[o++] = hx[ch & 15]; }
    }
    mcs_value_t s = mcs_string_n(vm, b, o);
    mcs_mem_realloc(vm, b, cap, 0);
    return s;
}
static const mcs_reg_t http_fns[] = {
    MCS_FN("Get", http_get, -1), MCS_FN("Head", http_head, -1), MCS_FN("Delete", http_delete, -1),
    MCS_FN("Post", http_post, -1), MCS_FN("Put", http_put, -1), MCS_FN("Patch", http_patch, -1),
    MCS_FN("Request", http_request, -1), MCS_FN("GetString", http_getstring, -1), MCS_FN("Download", http_download, -1),
    MCS_FN("UrlEncode", http_urlencode, 1),
    MCS_GET("Timeout", http_gettimeout), MCS_SET("Timeout", http_settimeout),
    MCS_GET("UserAgent", http_getua), MCS_SET("UserAgent", http_setua),
    MCS_GET("FollowRedirects", http_getredir), MCS_SET("FollowRedirects", http_setredir), MCS_REG_END };

void mcs__sock_open(mcs_vm_t* vm, const mcs_net_t* net) {
    (void)net;
    mcs_register_class(vm, &tcp_def);
    mcs_register_class(vm, &lst_def);
    mcs_register_class(vm, &udp_def);
    mcs_register_class(vm, &resp_def);
    mcs_register_module(vm, "Dns", dns_fns);
    mcs_register_module(vm, "Http", http_fns);
    mcs__srv_open(vm);
}
#endif
