/* MicroCS - standalone runtime: boot sequence and script management protocol. */
#include "mcs_shell.h"
#if MCS_ENABLE_SHELL
#include "mcs_vfs.h"
#if MCS_ENABLE_SCHED
#include "mcs_sched.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define EOT "\x04"

static void out(mcs_shell_t* sh, const char* s) { sh->t.write(sh->t.ud, s, strlen(s)); }
static void outf(mcs_shell_t* sh, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void outf(mcs_shell_t* sh, const char* fmt, ...) {
    char buf[200];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) sh->t.write(sh->t.ud, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}
static void ok(mcs_shell_t* sh) { out(sh, EOT "OK\n"); }
static void err(mcs_shell_t* sh, const char* msg) { outf(sh, EOT "ERR %s\n", msg); }

void mcs_shell_init(mcs_shell_t* sh, mcs_vm_t* vm, struct mcs_vfs* vfs, struct mcs_sched* sched, mcs_transport_t t) {
    memset(sh, 0, sizeof *sh);
    sh->vm = vm; sh->vfs = vfs; sh->sched = sched; sh->t = t;
    mcs_set_ext(vm, MCS_EXT_SHELL, sh);
}

int mcs_shell_poll_break(mcs_vm_t* vm) {
    mcs_shell_t* sh = (mcs_shell_t*)mcs_get_ext(vm, MCS_EXT_SHELL);
    if (!sh) return 0;
    uint8_t buf[32];
    int n = sh->t.read(sh->t.ud, buf, sizeof buf, 0);
    int brk = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == 0x03) { brk = 1; continue; }
        if (sh->in_len < sizeof sh->in) sh->in[sh->in_len++] = buf[i];   /* replayed after the script */
    }
    return brk;
}

static bool exists(mcs_shell_t* sh, const char* p) { mcs_vfs_stat_t st; return !mcs_vfs_stat(sh->vfs, p, &st) && !st.is_dir; }

static mcs_result_t run_first(mcs_shell_t* sh, const char* a, const char* b) {
    const char* p = exists(sh, a) ? a : exists(sh, b) ? b : NULL;
    return p ? mcs_exec_file(sh->vm, sh->vfs, p) : MCS_OK;
}

void mcs_shell_boot(mcs_shell_t* sh, bool run_scripts) {
    if (sh->vfs && run_scripts) {
        run_first(sh, "/boot.mcsb", "/boot.cs");
#if MCS_ENABLE_SCHED
        char* cfg; size_t len;
        if (sh->sched && !mcs_vfs_read_file(sh->vfs, "/jobs.cfg", &cfg, &len)) {
            int n = mcs_sched_load_config(sh->sched, cfg);
            if (n < 0) outf(sh, "jobs.cfg: syntax error on line %d\n", -n);
            mcs_vfs_free(sh->vfs, cfg, len);
        }
#endif
        run_first(sh, "/main.mcsb", "/main.cs");
    }
    outf(sh, "MicroCS %s shell. Type 'help'.\n", MCS_VERSION_STRING);
    ok(sh);
}

