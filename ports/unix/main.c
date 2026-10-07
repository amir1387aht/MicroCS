/* MicroCS command line tool for desktop hosts (Linux/macOS/Windows-MinGW).
 *
 *   mcs script.cs            run a C# file
 *   mcs app.mcsb             run a precompiled bytecode image
 *   mcs -c script.cs -o out.mcsb [-s]   compile to bytecode (-s strips line info)
 *   mcs -C script.cs -o out.h [-n name] compile to a C header (const array for flash)
 *   mcs -d script.cs         disassemble
 *   mcs -e "code"            run code from the command line
 *   mcs                      interactive REPL
 *   mcs --shell              standalone runtime / script manager on stdin+stdout
 *   options: --heap N (bytes, hard limit), --stack N (slots), --stats, see usage() */
#include "mcs.h"
#include "mcs_vfs.h"
#include "mcs_hal.h"
#include "mcs_sched.h"
#include "mcs_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static volatile int g_interrupted;
static mcs_vm_t* g_vm;
#if MCS_ENABLE_SHELL
static bool g_shell;
#endif

static void on_sigint(int sig) { (void)sig; g_interrupted = 1; if (g_vm) mcs_request_abort(g_vm); }

static int hook(mcs_vm_t* vm, void* ud) {
    (void)ud;
#if MCS_ENABLE_SHELL
    if (g_shell && mcs_shell_poll_break(vm)) return 1;
#else
    (void)vm;
#endif
    return g_interrupted ? 1 : 0;
}

static uint32_t ticks(void* ud) {
    (void)ud;
#if defined(_WIN32)
    return (uint32_t)GetTickCount();
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}
static void delay(void* ud, uint32_t ms) {
    (void)ud;
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static char* read_file(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mcs: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); fprintf(stderr, "mcs: read error\n"); return NULL; }
    buf[n] = 0;
    fclose(f);
    if (len) *len = (size_t)n;
    return buf;
}

