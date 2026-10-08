/* MicroCS - standalone runtime: boot sequence and script management protocol. */
#include "mcs_shell.h"
#if MCS_ENABLE_SHELL
#include "mcs_vfs.h"
#if MCS_ENABLE_SCHED
#include "mcs_sched.h"
#endif
#if MCS_ENABLE_HAL
#include "mcs_hal.h"
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
#if MCS_SHELL_REPL_MAX > 0
    if (sh->repl) { mcs_shell_set_repl(sh, true); return; }
#endif
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


/* ------------------------------------------------------------ REPL helpers */
bool mcs_repl_complete(const char* s, size_t len) {
    int depth = 0;
    enum { CODE, STR, CHR, VERB, LINE, BLOCK } st = CODE;
    for (size_t i = 0; i < len; i++) {
        char c = s[i], n = i + 1 < len ? s[i + 1] : 0;
        switch (st) {
        case STR: if (c == '\\' && n) i++; else if (c == '"') st = CODE; break;
        case CHR: if (c == '\\' && n) i++; else if (c == '\'') st = CODE; break;
        case VERB: if (c == '"' && n == '"') i++; else if (c == '"') st = CODE; break;
        case LINE: if (c == '\n') st = CODE; break;
        case BLOCK: if (c == '*' && n == '/') { st = CODE; i++; } break;
        case CODE:
            if (c == '/' && n == '/') { st = LINE; i++; }
            else if (c == '/' && n == '*') { st = BLOCK; i++; }
            else if (c == '@' && n == '"') { st = VERB; i++; }
            else if (c == '$' && n == '@' && i + 2 < len && s[i + 2] == '"') { st = VERB; i += 2; }
            else if (c == '"') st = STR;
            else if (c == '\'') st = CHR;
            else if (c == '{' || c == '(' || c == '[') depth++;
            else if (c == '}' || c == ')' || c == ']') depth--;
            break;
        }
    }
    return depth <= 0 && (st == CODE || st == LINE);
}

static bool ident_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
/* statement keywords / declarations that must not be wrapped in Console.WriteLine */
static bool is_statement(const char* s, size_t n) {
    static const char* const kw[] = { "var", "const", "if", "for", "foreach", "while", "do", "switch", "try",
        "class", "struct", "interface", "enum", "static", "using", "return", "throw", "break", "continue",
        "public", "private", "abstract", "sealed", "void", "record", "delegate", "event", "readonly", NULL };
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    size_t w = i;
    while (w < n && ident_char(s[w])) w++;
    for (int k = 0; kw[k]; k++) if (strlen(kw[k]) == w - i && !strncmp(s + i, kw[k], w - i)) return true;
    if (w == i) return false;
    /* `Type name ...` / `Type<T> name` / `Type[] name` / `Type? name`: a declaration */
    size_t j = w;
    if (j < n && s[j] == '<') { int d = 0; for (; j < n; j++) { if (s[j] == '<') d++; else if (s[j] == '>' && --d == 0) { j++; break; } } }
    while (j < n && (s[j] == '[' || s[j] == ']' || s[j] == '?')) j++;
    size_t sp = j;
    while (j < n && s[j] == ' ') j++;
    if (j == sp && j == w) return false;
    size_t k = j;
    while (k < n && ident_char(s[k])) k++;
    if (k == j) return false;
    while (k < n && s[k] == ' ') k++;
    return k >= n || s[k] == '=' || s[k] == ';' || s[k] == '(' || s[k] == '{' || s[k] == ',';
}

/* end of the significant code: trailing whitespace and comments do not count */
static size_t code_end(const char* s, size_t len) {
    enum { CODE, STR, CHR, VERB, LINE, BLOCK } st = CODE;
    size_t end = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i], n = i + 1 < len ? s[i + 1] : 0;
        switch (st) {
        case STR: if (c == '\\' && n) i++; else if (c == '"') st = CODE; end = i + 1; break;
        case CHR: if (c == '\\' && n) i++; else if (c == '\'') st = CODE; end = i + 1; break;
        case VERB: if (c == '"' && n == '"') i++; else if (c == '"') st = CODE; end = i + 1; break;
        case LINE: if (c == '\n') st = CODE; break;
        case BLOCK: if (c == '*' && n == '/') { st = CODE; i++; } break;
        case CODE:
            if (c == '/' && n == '/') { st = LINE; i++; }
            else if (c == '/' && n == '*') { st = BLOCK; i++; }
            else {
                if (c == '"' || (c == '@' && n == '"')) st = c == '@' ? (i++, VERB) : STR;
                else if (c == '\'') st = CHR;
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') end = i + 1;
            }
            break;
        }
    }
    return end;
}

