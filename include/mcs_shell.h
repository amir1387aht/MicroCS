/*
 * MicroCS - standalone runtime and script manager (optional, MCS_ENABLE_SHELL).
 *
 * Turns a VM + VFS (+ optional scheduler) into a device runtime that can be
 * managed over any byte transport (UART first; USB-CDC, TCP or BLE later only
 * need another mcs_transport_t).
 *
 * Boot: /boot.mcsb or /boot.cs, then /jobs.cfg (scheduler), then /main.mcsb or
 * /main.cs. Afterwards the shell serves commands and polls the scheduler.
 *
 * Protocol: one command per line. Every reply ends with a status line that
 * starts with EOT (0x04): "\x04OK" or "\x04ERR <message>". Uploads are
 * length-prefixed and binary safe:
 *     put <path> <len>   -> "\x04READY", then exactly <len> raw bytes, -> "\x04OK"
 *     get <path>         -> "\x04DATA <len>", <len> raw bytes, "\x04OK"
 * A 0x03 byte (Ctrl-C) received while a script runs stops it.
 * See docs/STANDALONE.md and tools/mcs_remote.py.
 */
#ifndef MCS_SHELL_H
#define MCS_SHELL_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef MCS_SHELL_LINE_MAX
#define MCS_SHELL_LINE_MAX 256
#endif

typedef struct {
    /* read up to n bytes, waiting at most timeout_ms; returns count (0 = timeout)
     * or < 0 when the transport is closed */
    int (*read)(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms);
    void (*write)(void* ud, const char* data, size_t n);
    void* ud;
} mcs_transport_t;

struct mcs_vfs;
struct mcs_sched;
typedef struct mcs_shell {
    mcs_vm_t* vm;
    struct mcs_vfs* vfs;
    struct mcs_sched* sched;    /* may be NULL */
    mcs_transport_t t;
    char line[MCS_SHELL_LINE_MAX];
    size_t len;
    uint8_t in[256];            /* input queue: all received bytes pass through it */
    uint16_t in_pos, in_len;
    bool quit;
    bool overflow;
} mcs_shell_t;

void mcs_shell_init(mcs_shell_t* sh, mcs_vm_t* vm, struct mcs_vfs* vfs, struct mcs_sched* sched, mcs_transport_t t);
/* Run the boot scripts (if run_scripts), then print the banner and status line. */
void mcs_shell_boot(mcs_shell_t* sh, bool run_scripts);
/* Process input for up to timeout_ms and run due jobs. False once the session ends. */
bool mcs_shell_step(mcs_shell_t* sh, uint32_t timeout_ms);
void mcs_shell_run(mcs_shell_t* sh);
/* Call from cfg.hook_fn: returns 1 if Ctrl-C arrived on the transport. */
int mcs_shell_poll_break(mcs_vm_t* vm);

#ifdef __cplusplus
}
#endif
#endif