/* read exactly n bytes with a per-chunk timeout */
static int read_exact(mcs_shell_t* sh, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n && sh->in_pos < sh->in_len) buf[got++] = sh->in[sh->in_pos++];
    while (got < n) {
        int r = sh->t.read(sh->t.ud, buf + got, n - got, 5000);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static void cmd_put(mcs_shell_t* sh, const char* path, const char* len_s) {
    char* end;
    unsigned long n = strtoul(len_s ? len_s : "", &end, 10);
    if (!path || !len_s || *end) { err(sh, "usage: put <path> <len>"); return; }
    char tmp[MCS_VFS_PATH_MAX];
    if (snprintf(tmp, sizeof tmp, "%s.part", path) >= (int)sizeof tmp) { err(sh, "path too long"); return; }
    mcs_vfs_file_t f;
    int e = mcs_vfs_open(sh->vfs, tmp, MCS_VFS_WRITE, &f);
    if (e) { err(sh, mcs_vfs_strerror(e)); return; }
    out(sh, EOT "READY\n");
    uint8_t buf[256];
    while (n) {
        size_t k = n < sizeof buf ? n : sizeof buf;
        if (read_exact(sh, buf, k)) { mcs_vfs_close(&f); mcs_vfs_remove(sh->vfs, tmp); err(sh, "upload timed out"); return; }
        if (!e && mcs_vfs_write(&f, buf, k) != (int)k) e = MCS_VFS_ENOSPC;   /* keep draining the stream */
        n -= k;
    }
    int c = mcs_vfs_close(&f);
    if (!e) e = c;
    /* replace atomically where the backend allows: a failed upload never
     * leaves a half-written script in place */
    if (!e) {
        mcs_vfs_remove(sh->vfs, path);
        e = mcs_vfs_rename(sh->vfs, tmp, path);
    }
    if (e) { mcs_vfs_remove(sh->vfs, tmp); err(sh, mcs_vfs_strerror(e)); return; }
    ok(sh);
}

static void cmd_get(mcs_shell_t* sh, const char* path) {
    mcs_vfs_stat_t st;
    int e = path ? mcs_vfs_stat(sh->vfs, path, &st) : MCS_VFS_EINVAL;
    if (!e && st.is_dir) e = MCS_VFS_EISDIR;
    mcs_vfs_file_t f;
    if (!e) e = mcs_vfs_open(sh->vfs, path, MCS_VFS_READ, &f);
    if (e) { err(sh, mcs_vfs_strerror(e)); return; }
    outf(sh, EOT "DATA %u\n", (unsigned)st.size);
    char buf[256];
    uint32_t left = st.size;
    while (left) {
        int r = mcs_vfs_read(&f, buf, left < sizeof buf ? left : sizeof buf);
        if (r <= 0) { memset(buf, 0, sizeof buf); r = (int)(left < sizeof buf ? left : sizeof buf); }  /* keep framing */
        sh->t.write(sh->t.ud, buf, (size_t)r);
        left -= (uint32_t)r;
    }
    mcs_vfs_close(&f);
    out(sh, "\n");
    ok(sh);
}

static int ls_cb(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    mcs_shell_t* sh = (mcs_shell_t*)ud;
    if (st->is_dir) outf(sh, "d        - %s\n", name);
    else outf(sh, "f %8u %s\n", (unsigned)st->size, name);
    return 0;
}

#if MCS_ENABLE_SCHED
static const char* job_state(int s) {
    return s == MCS_JOB_ACTIVE ? "active" : s == MCS_JOB_DONE ? "done" : s == MCS_JOB_FAILED ? "failed" : "cancelled";
}
static bool parse_ms(const char* s, uint32_t* out) {
    char* end; unsigned long v = strtoul(s, &end, 10);
    if (end == s) return false;
    if (!*end || !strcmp(end, "ms")) *out = (uint32_t)v;
    else if (!strcmp(end, "s")) *out = (uint32_t)v * 1000;
    else if (!strcmp(end, "m")) *out = (uint32_t)v * 60000;
    else return false;
    return true;
}
#endif

static void cmd_help(mcs_shell_t* sh) {
    out(sh, "ls [dir] | cat <f> | put <f> <len> | get <f> | rm <f> | mkdir <d> | mv <a> <b>\n"
            "run <f> | exec <code> | jobs | every <t> <f> | after <t> <f> | cancel <id>\n"
            "mem | info | quit       (Ctrl-C stops a running script)\n");
    ok(sh);
}

static void report(mcs_shell_t* sh, mcs_result_t r) {
    if (r == MCS_OK) ok(sh);
    else err(sh, mcs_last_error(sh->vm)[0] ? mcs_last_error(sh->vm) : "failed");
}

static void dispatch(mcs_shell_t* sh, char* line) {
    while (*line == ' ') line++;
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
    if (!*line) return;
    /* exec keeps the rest of the line verbatim */
    if (!strncmp(line, "exec ", 5)) {
#if MCS_ENABLE_COMPILER
        report(sh, mcs_exec_source(sh->vm, "<shell>", line + 5));
#else
        err(sh, "no compiler in this build");
#endif
        return;
    }
    char* argv[4] = { 0 };
    int argc = 0;
    for (char* p = strtok(line, " "); p && argc < 4; p = strtok(NULL, " ")) argv[argc++] = p;
    const char* c = argv[0];
    bool need_fs = strcmp(c, "help") && strcmp(c, "info") && strcmp(c, "mem") && strcmp(c, "quit") && strcmp(c, "exit") && strcmp(c, "jobs") && strcmp(c, "cancel");
    if (need_fs && !sh->vfs) { err(sh, "no filesystem"); return; }
    int e;
    if (!strcmp(c, "help")) cmd_help(sh);
    else if (!strcmp(c, "ls")) {
        if ((e = mcs_vfs_list(sh->vfs, argc > 1 ? argv[1] : "/", ls_cb, sh))) err(sh, mcs_vfs_strerror(e)); else ok(sh);
    } else if (!strcmp(c, "cat")) {
        char* d; size_t n;
        if (argc < 2) { err(sh, "usage: cat <path>"); return; }
        if ((e = mcs_vfs_read_file(sh->vfs, argv[1], &d, &n))) { err(sh, mcs_vfs_strerror(e)); return; }
        sh->t.write(sh->t.ud, d, n);
        if (n && d[n - 1] != '\n') out(sh, "\n");
        mcs_vfs_free(sh->vfs, d, n);
        ok(sh);
    } else if (!strcmp(c, "put")) cmd_put(sh, argv[1], argv[2]);
    else if (!strcmp(c, "get")) cmd_get(sh, argv[1]);
    else if (!strcmp(c, "rm")) {
        if (argc < 2) { err(sh, "usage: rm <path>"); return; }
        if ((e = mcs_vfs_remove(sh->vfs, argv[1]))) err(sh, mcs_vfs_strerror(e)); else ok(sh);
    } else if (!strcmp(c, "mkdir")) {
        if (argc < 2) { err(sh, "usage: mkdir <path>"); return; }
        if ((e = mcs_vfs_mkdir(sh->vfs, argv[1]))) err(sh, mcs_vfs_strerror(e)); else ok(sh);
    } else if (!strcmp(c, "mv")) {
        if (argc < 3) { err(sh, "usage: mv <from> <to>"); return; }
        if ((e = mcs_vfs_rename(sh->vfs, argv[1], argv[2]))) err(sh, mcs_vfs_strerror(e)); else ok(sh);
    } else if (!strcmp(c, "run")) {
        if (argc < 2) { err(sh, "usage: run <path>"); return; }
        report(sh, mcs_exec_file(sh->vm, sh->vfs, argv[1]));
    } else if (!strcmp(c, "mem")) {
        mcs_mem_stats_t st; mcs_mem_stats(sh->vm, &st);
        outf(sh, "heap %u bytes in use, peak %u, %u objects, %u collections\n",
             (unsigned)st.bytes_in_use, (unsigned)st.peak_bytes, (unsigned)st.objects, (unsigned)st.collections);
        ok(sh);
    } else if (!strcmp(c, "info")) {
        outf(sh, "MicroCS %s features=0x%04x value=%u bytes\n", MCS_VERSION_STRING, (unsigned)mcs_features(), (unsigned)sizeof(mcs_value_t));
        ok(sh);
    }
#if MCS_ENABLE_SCHED
    else if (!strcmp(c, "jobs")) {
        if (!sh->sched) { err(sh, "no scheduler"); return; }
        for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) {
            mcs_job_t* j = &sh->sched->jobs[i];
            if (j->state == MCS_JOB_FREE) continue;
            outf(sh, "%d %s %s %u ms runs=%u %s\n", j->id, job_state(j->state), j->periodic ? "every" : "once",
                 (unsigned)j->period_ms, (unsigned)j->runs, j->is_file ? j->path : "<delegate>");
        }
        ok(sh);
    } else if (!strcmp(c, "every") || !strcmp(c, "after")) {
        uint32_t t;
        if (!sh->sched || argc < 3 || !parse_ms(argv[1], &t)) { err(sh, "usage: every|after <time> <path>"); return; }
        bool every = c[0] == 'e';
        int id = mcs_sched_add_file(sh->sched, argv[2], every ? 0 : t, every ? t : 0, 1);
        if (id < 0) err(sh, "job table full"); else { outf(sh, "job %d\n", id); ok(sh); }
    } else if (!strcmp(c, "cancel")) {
        if (!sh->sched || argc < 2) { err(sh, "usage: cancel <id>"); return; }
        if (mcs_sched_cancel(sh->sched, atoi(argv[1]))) ok(sh); else err(sh, "no such job");
    }
#endif
    else if (!strcmp(c, "quit") || !strcmp(c, "exit")) { sh->quit = true; ok(sh); }
    else { outf(sh, EOT "ERR unknown command '%s'\n", c); }
}