int mcs_repl_prepare(const char* s, size_t len, char* out, size_t cap) {
    size_t e = code_end(s, len);
    size_t b = 0;
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) b++;
    int n;
    if (b >= e) n = snprintf(out, cap, "%s", "");
    else if (s[e - 1] == ';') n = snprintf(out, cap, "%.*s", (int)(len - b), s + b);
    /* statements and blocks get a ';' (a stray ';' after a block is legal C#) */
    else if (s[b] == '{' || is_statement(s + b, e - b)) n = snprintf(out, cap, "%.*s\n;", (int)(len - b), s + b);
    else n = snprintf(out, cap, "Console.WriteLine(%.*s\n);", (int)(len - b), s + b);
    return n < 0 || (size_t)n >= cap ? -1 : n;
}

#if MCS_SHELL_REPL_MAX > 0
static void prompt(mcs_shell_t* sh) {
    if (sh->paste) return;
    out(sh, sh->code_len ? "... " : "> ");
}
static void repl_banner(mcs_shell_t* sh) {
    outf(sh, "MicroCS %s C# REPL. .help for commands, Ctrl-E paste mode, Ctrl-A machine mode.\n", MCS_VERSION_STRING);
}
static void repl_exec(mcs_shell_t* sh, const char* src, size_t len, bool raw) {
#if MCS_ENABLE_COMPILER
    static char prog[MCS_SHELL_REPL_MAX + 32];
    int n = raw ? (int)len : mcs_repl_prepare(src, len, prog, sizeof prog);
    if (n < 0) { out(sh, "error: input too long\n"); return; }
    if (n == 0) return;
    if (raw) { memcpy(prog, src, len); prog[len] = 0; }
    mcs_exec_source(sh->vm, "<repl>", prog);
#else
    (void)src; (void)len; (void)raw;
    out(sh, "error: no compiler in this build - upload .mcsb images instead\n");
#endif
}
#endif

/* df [path]: size of every mount (or of the one holding `path`) */
static int cmd_df(mcs_shell_t* sh, const char* path) {
    int e = 0;
    for (int i = 0; i < sh->vfs->count; i++) {
        mcs_vfs_statfs_t st;
        const char* p = path && *path ? path : sh->vfs->mounts[i].prefix;
        e = mcs_vfs_statfs(sh->vfs, p, &st);
        if (e == MCS_VFS_EINVAL) outf(sh, "%s ? size unknown\n", st.mount ? st.mount : p);
        else if (e) return e;
        else outf(sh, "%s %s %lu KB total, %lu KB used, %lu KB free\n", st.mount, st.format ? st.format : "?",
                  (unsigned long)((st.total + 512) / 1024), (unsigned long)((st.total - st.free + 512) / 1024), (unsigned long)((st.free + 512) / 1024));
        if (path && *path) break;
    }
    return 0;
}

static void cmd_help(mcs_shell_t* sh) {
    out(sh, "ls [dir] | cat <f> | put <f> <len> | get <f> | rm <f> | mkdir <d> | mv <a> <b> | df [dir]\n"
            "run <f> | exec <code> | jobs | every <t> <f> | after <t> <f> | cancel <id>|all|scripts|files\n"
            "mem | info | repl | quit   (Ctrl-C stops a running script)\n");
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
    bool need_fs = strcmp(c, "help") && strcmp(c, "info") && strcmp(c, "mem") && strcmp(c, "quit") && strcmp(c, "exit") && strcmp(c, "jobs") && strcmp(c, "cancel") && strcmp(c, "repl");
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
    } else if (!strcmp(c, "df")) {
        if ((e = cmd_df(sh, argv[1]))) err(sh, mcs_vfs_strerror(e)); else ok(sh);
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
        if (!sh->sched || argc < 2) { err(sh, "usage: cancel <id>|all|scripts|files"); return; }
        int which = !strcmp(argv[1], "all") ? MCS_SCHED_ALL : !strcmp(argv[1], "scripts") ? MCS_SCHED_DELEGATES
                  : !strcmp(argv[1], "files") ? MCS_SCHED_FILES : -1;
        if (which >= 0) { outf(sh, "cancelled %d\n", mcs_sched_cancel_all(sh->sched, which)); ok(sh); }
        else if (mcs_sched_cancel(sh->sched, atoi(argv[1]))) ok(sh); else err(sh, "no such job");
    }
#endif
    else if (!strcmp(c, "repl")) {
#if MCS_SHELL_REPL_MAX > 0
        ok(sh); mcs_shell_set_repl(sh, true);
#else
        err(sh, "REPL not built (MCS_SHELL_REPL_MAX = 0)");
#endif
    }
    else if (!strcmp(c, "quit") || !strcmp(c, "exit")) { sh->quit = true; ok(sh); }
    else { outf(sh, EOT "ERR unknown command '%s'\n", c); }
}

