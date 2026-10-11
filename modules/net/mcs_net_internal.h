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
#endif
#endif
#endif