static bool ends_with(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static int result_code(mcs_result_t r) { return r == MCS_OK ? 0 : r == MCS_ERR_COMPILE ? 2 : r == MCS_ERR_ABORTED ? 130 : 1; }

/* ------------------------------------------------------------ REPL */
static __attribute__((unused)) bool balanced(const char* s) {
    int depth = 0; bool str = false, chr = false;
    for (const char* p = s; *p; p++) {
        if (str) { if (*p == '\\' && p[1]) p++; else if (*p == '"') str = false; continue; }
        if (chr) { if (*p == '\\' && p[1]) p++; else if (*p == '\'') chr = false; continue; }
        if (*p == '"') str = true; else if (*p == '\'') chr = true;
        else if (*p == '{' || *p == '(' || *p == '[') depth++;
        else if (*p == '}' || *p == ')' || *p == ']') depth--;
    }
    return depth <= 0;
}

/* Feature stubs so the CLI builds with any mcs_config.h combination */
static __attribute__((unused)) mcs_result_t mcs_cli_missing(mcs_vm_t* vm, const char* what) {
    (void)vm; fprintf(stderr, "mcs: built without %s\n", what); return MCS_ERR_COMPILE;
}
#if !MCS_ENABLE_COMPILER
#define mcs_exec_source(vm, n, s) ((void)(n), (void)(s), mcs_cli_missing(vm, "the compiler (MCS_ENABLE_COMPILER)"))
#endif
#if !(MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE)
#ifndef MCS_IMAGE_STRIP
#define MCS_IMAGE_STRIP  1u
#define MCS_IMAGE_NO_OPT 2u
#endif
static mcs_result_t mcs_compile_image_ex(mcs_vm_t* vm, const char* n, const char* s, unsigned fl, uint8_t** img, size_t* len) {
    (void)n; (void)s; (void)fl; *img = NULL; *len = 0;
    return mcs_cli_missing(vm, "image output (MCS_ENABLE_BYTECODE_SAVE)");
}
#define mcs_free_image(vm, img) ((void)(vm), (void)(img))
#endif
#if !(MCS_ENABLE_DISASM && MCS_ENABLE_COMPILER)
#define mcs_disassemble_source(vm, n, s) ((void)(n), (void)(s), mcs_cli_missing(vm, "the disassembler (MCS_ENABLE_DISASM)"))
#endif
#if !MCS_ENABLE_BYTECODE_LOAD
#define mcs_exec_image(vm, d, n) ((void)(d), (void)(n), mcs_cli_missing(vm, "image loading (MCS_ENABLE_BYTECODE_LOAD)"))
#define mcs_exec_image_xip(vm, d, n) mcs_exec_image(vm, d, n)
#endif

static void repl(mcs_vm_t* vm) {
    printf("MicroCS %s - C# for microcontrollers. Type 'exit' to quit.\n", MCS_VERSION_STRING);
    char line[1024];
    size_t cap = 4096, len = 0;
    char* src = (char*)malloc(cap);
    for (;;) {
        fputs(len ? "... " : "> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        if (!len && (!strcmp(line, "exit\n") || !strcmp(line, "quit\n"))) break;
        size_t n = strlen(line);
        if (len + n + 2 > cap) { cap = (len + n) * 2 + 2; src = (char*)realloc(src, cap); }
        memcpy(src + len, line, n + 1);
        len += n;
#if MCS_ENABLE_SHELL
        if (!mcs_repl_complete(src, len)) continue;
        char* tmp = (char*)malloc(len + 64);
        int pn = mcs_repl_prepare(src, len, tmp, len + 64);
        char* code = pn < 0 ? src : tmp;
#else
        if (!balanced(src)) continue;
        /* bare expression? print its value */
        size_t e = len;
        while (e && (src[e - 1] == '\n' || src[e - 1] == ' ' || src[e - 1] == '\r')) e--;
        char* code = src;
        char* tmp = NULL;
        if (e && src[e - 1] != ';' && src[e - 1] != '}') {
            tmp = (char*)malloc(e + 64);
            snprintf(tmp, e + 64, "Console.WriteLine(%.*s);", (int)e, src);
            code = tmp;
        }
#endif
        g_interrupted = 0;
        mcs_exec_source(vm, "<repl>", code);
        free(tmp);
        len = 0; src[0] = 0;
    }
    free(src);
    putchar('\n');
}

/* ------------------------------------------------------------ C header output */
static int write_header(const char* out, const char* name, const uint8_t* img, size_t n) {
    FILE* f = out ? fopen(out, "w") : stdout;
    if (!f) { fprintf(stderr, "mcs: cannot write '%s'\n", out); return 1; }
    fprintf(f, "/* Generated by mcs %s - MicroCS bytecode image */\n#include <stdint.h>\n#include <stddef.h>\n", MCS_VERSION_STRING);
    fprintf(f, "static const uint8_t %s[%zu] __attribute__((aligned(4))) = {\n", name, n);
    for (size_t i = 0; i < n; i++) fprintf(f, "%s0x%02x,%s", i % 16 == 0 ? "    " : "", img[i], i % 16 == 15 || i + 1 == n ? "\n" : " ");
    fprintf(f, "};\nstatic const size_t %s_len = %zu;\n", name, n);
    if (out) fclose(f);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "usage: mcs [options] [file.cs | file.mcsb] [args]\n"
        "  -c FILE      compile to bytecode image (.mcsb)\n"
        "  -C FILE      compile to C header with a const byte array\n"
        "  -o OUT       output path for -c / -C\n"
        "  -n NAME      array name for -C (default: mcs_app)\n"
        "  -s           strip debug line info from images\n"
        "  -O0          do not optimize the image (for VMs built with MCS_ENABLE_SUPEROPS=0)\n"
        "  -d FILE      disassemble\n"
        "  -e CODE      execute code string\n"
        "  --heap N     heap limit in bytes (emulate a small MCU)\n"
        "  --stack N    value stack slots\n"
        "  --stats      print memory statistics at exit\n"
        "  --xip        run .mcsb images in place (no code copy, see mcs_exec_image_xip)\n"
        "  --fs DIR     mount host directory DIR at / for File/Directory (default: .)\n"
        "  --ramfs N    mount an N-byte RAM filesystem at / instead\n"
        "  --no-fs      no filesystem (File/Directory unavailable)\n"
        "  --ro         mount the filesystem read-only\n"
        "  --sim        simulated board with every peripheral (--sim-log traces it,\n"
        "               --sim-virtual uses a deterministic virtual clock for timers)\n"
        "  --time-limit MS, --step-limit N   abort a run that exceeds the budget\n"
        "  --run-for MS stop the job scheduler after MS (default: until no jobs or Ctrl-C)\n"
        "  --shell      standalone runtime: boot scripts, jobs, script upload protocol\n"
        "  --repl       like --shell but starts at the interactive C# prompt (the device REPL)\n"
        "  --no-boot    with --shell/--repl: do not run /boot.cs, /jobs.cfg, /main.cs\n"
        "  --echo       with --shell/--repl: echo typed input like a device on a raw UART\n"
        "  --features   print compile-time features and exit\n"
        "  -v           version\n");
}

static void print_features(void) {
    static const struct { uint32_t bit; const char* name; } f[] = {
        { MCS_FEAT_COMPILER, "compiler" }, { MCS_FEAT_IMAGE_LOAD, "image-load" }, { MCS_FEAT_IMAGE_SAVE, "image-save" },
        { MCS_FEAT_FLOAT, "float" }, { MCS_FEAT_DOUBLE, "double" }, { MCS_FEAT_INT64, "int64" },
        { MCS_FEAT_LINES, "lines" }, { MCS_FEAT_DISASM, "disasm" }, { MCS_FEAT_LIST, "list" }, { MCS_FEAT_DICT, "dict" },
        { MCS_FEAT_FS, "fs" }, { MCS_FEAT_HAL, "hal" }, { MCS_FEAT_SCHED, "sched" }, { MCS_FEAT_SHELL, "shell" },
    };
    uint32_t m = mcs_features();
    printf("MicroCS %s, %d-bit values:", MCS_VERSION_STRING, (int)(sizeof(mcs_value_t) * 8));
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) if (m & f[i].bit) printf(" %s", f[i].name);
    putchar('\n');
}

