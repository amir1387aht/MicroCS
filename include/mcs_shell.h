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
 *
 * Interactive REPL (MCS_SHELL_REPL_MAX > 0): the `repl` command - or
 * sh->repl = true before mcs_shell_boot() - turns the session into a C# prompt
 * for humans on a serial terminal: multi-line input, expressions are printed,
 * Ctrl-E paste mode (Ctrl-D runs), Ctrl-C clears, dot commands (.help, .ls,
 * .run, ...). Ctrl-A (0x01) always returns to the machine protocol and replies
 * "\x04OK", so tools can take over from any state.
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
#ifndef MCS_SHELL_REPL_MAX
#define MCS_SHELL_REPL_MAX 1024    /* multi-line REPL input buffer; 0 removes the REPL */
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
    /* interactive REPL */
    bool repl;                  /* REPL mode active (set before boot to start in it) */
    bool echo;                  /* echo typed characters + line editing (raw UART terminals) */
    bool paste;                 /* Ctrl-E paste mode */
    bool last_cr;
#if MCS_SHELL_REPL_MAX > 0
    char code[MCS_SHELL_REPL_MAX];
    size_t code_len;
#endif
} mcs_shell_t;

void mcs_shell_init(mcs_shell_t* sh, mcs_vm_t* vm, struct mcs_vfs* vfs, struct mcs_sched* sched, mcs_transport_t t);
/* Run the boot scripts (if run_scripts), then print the banner and status line. */
void mcs_shell_boot(mcs_shell_t* sh, bool run_scripts);
/* Process input for up to timeout_ms and run due jobs. False once the session ends. */
bool mcs_shell_step(mcs_shell_t* sh, uint32_t timeout_ms);
void mcs_shell_run(mcs_shell_t* sh);
/* Call from cfg.hook_fn: returns 1 if Ctrl-C arrived on the transport. */
int mcs_shell_poll_break(mcs_vm_t* vm);
/* Switch between the interactive REPL and the machine protocol. */
void mcs_shell_set_repl(mcs_shell_t* sh, bool on);

/* REPL helpers (also used by the host CLI):
 * mcs_repl_complete: false while brackets / strings / comments are still open.
 * mcs_repl_prepare: turns one REPL entry into a program - a bare expression
 *   becomes Console.WriteLine(expr); a statement without ';' gets one.
 *   Returns the length written to out, or -1 if it does not fit. */
bool mcs_repl_complete(const char* src, size_t len);
int mcs_repl_prepare(const char* src, size_t len, char* out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