static void feed(mcs_shell_t* sh, uint8_t b) {
    if (b == '\n') {
        sh->line[sh->len] = 0;
        if (sh->overflow) err(sh, "line too long");
        else dispatch(sh, sh->line);
        sh->len = 0; sh->overflow = false;
    } else if (b == 0x03) {
        sh->len = 0;     /* Ctrl-C at the prompt clears the line */
    } else if (sh->len + 1 < sizeof sh->line) sh->line[sh->len++] = (char)b;
    else sh->overflow = true;
}

bool mcs_shell_step(mcs_shell_t* sh, uint32_t timeout_ms) {
    if (sh->quit) return false;
    if (sh->in_pos >= sh->in_len) {
        sh->in_pos = sh->in_len = 0;
        int n = sh->t.read(sh->t.ud, sh->in, sizeof sh->in, timeout_ms);
        if (n < 0) { sh->quit = true; return false; }
        sh->in_len = (uint16_t)n;
    }
    /* commands may consume queued bytes themselves (put) or append to the
     * queue while a script runs (mcs_shell_poll_break) */
    while (sh->in_pos < sh->in_len && !sh->quit) feed(sh, sh->in[sh->in_pos++]);
    if (sh->in_pos >= sh->in_len) sh->in_pos = sh->in_len = 0;
#if MCS_ENABLE_SCHED
    if (sh->sched) mcs_sched_poll(sh->sched);
#endif
    return !sh->quit;
}

void mcs_shell_run(mcs_shell_t* sh) {
    for (;;) {
        uint32_t wait = 100;
#if MCS_ENABLE_SCHED
        if (sh->sched) {
            int32_t next = mcs_sched_poll(sh->sched);
            if (next >= 0 && (uint32_t)next < wait) wait = (uint32_t)next;
        }
#endif
        if (!mcs_shell_step(sh, wait)) break;
    }
}
#endif