#if MCS_SHELL_REPL_MAX > 0
static void repl_command(mcs_shell_t* sh, char* line) {
    char* a = line + 1;
    while (*a && *a != ' ') a++;
    if (*a) *a++ = 0;
    while (*a == ' ') a++;
    const char* c = line + 1;
    int e;
    if (!strcmp(c, "help")) {
        out(sh, "C# statements and expressions run as you type them; expressions are printed.\n"
                ".ls [dir]  .cat <f>  .run <f>  .rm <f>  .df  .mem  .info  .jobs  .cancel <id>|all  .clear  .exit (machine mode)\n"
                "Jobs started with Scheduler.Every/After keep running after the script ends (and after\n"
                "its file is deleted) until .cancel, Scheduler.CancelAll() or a reset.\n"
                "Ctrl-C clears / stops a script, Ctrl-E paste mode (Ctrl-D runs), Ctrl-A machine mode\n");
    } else if (!strcmp(c, "clear")) sh->code_len = 0;
    else if (!strcmp(c, "exit") || !strcmp(c, "shell")) { mcs_shell_set_repl(sh, false); ok(sh); return; }
    else if ((!strcmp(c, "ls") || !strcmp(c, "cat") || !strcmp(c, "run") || !strcmp(c, "rm") || !strcmp(c, "df")) && !sh->vfs) out(sh, "error: no filesystem\n");
    else if (!strcmp(c, "df")) { if ((e = cmd_df(sh, a))) outf(sh, "error: %s\n", mcs_vfs_strerror(e)); }
    else if (!strcmp(c, "mem") || !strcmp(c, "info") || !strcmp(c, "jobs") || !strcmp(c, "cancel") || !strcmp(c, "ls") || !strcmp(c, "cat") || !strcmp(c, "run") || !strcmp(c, "rm")) {
        if (!strcmp(c, "run")) { if (*a) mcs_exec_file(sh->vm, sh->vfs, a); else out(sh, "usage: .run <file>\n"); }
        else if (!strcmp(c, "ls")) { if ((e = mcs_vfs_list(sh->vfs, *a ? a : "/", ls_cb, sh))) outf(sh, "error: %s\n", mcs_vfs_strerror(e)); }
        else if (!strcmp(c, "cat")) {
            char* d; size_t n;
            if ((e = mcs_vfs_read_file(sh->vfs, a, &d, &n))) outf(sh, "error: %s\n", mcs_vfs_strerror(e));
            else { sh->t.write(sh->t.ud, d, n); if (n && d[n - 1] != '\n') out(sh, "\n"); mcs_vfs_free(sh->vfs, d, n); }
        } else if (!strcmp(c, "rm")) { if ((e = mcs_vfs_remove(sh->vfs, a))) outf(sh, "error: %s\n", mcs_vfs_strerror(e)); }
        else if (!strcmp(c, "mem")) {
            mcs_mem_stats_t st; mcs_mem_stats(sh->vm, &st);
            outf(sh, "heap %u bytes in use, peak %u, %u objects, %u collections\n",
                 (unsigned)st.bytes_in_use, (unsigned)st.peak_bytes, (unsigned)st.objects, (unsigned)st.collections);
        } else if (!strcmp(c, "info")) outf(sh, "MicroCS %s features=0x%04x value=%u bytes\n", MCS_VERSION_STRING, (unsigned)mcs_features(), (unsigned)sizeof(mcs_value_t));
#if MCS_ENABLE_SCHED
        else if (!strcmp(c, "cancel") && sh->sched) {
            if (!*a) out(sh, "usage: .cancel <id>|all|scripts|files\n");
            else if (!strcmp(a, "all") || !strcmp(a, "scripts") || !strcmp(a, "files"))
                outf(sh, "cancelled %d job(s)\n", mcs_sched_cancel_all(sh->sched, a[0] == 'a' ? MCS_SCHED_ALL : a[0] == 's' ? MCS_SCHED_DELEGATES : MCS_SCHED_FILES));
            else if (!mcs_sched_cancel(sh->sched, atoi(a))) out(sh, "no such job\n");
        }
        else if (!strcmp(c, "jobs") && sh->sched) {
            for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) {
                mcs_job_t* j = &sh->sched->jobs[i];
                if (j->state == MCS_JOB_FREE) continue;
                outf(sh, "%d %s %s %u ms runs=%u %s\n", j->id, job_state(j->state), j->periodic ? "every" : "once",
                     (unsigned)j->period_ms, (unsigned)j->runs, j->is_file ? j->path : "<delegate>");
            }
        }
#endif
    } else outf(sh, "unknown command '.%s' (.help)\n", c);
    prompt(sh);
}