#if MCS_ENABLE_SHELL
#if defined(_WIN32)
#error "the --shell transport uses poll(); provide a Windows transport"
#endif
#include <poll.h>
static int stdio_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ud;
    fflush(stdout);
    struct pollfd p = { 0, POLLIN, 0 };
    int r = poll(&p, 1, (int)timeout_ms);
    if (r <= 0) return 0;
    ssize_t k = read(0, buf, n);
    return k <= 0 ? -1 : (int)k;
}
static void stdio_write(void* ud, const char* s, size_t n) { (void)ud; fwrite(s, 1, n, stdout); }
static mcs_transport_t stdio_transport(void) { mcs_transport_t t = { stdio_read, stdio_write, NULL }; return t; }
#endif

#if MCS_ENABLE_FS
static mcs_vfs_t g_vfs;
static mcs_posixfs_t g_posixfs;
static mcs_ramfs_t g_ramfs;
#endif
#if MCS_ENABLE_HAL
static mcs_hal_t g_hal;
static mcs_hal_sim_t g_sim;
static void sim_log(void* ud, const char* s, size_t n) { (void)ud; fwrite(s, 1, n, stderr); }
static uint32_t sim_clock_us(void* ud) {
    (void)ud;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}
#endif
#if MCS_ENABLE_SCHED
static mcs_sched_t g_sched;
#endif

