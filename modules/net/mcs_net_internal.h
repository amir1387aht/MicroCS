/* MicroCS - network module internals shared by mcs_net_lib.c and mcs_net_sock.c (not public API). */
#ifndef MCS_NET_INTERNAL_H
#define MCS_NET_INTERNAL_H
#include "mcs_net.h"
#if MCS_ENABLE_NET
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
#define OPT_STR(i, var) const char* var = argc > (i) && !mcs_is_null(argv[i]) ? mcs_to_cstr(vm, argv[i]) : NULL; CHECK()

typedef struct {
    const mcs_net_t* net;
    int self_pin, cb_pin;            /* this object, list [Wifi.OnEvent, EspNow.OnReceive] */
    int last_reason;
    uint8_t have_last;               /* Wifi.Reconnect: the last Connect arguments */
    mcs_wifi_sta_cfg_t last;
    char eap_identity[65], eap_user[65];  /* copies for Reconnect (last.identity / username) */
    uint32_t http_timeout;           /* Http.Timeout (ms) */
    char user_agent[48];
    uint8_t no_redirect;             /* Http.FollowRedirects = false */
} mcs__net_state_t;

enum { MCS__NET_CB_WIFI = 0, MCS__NET_CB_ESPNOW = 1, MCS__NET_CB_COUNT };

mcs__net_state_t* mcs__net_state(mcs_vm_t* vm);
/* exception for a negative MCS_HAL_E* code; returns null */
mcs_value_t mcs__net_fail(mcs_vm_t* vm, const char* op, int rc);
/* dotted IPv4 <-> 4 bytes */
bool mcs__ip_parse(const char* s, uint8_t ip[4]);
void mcs__ip_fmt(const uint8_t ip[4], char out[16]);
/* string / byte[] / List<byte> -> bytes. *heap is set when a VM-heap copy was made
 * (free with mcs_mem_realloc(vm, *heap, *n ? *n : 1, 0)); false with an exception raised */
bool mcs__bytes_arg(mcs_vm_t* vm, mcs_value_t v, const uint8_t** p, size_t* n, uint8_t** heap);
void mcs__bytes_free(mcs_vm_t* vm, uint8_t* heap, size_t n);
mcs_value_t mcs__bytes_new(mcs_vm_t* vm, const uint8_t* p, size_t n);
/* wait `ms` in slices that stay abortable; false when the run must stop */
bool mcs__net_wait(mcs_vm_t* vm, uint32_t ms);

#if MCS_ENABLE_SOCKETS
/* host name or dotted address -> IPv4 (".local" names through mDNS when the port has it) */
int mcs__resolve4(mcs_vm_t* vm, const char* host, uint8_t ip[4], uint32_t timeout_ms);
void mcs__sock_open(mcs_vm_t* vm, const mcs_net_t* net);
/* a TCP or TLS stream */
typedef struct {
    int fd;                      /* -1 = closed */
    void* tls;                   /* TLS stream from net->tls_open */
    const mcs_net_t* net;
} mcs__conn_t;
void mcs__set_nonblock(int fd);
void mcs__no_sigpipe(int fd);
int mcs__wait_fd(mcs_vm_t* vm, int fd, bool wr, uint32_t timeout_ms);   /* 1 ready, 0 timeout, -1 aborted */
mcs_value_t mcs__sock_fail(mcs_vm_t* vm, const char* op, int e);       /* raises, returns null */
int mcs__conn_open(mcs_vm_t* vm, mcs__conn_t* c, const char* host, int port, bool tls, uint32_t timeout, const char* ca, const char* op);
int mcs__conn_write(mcs_vm_t* vm, mcs__conn_t* c, const uint8_t* p, size_t n, uint32_t timeout);   /* bytes or -errno */
int mcs__conn_read(mcs_vm_t* vm, mcs__conn_t* c, uint8_t* buf, size_t n, uint32_t timeout);        /* bytes, 0 closed, -errno */
void mcs__conn_close(mcs__conn_t* c);
void mcs__web_open(mcs_vm_t* vm);    /* HttpServer, HttpRequest, WebSocket, DnsServer (mcs_net_web.c) */
/* keep a value alive for a native object (slot in the state's pinned list) */
int mcs__keep(mcs_vm_t* vm, mcs_value_t v);
mcs_value_t mcs__kept(mcs_vm_t* vm, int slot);
void mcs__unkeep(mcs_vm_t* vm, int slot);
#endif
#endif
#endif