static void repl_line(mcs_shell_t* sh, char* line, size_t len) {
    if (sh->paste) {                      /* collect verbatim until Ctrl-D */
        if (sh->code_len + len + 1 >= sizeof sh->code) { out(sh, "error: paste too long\n"); sh->paste = false; sh->code_len = 0; prompt(sh); return; }
        memcpy(sh->code + sh->code_len, line, len); sh->code_len += len;
        sh->code[sh->code_len++] = '\n';
        out(sh, "=== ");
        return;
    }
    size_t i = 0;
    while (i < len && line[i] == ' ') i++;
    if (!sh->code_len && line[i] == '.' && line[i + 1] >= 'a' && line[i + 1] <= 'z') { repl_command(sh, line + i); return; }
    if (sh->code_len + len + 2 >= sizeof sh->code) { out(sh, "error: input too long, cleared\n"); sh->code_len = 0; prompt(sh); return; }
    memcpy(sh->code + sh->code_len, line, len); sh->code_len += len;
    sh->code[sh->code_len++] = '\n';
    sh->code[sh->code_len] = 0;
    if (!mcs_repl_complete(sh->code, sh->code_len)) { prompt(sh); return; }
    size_t n = sh->code_len;
    sh->code_len = 0;
    repl_exec(sh, sh->code, n, false);
    prompt(sh);
}
#endif

void mcs_shell_set_repl(mcs_shell_t* sh, bool on) {
#if MCS_SHELL_REPL_MAX > 0
    sh->repl = on; sh->paste = false; sh->code_len = 0; sh->len = 0;
    if (on) { repl_banner(sh); prompt(sh); }
#else
    (void)sh; (void)on;
#endif
}

static void feed(mcs_shell_t* sh, uint8_t b) {
    if (b == 0x01) {                       /* Ctrl-A: machine protocol, from any state */
        sh->len = 0; sh->overflow = false;
#if MCS_SHELL_REPL_MAX > 0
        if (sh->repl) { sh->repl = false; sh->paste = false; sh->code_len = 0; out(sh, "\n"); }
#endif
        ok(sh);
        return;
    }
#if MCS_SHELL_REPL_MAX > 0
    if (sh->repl) {
        bool cr = b == '\r';
        if (b == '\n' && sh->last_cr) { sh->last_cr = false; return; }   /* CR LF from terminals */
        sh->last_cr = cr;
        if (cr) b = '\n';
        if (b == '\n') {
            if (sh->echo) out(sh, "\r\n");
            sh->line[sh->len] = 0;
            size_t n = sh->len;
            sh->len = 0;
            if (sh->overflow) { sh->overflow = false; out(sh, "error: line too long\n"); prompt(sh); return; }
            repl_line(sh, sh->line, n);
            return;
        }
        if (b == 0x03) {                   /* Ctrl-C: cancel input / paste */
            sh->len = 0; sh->code_len = 0; sh->paste = false; sh->overflow = false;
            out(sh, sh->echo ? "^C\r\n" : "\n");
            prompt(sh);
            return;
        }
        if (b == 0x05) {                   /* Ctrl-E: paste mode */
            sh->paste = true; sh->code_len = 0; sh->len = 0;
            out(sh, "\npaste mode; Ctrl-D runs, Ctrl-C cancels\n=== ");
            return;
        }
        if (b == 0x04) {                   /* Ctrl-D: run paste buffer */
            if (sh->paste) {
                if (sh->len) repl_line(sh, sh->line, sh->len);
                sh->len = 0;
                sh->paste = false;
                out(sh, "\n");
                size_t n = sh->code_len; sh->code_len = 0;
                repl_exec(sh, sh->code, n, true);
                prompt(sh);
            }
            return;
        }
        if (b == 0x08 || b == 0x7F) {      /* backspace */
            if (sh->len) { sh->len--; if (sh->echo) out(sh, "\b \b"); }
            return;
        }
        if (b < 0x20 && b != '\t') return; /* other control bytes / escape sequences start */
        if (sh->len + 1 < sizeof sh->line) {
            sh->line[sh->len++] = (char)b;
            if (sh->echo) sh->t.write(sh->t.ud, (const char*)&b, 1);
        } else sh->overflow = true;
        return;
    }
#endif
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
#if MCS_ENABLE_HAL
    if (mcs_hal_get(sh->vm)) mcs_hal_poll(sh->vm);
#endif
    return !sh->quit;
}

void mcs_shell_run(mcs_shell_t* sh) {
    for (;;) {
        uint32_t wait = 100;
#if MCS_ENABLE_HAL
        if (mcs_hal_get(sh->vm)) wait = MCS_SLEEP_SLICE_MS;   /* keep interrupt callbacks responsive */
#endif
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