int main(int argc, char** argv) {
    const char *compile = NULL, *compile_h = NULL, *out = NULL, *disasm = NULL, *code = NULL, *file = NULL, *name = "mcs_app";
    bool strip = false, stats = false, xip = false, no_opt = false;
    char* xip_data = NULL;
    size_t heap = 0; uint32_t stack = 0;
    const char* fs_dir = ".";
    size_t ramfs = 0;
    bool no_fs = false, ro = false, sim = false, sim_log_on = false, sim_virtual = false, shell = false, repl_mode = false, boot = true, echo = false;
    mcs_limits_t limits = { 0, 0 };
    uint32_t run_for = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "-c") && i + 1 < argc) compile = argv[++i];
        else if (!strcmp(a, "-C") && i + 1 < argc) compile_h = argv[++i];
        else if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "-n") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(a, "-d") && i + 1 < argc) disasm = argv[++i];
        else if (!strcmp(a, "-e") && i + 1 < argc) code = argv[++i];
        else if (!strcmp(a, "-s")) strip = true;
        else if (!strcmp(a, "-O0")) no_opt = true;
        else if (!strcmp(a, "--stats")) stats = true;
        else if (!strcmp(a, "--xip")) xip = true;
        else if (!strcmp(a, "--heap") && i + 1 < argc) heap = (size_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--stack") && i + 1 < argc) stack = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--fs") && i + 1 < argc) fs_dir = argv[++i];
        else if (!strcmp(a, "--ramfs") && i + 1 < argc) ramfs = (size_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--no-fs")) no_fs = true;
        else if (!strcmp(a, "--ro")) ro = true;
        else if (!strcmp(a, "--sim")) sim = true;
        else if (!strcmp(a, "--sim-log")) sim = sim_log_on = true;
        else if (!strcmp(a, "--sim-virtual")) sim = sim_virtual = true;
        else if (!strcmp(a, "--time-limit") && i + 1 < argc) limits.time_ms = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--step-limit") && i + 1 < argc) limits.steps = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--run-for") && i + 1 < argc) run_for = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--shell")) shell = true;
        else if (!strcmp(a, "--repl")) shell = repl_mode = true;
        else if (!strcmp(a, "--no-boot")) boot = false;
        else if (!strcmp(a, "--echo")) echo = true;
        else if (!strcmp(a, "--features")) { print_features(); return 0; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("MicroCS %s\n", MCS_VERSION_STRING); return 0; }
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-' && a[1]) { usage(); return 64; }
        else { file = a; break; }
    }

    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.hook_fn = hook;
    cfg.ticks_fn = ticks;
    cfg.delay_fn = delay;
    cfg.heap_limit = heap;
    if (stack) cfg.stack_slots = stack;
#if MCS_ENABLE_SHELL
    if (shell) cfg.error_fn = stdio_write;   /* everything goes over the transport */
#endif
    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) { fprintf(stderr, "mcs: cannot create VM\n"); return 1; }
    g_vm = vm;
    signal(SIGINT, on_sigint);
    mcs_set_limits(vm, &limits);
#if MCS_ENABLE_FS
    mcs_vfs_t* vfs = NULL;
    if (!no_fs) {
        mcs_vfs_init(&g_vfs);
        int e;
        if (ramfs) { mcs_ramfs_init(&g_ramfs, ramfs, NULL, NULL); e = mcs_vfs_mount(&g_vfs, "/", &mcs_ramfs_ops, &g_ramfs, ro ? MCS_VFS_RDONLY : 0); }
        else if ((e = mcs_posixfs_init(&g_posixfs, fs_dir)) == 0) e = mcs_vfs_mount(&g_vfs, "/", &mcs_posixfs_ops, &g_posixfs, ro ? MCS_VFS_RDONLY : 0);
        if (e) { fprintf(stderr, "mcs: cannot mount '%s': %s\n", ramfs ? "ramfs" : fs_dir, mcs_vfs_strerror(e)); return 1; }
        vfs = &g_vfs;
        mcs_fs_open_lib(vm, vfs);
    }
#else
    (void)fs_dir; (void)ramfs; (void)no_fs; (void)ro;
#endif
#if MCS_ENABLE_HAL
    if (sim) {
        g_sim.log = sim_log_on ? sim_log : NULL;
        g_sim.clock_us = sim_virtual ? NULL : sim_clock_us;   /* virtual: 1 ms per event poll, deterministic */
        mcs_hal_sim_init(&g_hal, &g_sim);
        mcs_hal_open_lib(vm, &g_hal);
    }
#else
    (void)sim; (void)sim_log_on; (void)sim_virtual;
#endif
#if MCS_ENABLE_SCHED
#if MCS_ENABLE_FS
    mcs_sched_init(&g_sched, vm, vfs, ticks, NULL);
#else
    mcs_sched_init(&g_sched, vm, NULL, ticks, NULL);
#endif
    mcs_sched_open_lib(vm, &g_sched);
#else
    (void)run_for;
#endif

    int rc = 0;
    if (compile || compile_h) {
        const char* in = compile ? compile : compile_h;
        char* src = read_file(in, NULL);
        if (!src) return 1;
        uint8_t* img; size_t n;
        mcs_result_t r = mcs_compile_image_ex(vm, in, src, (strip ? MCS_IMAGE_STRIP : 0u) | (no_opt ? MCS_IMAGE_NO_OPT : 0u), &img, &n);
        free(src);
        if (r != MCS_OK) { fprintf(stderr, "%s\n", mcs_last_error(vm)); mcs_free(vm); return 2; }
        if (compile_h) rc = write_header(out, name, img, n);
        else {
            char def[512];
            if (!out) { snprintf(def, sizeof def, "%s", in); char* dot = strrchr(def, '.'); if (dot) *dot = 0; strncat(def, ".mcsb", sizeof def - strlen(def) - 1); out = def; }
            FILE* f = fopen(out, "wb");
            if (!f || fwrite(img, 1, n, f) != n) { fprintf(stderr, "mcs: cannot write '%s'\n", out); rc = 1; }
            if (f) fclose(f);
            if (!rc) fprintf(stderr, "wrote %s (%zu bytes)\n", out, n);
        }
        mcs_free_image(vm, img);
    } else if (disasm) {
        size_t dn = 0;
        char* src = read_file(disasm, &dn);
        if (!src) return 1;
        if (dn >= 4 && !memcmp(src, "MCSB", 4)) {
#if MCS_ENABLE_DISASM && MCS_ENABLE_BYTECODE_LOAD
            rc = result_code(mcs_disassemble_image(vm, (const uint8_t*)src, dn));
#else
            fprintf(stderr, "mcs: image disassembly not built in\n"); rc = 1;
#endif
        } else rc = result_code(mcs_disassemble_source(vm, disasm, src));
        if (rc) fprintf(stderr, "%s\n", mcs_last_error(vm));
        free(src);
    } else if (code) {
        rc = result_code(mcs_exec_source(vm, "<cmdline>", code));
    } else if (file) {
        size_t n;
        char* data = read_file(file, &n);
        if (!data) return 1;
        if (ends_with(file, ".mcsb") || (n >= 4 && !memcmp(data, "MCSB", 4))) {
            if (xip) { rc = result_code(mcs_exec_image_xip(vm, (const uint8_t*)data, n)); xip_data = data; data = NULL; }
            else rc = result_code(mcs_exec_image(vm, (const uint8_t*)data, n));
        } else
            rc = result_code(mcs_exec_source(vm, file, data));
        free(data);
    } else if (shell) {
#if MCS_ENABLE_SHELL
        static mcs_shell_t sh;
        g_shell = true;
#if MCS_ENABLE_SCHED
        mcs_shell_init(&sh, vm, vfs, &g_sched, stdio_transport());
#else
        mcs_shell_init(&sh, vm, vfs, NULL, stdio_transport());
#endif
#if MCS_SHELL_REPL_MAX > 0
        sh.repl = repl_mode;
#else
        (void)repl_mode;
#endif
        sh.echo = echo;
        mcs_shell_boot(&sh, boot);
        mcs_shell_run(&sh);
#else
        (void)boot;
        fprintf(stderr, "mcs: built without the shell (MCS_ENABLE_SHELL)\n");
        rc = 64;
#endif
    } else {
        repl(vm);
    }
#if MCS_ENABLE_SCHED
    if (!shell && !compile && !compile_h && !disasm && mcs_sched_active(&g_sched))
        mcs_sched_run(&g_sched, delay, NULL, &g_interrupted, 50, run_for);
    mcs_sched_free(&g_sched);
#endif
    if (stats) {
        mcs_mem_stats_t st; mcs_mem_stats(vm, &st);
        fprintf(stderr, "[mem] in use: %zu bytes, peak: %zu bytes, objects: %u, collections: %u\n", st.bytes_in_use, st.peak_bytes, st.objects, st.collections);
    }
    mcs_free(vm);
    free(xip_data);   /* --xip: the image buffer must outlive the VM */
    return rc;
}
