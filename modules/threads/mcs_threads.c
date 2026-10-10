/* MicroCS - C# on OS threads: one VM per thread, Channels between them (mcs_threads.h).
 * Compiled to nothing unless an OS is chosen with MCS_OS. */
#include "mcs_threads.h"
#if MCS_ENABLE_THREADS
#include <string.h>
#include <stdio.h>
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#if MCS_ENABLE_HAL
#include "mcs_hal.h"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)

/* flags shared between the threads without the registry lock */
#if defined(__GNUC__) || defined(__clang__)
#define AT_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define AT_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
#else
#define AT_LOAD(x) (x)
#define AT_STORE(x, v) ((x) = (v))
#endif
#define TH_SLOTS (MCS_THREADS_MAX * 2)
#define TH_PATH_MAX 64
#define TH_FN_MAX 32
#define TH_ERR_MAX 112
#define TH_LINE_MAX 120

typedef struct worker {
    int id, refs;
    uint8_t state;         /* mcs_thread_state_t */
    uint8_t stop;
    volatile uint8_t ended;         /* worker_main has given `done`: reapable once refs == 0 */
    mcs_os_thread_t* th;
    mcs_os_sem_t* done;             /* given once when the thread ends (re-given by joiners) */
    char path[TH_PATH_MAX];
    char fn[TH_FN_MAX];
    const void* code; size_t code_len;
    uint32_t delay, period;
    uint16_t maxf;
    int core, prio;
    uint32_t heap_size, stack_size;
    uint8_t* args; size_t args_len; /* marshalled argument array (Thread.Run) */
    uint8_t* result; size_t result_len;
    uint32_t runs, failures;
    char error[TH_ERR_MAX];
    void* heap;
    char line[TH_LINE_MAX];         /* console output, written a line at a time */
    size_t line_len;
    bool err_bol;
} worker_t;

typedef struct { uint8_t* p; uint32_t n; } msg_t;
typedef struct channel {
    char name[24];
    unsigned cap, head, count;
    msg_t* q;
    mcs_os_mutex_t* m;
    mcs_os_sem_t* items;
    mcs_os_sem_t* space;
} channel_t;

static struct {
    bool inited;
    mcs_threads_cfg_t cfg;
    mcs_os_mutex_t *lock, *console, *fs, *heap;
    worker_t* w[MCS_THREADS_MAX * 2];  /* running threads + ended ones still referenced */
    channel_t* ch[MCS_CHANNELS_MAX];
    int next_id;
    volatile bool active;
#if MCS_ENABLE_FS
    mcs_vfs_lock_t vfs_lock;
#endif
} G;
static char g_main_tag;               /* MCS_EXT_THREADS of VMs that are not thread VMs */

#define SELF(vm) ((worker_t*)(mcs_get_ext((vm), MCS_EXT_THREADS) == &g_main_tag ? NULL : mcs_get_ext((vm), MCS_EXT_THREADS)))

/* ------------------------------------------------------------ locks */
void mcs_threads_console_lock(void) { if (G.console) mcs_os_mutex_lock(G.console); }
void mcs_threads_console_unlock(void) { if (G.console) mcs_os_mutex_unlock(G.console); }
void mcs_threads_heap_lock(void) { if (G.heap) mcs_os_mutex_lock(G.heap); }
void mcs_threads_heap_unlock(void) { if (G.heap) mcs_os_mutex_unlock(G.heap); }
bool mcs_threads_active(void) { return G.active; }
#if MCS_ENABLE_FS
static void fs_enter(void* ud) { mcs_os_mutex_lock((mcs_os_mutex_t*)ud); }
static void fs_leave(void* ud) { mcs_os_mutex_unlock((mcs_os_mutex_t*)ud); }
#endif

int mcs_threads_init(const mcs_threads_cfg_t* cfg) {
    if (!G.inited) {
        G.lock = mcs_os_mutex_new(); G.console = mcs_os_mutex_new();
        G.fs = mcs_os_mutex_new(); G.heap = mcs_os_mutex_new();
        if (!G.lock || !G.console || !G.fs || !G.heap) return -1;
        G.next_id = 1;
        G.inited = true;
    }
    G.cfg = *cfg;
#if MCS_ENABLE_FS
    G.vfs_lock.enter = fs_enter; G.vfs_lock.leave = fs_leave; G.vfs_lock.ud = G.fs;
    if (cfg->vfs) cfg->vfs->lock = &G.vfs_lock;     /* the main VM's calls are locked too */
#endif
    return 0;
}

/* ------------------------------------------------------------ value copies between VMs
 * N null, B bool, I int64, F double, C char, S string, A array, L list (u32 count + items) */
typedef struct { uint8_t* b; size_t n, cap; const char* err; } wbuf_t;
static void put(wbuf_t* w, const void* p, size_t n) {
    if (w->err) return;
    if (w->n + n > w->cap) { w->err = "message too large (MCS_CHANNEL_MSG_MAX)"; return; }
    memcpy(w->b + w->n, p, n); w->n += n;
}
static void put_u32(wbuf_t* w, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; put(w, b, 4); }
static void put_u64(wbuf_t* w, uint64_t v) { put_u32(w, (uint32_t)v); put_u32(w, (uint32_t)(v >> 32)); }
static void ser(wbuf_t* w, mcs_value_t v, int depth) {
    char t;
    switch (v.type) {
    case MCS_T_NULL: t = 'N'; put(w, &t, 1); return;
    case MCS_T_BOOL: { uint8_t b[2] = { 'B', (uint8_t)(v.as.b ? 1 : 0) }; put(w, b, 2); return; }
    case MCS_T_INT: t = 'I'; put(w, &t, 1); put_u64(w, (uint64_t)(int64_t)v.as.i); return;
    case MCS_T_CHAR: t = 'C'; put(w, &t, 1); put_u32(w, (uint32_t)v.as.i); return;
#if MCS_ENABLE_FLOAT
    case MCS_T_FLOAT: { double d = (double)v.as.f; uint64_t u; memcpy(&u, &d, 8); t = 'F'; put(w, &t, 1); put_u64(w, u); return; }
#endif
    default: break;
    }
    if (mcs_is_string(v)) {
        uint32_t n = (uint32_t)mcs_strlen(v);
        t = 'S'; put(w, &t, 1); put_u32(w, n); put(w, mcs_cstr(v), n);
        return;
    }
    int k = mcs_obj_kind(v);
    if ((k == MCS_O_ARRAY || k == MCS_O_LIST) && depth < 4) {
        uint32_t n = mcs_len(v);
        t = k == MCS_O_ARRAY ? 'A' : 'L'; put(w, &t, 1); put_u32(w, n);
        for (uint32_t i = 0; i < n && !w->err; i++) ser(w, mcs_index(v, i), depth + 1);
        return;
    }
    if (!w->err) w->err = depth >= 4 && (k == MCS_O_ARRAY || k == MCS_O_LIST) ? "arrays nested too deeply"
        : "only null, bool, numbers, char, string and arrays / lists of them can be passed between threads";
}
/* serialize into a fresh OS-heap block; NULL + raises on error */
static uint8_t* marshal(mcs_vm_t* vm, mcs_value_t v, size_t* len) {
    wbuf_t w = { NULL, 0, MCS_CHANNEL_MSG_MAX, NULL };
    w.b = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, w.cap);
    if (!w.b) return NULL;
    ser(&w, v, 0);
    uint8_t* out = NULL;
    if (w.err) mcs_raise(vm, "ArgumentException", "%s", w.err);
    else if (!(out = (uint8_t*)mcs_os_malloc(w.n ? w.n : 1))) mcs_raise(vm, "OutOfMemoryException", "no memory for a thread message");
    else { memcpy(out, w.b, w.n); *len = w.n; }
    mcs_mem_realloc(vm, w.b, w.cap, 0);
    return out;
}
typedef struct { const uint8_t* p; size_t n; bool bad; } rbuf_t;
static bool get(rbuf_t* r, void* out, size_t n) {
    if (r->bad || r->n < n) { r->bad = true; memset(out, 0, n); return false; }
    memcpy(out, r->p, n); r->p += n; r->n -= n; return true;
}
static uint32_t get_u32(rbuf_t* r) { uint8_t b[4]; get(r, b, 4); return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static uint64_t get_u64(rbuf_t* r) { uint64_t lo = get_u32(r); return lo | (uint64_t)get_u32(r) << 32; }
static mcs_value_t deser(mcs_vm_t* vm, rbuf_t* r) {
    char t = 0;
    if (!get(r, &t, 1)) return mcs_null();
    switch (t) {
    case 'B': { uint8_t b; get(r, &b, 1); return mcs_bool(b != 0); }
    case 'I': return mcs_int((mcs_int_t)(int64_t)get_u64(r));
    case 'C': return mcs_char(get_u32(r));
    case 'F': {
        uint64_t u = get_u64(r); double d; memcpy(&d, &u, 8);
#if MCS_ENABLE_FLOAT
        return mcs_float((mcs_float_t)d);
#else
        return mcs_int((mcs_int_t)d);
#endif
    }
    case 'S': {
        uint32_t n = get_u32(r);
        if (r->bad || n > r->n) { r->bad = true; return mcs_null(); }
        mcs_value_t s = mcs_string_n(vm, (const char*)r->p, n);
        r->p += n; r->n -= n;
        return s;
    }
    case 'A': case 'L': {
        uint32_t n = get_u32(r);
        if (r->bad || n > r->n) { r->bad = true; return mcs_null(); }
        mcs_value_t a = t == 'A' ? mcs_new_array(vm, n) : mcs_new_list(vm);
        mcs_push_root(vm, a);
        for (uint32_t i = 0; i < n && !r->bad; i++) {
            mcs_value_t x = deser(vm, r);
            if (t == 'A') mcs_set_index(a, i, x);
            else { mcs_push_root(vm, x); mcs_list_add(vm, a, x); mcs_pop_root(vm, 1); }
        }
        mcs_pop_root(vm, 1);
        return a;
    }
    default: return mcs_null();
    }
}
static mcs_value_t unmarshal(mcs_vm_t* vm, const uint8_t* p, size_t n) {
    rbuf_t r = { p, n, false };
    return deser(vm, &r);
}

/* ------------------------------------------------------------ registry */
static worker_t* find_locked(int id) {
    for (int i = 0; i < TH_SLOTS; i++) if (G.w[i] && G.w[i]->id == id) return G.w[i];
    return NULL;
}
static void heap_free(void* p, size_t n) {
    if (!p) return;
    if (G.cfg.heap_fn) G.cfg.heap_fn(G.cfg.heap_ud, p, n, 0); else mcs_os_free(p);
}
static void destroy(worker_t* w) {
    mcs_os_thread_free(w->th);         /* joins: worker_main has returned */
    mcs_os_sem_free(w->done);
    mcs_os_free(w->args);
    mcs_os_free(w->result);
    mcs_os_free(w);
}
/* free threads that ended and nobody references (or all ended ones with force) */
static void reap(bool force) {
    worker_t* dead[TH_SLOTS];
    int n = 0;
    mcs_os_mutex_lock(G.lock);
    for (int i = 0; i < TH_SLOTS; i++)
        if (G.w[i] && G.w[i]->ended && (G.w[i]->refs <= 0 || force)) { dead[n++] = G.w[i]; G.w[i] = NULL; }
    mcs_os_mutex_unlock(G.lock);
    for (int i = 0; i < n; i++) destroy(dead[i]);
}

/* ------------------------------------------------------------ the thread */
static void flush_line(worker_t* w) {
    if (!w->line_len) return;
    mcs_threads_console_lock();
    if (G.cfg.write) G.cfg.write(G.cfg.write_ud, w->line, w->line_len);
#if MCS_ENABLE_STDIO
    else { fwrite(w->line, 1, w->line_len, stdout); fflush(stdout); }
#endif
    mcs_threads_console_unlock();
    w->line_len = 0;
}
static void th_write(void* ud, const char* s, size_t n) {
    worker_t* w = (worker_t*)ud;
    for (size_t i = 0; i < n; i++) {
        w->line[w->line_len++] = s[i];
        if (s[i] == '\n' || w->line_len == sizeof w->line) flush_line(w);
    }
}
static void th_error(void* ud, const char* s, size_t n) {
    worker_t* w = (worker_t*)ud;
    if (AT_LOAD(w->stop)) return;        /* Stop(): no "Script aborted." noise */
    for (size_t i = 0; i < n; i++) {
        if (w->err_bol) {
            char pre[24];
            int k = snprintf(pre, sizeof pre, "[thread %d] ", w->id);
            w->err_bol = false;
            th_write(w, pre, (size_t)k);
        }
        th_write(w, &s[i], 1);
        if (s[i] == '\n') w->err_bol = true;
    }
}
static uint32_t th_ticks(void* ud) { return mcs_os_ticks(); }
static void th_delay(void* ud, uint32_t ms) { mcs_os_sleep(ms); }
static int th_hook(mcs_vm_t* vm, void* ud) { return AT_LOAD(((worker_t*)ud)->stop); }

/* sleep until `until` (ticks), in slices so Stop() is noticed */
static void sleep_until(worker_t* w, uint32_t until) {
    for (;;) {
        int32_t left = (int32_t)(until - mcs_os_ticks());
        if (left <= 0 || AT_LOAD(w->stop)) return;
        mcs_os_sleep(left > 20 ? 20 : (uint32_t)left);
    }
}

static void set_error(worker_t* w, const char* msg) {
    mcs_os_mutex_lock(G.lock);
    snprintf(w->error, sizeof w->error, "%s", msg && *msg ? msg : "failed");
    mcs_os_mutex_unlock(G.lock);
}

static mcs_result_t load(worker_t* w, mcs_vm_t* vm, void* vfs) {
    if (w->code) return mcs_exec_auto(vm, "<thread>", w->code, w->code_len);
#if MCS_ENABLE_FS
    if (vfs) return mcs_exec_file(vm, (mcs_vfs_t*)vfs, w->path);
#endif
    (void)vfs;
    return mcs_fail(vm, MCS_ERR_RUNTIME, "no filesystem for '%s'", w->path);
}

static void worker_main(void* arg) {
    worker_t* w = (worker_t*)arg;
    mcs_pool_t pool;
    mcs_pool_init(&pool, w->heap, w->heap_size);
    mcs_config_t c;
    mcs_config_default(&c);
    c.realloc_fn = mcs_pool_realloc; c.alloc_ud = &pool;
    c.heap_limit = w->heap_size > 4096 ? w->heap_size - w->heap_size / 16 : w->heap_size;
    c.write_fn = th_write; c.error_fn = th_error; c.user_data = w;
    c.ticks_fn = th_ticks; c.delay_fn = th_delay; c.hook_fn = th_hook;
    c.stack_slots = G.cfg.stack_slots ? G.cfg.stack_slots : MCS_THREAD_SLOTS;
    c.max_frames = G.cfg.max_frames ? G.cfg.max_frames : MCS_THREAD_FRAMES;
    if (G.cfg.stdlib) c.stdlib = G.cfg.stdlib;
    w->err_bol = true;
    mcs_vm_t* vm = mcs_new(&c);
    uint8_t final = MCS_THREAD_FAILED;
    if (!vm) { set_error(w, "out of memory: the thread heap is too small for a VM"); goto out; }
    mcs_set_ext(vm, MCS_EXT_THREADS, w);
    void* vfsp = NULL;
#if MCS_ENABLE_FS
    mcs_vfs_t vfs;
    if (G.cfg.vfs) {
        mcs_os_mutex_lock(G.fs);
        vfs = *G.cfg.vfs;                 /* same mounts and backends, own scratch heap */
        mcs_os_mutex_unlock(G.fs);
        vfs.realloc_fn = mcs_pool_realloc; vfs.alloc_ud = &pool;
        vfs.lock = &G.vfs_lock;
        mcs_fs_open_lib(vm, &vfs);
        vfsp = &vfs;
    }
#endif
#if MCS_ENABLE_HAL
    if (G.cfg.hal) mcs_hal_open_lib_ex(vm, G.cfg.hal, MCS_HAL_NO_EVENTS);
#endif
    mcs_threads_open_lib(vm);
    if (G.cfg.setup) G.cfg.setup(vm, G.cfg.ud);
    AT_STORE(w->state, MCS_THREAD_RUNNING);
    sleep_until(w, mcs_os_ticks() + w->delay);

    mcs_value_t argv_arr = mcs_null();
    int argc = 0;
    bool loaded = false, ok = true;
    if (w->fn[0]) {                       /* load the script once, then call the function */
        if (!AT_LOAD(w->stop) && load(w, vm, vfsp) != MCS_OK) { set_error(w, mcs_last_error(vm)); ok = false; }
        loaded = true;
        if (ok && w->args) {
            argv_arr = unmarshal(vm, w->args, w->args_len);
            argc = (int)mcs_len(argv_arr);
        }
    }
    int pin = mcs_is_null(argv_arr) ? -1 : mcs_pin(vm, argv_arr);
    uint32_t next = mcs_os_ticks();
    while (ok && !AT_LOAD(w->stop)) {
        mcs_result_t r;
        mcs_value_t res = mcs_null();
        if (w->fn[0]) {
            mcs_value_t args[16];
            int n = argc > 16 ? 16 : argc;
            for (int i = 0; i < n; i++) args[i] = mcs_index(argv_arr, (uint32_t)i);
            r = mcs_call(vm, w->fn, n, args, &res);
        } else r = load(w, vm, vfsp);
        (void)loaded;
        if (AT_LOAD(w->stop)) break;
        AT_STORE(w->runs, AT_LOAD(w->runs) + 1);
        if (r == MCS_OK) {
            AT_STORE(w->failures, 0);
            if (!w->period && w->fn[0] && !mcs_is_null(res)) {   /* Thread.Run result back to the caller */
                size_t len = 0;
                uint8_t* blob = marshal(vm, res, &len);
                if (!blob) { set_error(w, "the result cannot be passed between threads (null, bool, numbers, char, string, arrays)"); ok = false; break; }
                w->result = blob; w->result_len = len;
            }
        } else {
            AT_STORE(w->failures, AT_LOAD(w->failures) + 1);
            set_error(w, mcs_last_error(vm));
            if (!w->period || (w->maxf && AT_LOAD(w->failures) >= w->maxf)) { ok = false; break; }
        }
        if (!w->period) break;
        next += w->period;
        if ((int32_t)(mcs_os_ticks() - next) >= 0) next = mcs_os_ticks() + w->period;   /* late: no burst */
        sleep_until(w, next);
    }
    if (pin >= 0) mcs_unpin(vm, pin);
    final = AT_LOAD(w->stop) ? MCS_THREAD_STOPPED : ok ? MCS_THREAD_DONE : MCS_THREAD_FAILED;
#if MCS_ENABLE_HAL
    if (G.cfg.hal) mcs_hal_close_lib(vm);
#endif
    mcs_free(vm);
out:
    flush_line(w);
    heap_free(w->heap, w->heap_size);
    w->heap = NULL;
    mcs_os_mutex_lock(G.lock);
    AT_STORE(w->state, final);
    w->ended = 1;
    mcs_os_mutex_unlock(G.lock);
    mcs_os_sem_give(w->done);
}

static int start_ex(const mcs_thread_spec_t* s, uint8_t* args, size_t args_len) {
    if (!G.inited) return -5;
    if ((!s->path || !*s->path) && !s->code) return -4;
    if (s->path && strlen(s->path) >= TH_PATH_MAX) return -4;
    if (s->function && strlen(s->function) >= TH_FN_MAX) return -4;
    if (s->core >= mcs_os_cores() || s->core < MCS_OS_ANY_CORE) return -3;
    reap(false);
    worker_t* w = (worker_t*)mcs_os_malloc(sizeof *w);
    if (!w) return -2;
    memset(w, 0, sizeof *w);
    if (s->path) strcpy(w->path, s->path); else strcpy(w->path, "<code>");
    if (s->function) strcpy(w->fn, s->function);
    w->code = s->code; w->code_len = s->code_len;
    w->delay = s->delay_ms; w->period = s->period_ms; w->maxf = s->max_failures;
    w->core = s->core; w->prio = s->priority;
    w->heap_size = s->heap_size ? s->heap_size : G.cfg.heap_size ? G.cfg.heap_size : MCS_THREAD_HEAP;
    w->stack_size = s->stack_size ? s->stack_size : G.cfg.stack_size ? G.cfg.stack_size : MCS_THREAD_STACK;
    w->refs = 1;
    w->done = mcs_os_sem_new(0, 1);
    w->heap = G.cfg.heap_fn ? G.cfg.heap_fn(G.cfg.heap_ud, NULL, 0, w->heap_size) : mcs_os_malloc(w->heap_size);
    if (!w->done || !w->heap) { mcs_os_sem_free(w->done); heap_free(w->heap, w->heap_size); mcs_os_free(w); return -2; }
    mcs_os_mutex_lock(G.lock);
    int slot = -1, running = 0;
    for (int i = 0; i < TH_SLOTS; i++) {
        if (!G.w[i]) { if (slot < 0) slot = i; }
        else running += !G.w[i]->ended;
    }
    if (running >= MCS_THREADS_MAX) slot = -1;
    if (slot >= 0) { w->id = G.next_id++; if (G.next_id <= 0) G.next_id = 1; G.w[slot] = w; }
    mcs_os_mutex_unlock(G.lock);
    if (slot < 0) { mcs_os_sem_free(w->done); heap_free(w->heap, w->heap_size); mcs_os_free(w); return -1; }
    w->args = args; w->args_len = args_len;
    G.active = true;
    char name[16];
    snprintf(name, sizeof name, "mcs%d", w->id);
    mcs_os_thread_cfg_t tc = { name, w->stack_size, w->prio, w->core };
    w->th = mcs_os_thread_start(&tc, worker_main, w);
    if (!w->th) {
        mcs_os_mutex_lock(G.lock);
        G.w[slot] = NULL;
        mcs_os_mutex_unlock(G.lock);
        w->args = NULL;                    /* still owned by the caller */
        mcs_os_sem_free(w->done); heap_free(w->heap, w->heap_size); mcs_os_free(w);
        return s->core > 0 ? -3 : -2;
    }
    return w->id;
}

int mcs_thread_start(const mcs_thread_spec_t* spec) { return start_ex(spec, NULL, 0); }

void mcs_thread_stop(int id) {
    if (!G.inited) return;
    mcs_os_mutex_lock(G.lock);
    worker_t* w = find_locked(id);
    if (w) AT_STORE(w->stop, 1);
    mcs_os_mutex_unlock(G.lock);
}

static mcs_os_sem_t* done_sem(int id, bool* ended) {
    mcs_os_mutex_lock(G.lock);
    worker_t* w = find_locked(id);
    mcs_os_sem_t* s = w ? w->done : NULL;
    *ended = !w || w->ended;
    mcs_os_mutex_unlock(G.lock);
    return s;
}
bool mcs_thread_join(int id, uint32_t timeout_ms) {
    if (!G.inited) return true;
    bool ended;
    mcs_os_sem_t* s = done_sem(id, &ended);
    if (ended || !s) return true;
    if (!mcs_os_sem_take(s, timeout_ms)) return false;
    mcs_os_sem_give(s);                    /* for other joiners */
    return true;
}

bool mcs_thread_info(int id, mcs_thread_info_t* out) {
    if (!G.inited) return false;
    mcs_os_mutex_lock(G.lock);
    worker_t* w = find_locked(id);
    if (w) {
        out->id = w->id; out->state = (mcs_thread_state_t)AT_LOAD(w->state); out->core = w->core;
        out->runs = AT_LOAD(w->runs); out->failures = AT_LOAD(w->failures); out->period_ms = w->period;
        out->name = w->path; out->error = w->error;
    }
    mcs_os_mutex_unlock(G.lock);
    return w != NULL;
}

void mcs_thread_release(int id) {
    if (!G.inited) return;
    mcs_os_mutex_lock(G.lock);
    worker_t* w = find_locked(id);
    if (w && w->refs > 0) w->refs--;
    mcs_os_mutex_unlock(G.lock);
    reap(false);
}
int mcs_thread_count(void) {
    if (!G.inited) return 0;
    int n = 0;
    mcs_os_mutex_lock(G.lock);
    for (int i = 0; i < TH_SLOTS; i++) n += G.w[i] && !G.w[i]->ended;
    mcs_os_mutex_unlock(G.lock);
    return n;
}
int mcs_thread_id_at(int index) {
    if (!G.inited) return 0;
    int id = 0, k = 0;
    mcs_os_mutex_lock(G.lock);
    for (int i = 0; i < TH_SLOTS && !id; i++) if (G.w[i] && k++ == index) id = G.w[i]->id;
    mcs_os_mutex_unlock(G.lock);
    return id;
}

const char* mcs_thread_strerror(int e) {
    switch (e) {
    case -1: return "too many threads (MCS_THREADS_MAX)";
    case -2: return "out of memory for the thread (heap / stack)";
    case -3: return "no such core, or this OS build cannot pin threads to it";
    case -4: return "invalid thread (no script, or a name that is too long)";
    case -5: return "threads are not initialised (mcs_threads_init)";
    default: return "ok";
    }
}

int mcs_threads_shutdown(uint32_t timeout_ms) {
    if (!G.inited) return 0;
    mcs_os_mutex_lock(G.lock);
    for (int i = 0; i < TH_SLOTS; i++) if (G.w[i]) AT_STORE(G.w[i]->stop, 1);
    mcs_os_mutex_unlock(G.lock);
    uint32_t t0 = mcs_os_ticks();
    for (int i = 0; i < TH_SLOTS; i++) {
        int id = 0;
        mcs_os_mutex_lock(G.lock);
        if (G.w[i]) id = G.w[i]->id;
        mcs_os_mutex_unlock(G.lock);
        if (!id) continue;
        uint32_t el = mcs_os_ticks() - t0;
        mcs_thread_join(id, timeout_ms == MCS_OS_FOREVER ? MCS_OS_FOREVER : el < timeout_ms ? timeout_ms - el : 0);
    }
    reap(true);
    int left = mcs_thread_count();
    if (!left) {
        for (int i = 0; i < MCS_CHANNELS_MAX; i++) {
            channel_t* c = G.ch[i];
            if (!c) continue;
            for (unsigned k = 0; k < c->count; k++) mcs_os_free(c->q[(c->head + k) % c->cap].p);
            mcs_os_mutex_free(c->m); mcs_os_sem_free(c->items); mcs_os_sem_free(c->space);
            mcs_os_free(c->q); mcs_os_free(c);
            G.ch[i] = NULL;
        }
        G.active = false;
    }
    return left;
}

/* ------------------------------------------------------------ waiting from C# */
/* Wait for `s` like Thread.Sleep waits: the main VM keeps running interrupt callbacks
 * and scheduler jobs (its idle handler), every VM stays abortable. */
static bool vm_wait(mcs_vm_t* vm, mcs_os_sem_t* s, uint32_t timeout) {
    worker_t* me = SELF(vm);
    uint32_t t0 = mcs_os_ticks();
    for (;;) {
        uint32_t slice = MCS_SLEEP_SLICE_MS;
        if (timeout != MCS_OS_FOREVER) {
            uint32_t el = mcs_os_ticks() - t0;
            if (el >= timeout) return mcs_os_sem_take(s, 0);
            if (timeout - el < slice) slice = timeout - el;
        }
        if (mcs_os_sem_take(s, slice)) return true;
        if (!me) {
            void* ud;
            mcs_idle_fn f = mcs_get_idle(vm, &ud);
            if (f) { f(vm, ud); if (mcs_has_exception(vm)) return false; }
        }
        if (mcs_safepoint(vm)) return false;
    }
}
static uint32_t timeout_arg(mcs_vm_t* vm, int argc, mcs_value_t* argv, int i) {
    if (argc <= i) return MCS_OS_FOREVER;
    mcs_int_t ms = mcs_to_int(vm, argv[i]);
    return ms < 0 ? MCS_OS_FOREVER : (uint32_t)ms;
}

/* ------------------------------------------------------------ C#: Worker */
typedef struct { int id; } handle_t;
static void handle_fin(mcs_vm_t* vm, void* data) { handle_t* h = (handle_t*)data; if (h->id) mcs_thread_release(h->id); h->id = 0; }
static const mcs_class_def_t worker_def;
static int handle_id(mcs_vm_t* vm, mcs_value_t self) {
    handle_t* h = (handle_t*)mcs_check_userdata(vm, self, &worker_def);
    if (!h) return 0;
    if (!h->id) mcs_raise(vm, "InvalidOperationException", "not a started thread (use Thread.Start / Thread.Run)");
    return h->id;
}
static bool join_vm(mcs_vm_t* vm, int id, uint32_t timeout) {
    bool ended;
    mcs_os_sem_t* s = done_sem(id, &ended);
    if (ended || !s) return true;
    if (!vm_wait(vm, s, timeout)) return false;
    mcs_os_sem_give(s);
    return true;
}
NATIVE(w_join) {
    int id = handle_id(vm, self); if (!id) return mcs_null();
    uint32_t t = timeout_arg(vm, argc, argv, 0); if (mcs_has_exception(vm)) return mcs_null();
    return mcs_bool(join_vm(vm, id, t));
}
NATIVE(w_stop) { int id = handle_id(vm, self); if (id) mcs_thread_stop(id); return mcs_null(); }
static bool info_of(mcs_vm_t* vm, mcs_value_t self, mcs_thread_info_t* in) {
    int id = handle_id(vm, self);
    if (!id) return false;
    if (!mcs_thread_info(id, in)) { mcs_raise(vm, "InvalidOperationException", "thread %d is gone", id); return false; }
    return true;
}
NATIVE(w_alive) { mcs_thread_info_t in; if (!info_of(vm, self, &in)) return mcs_null(); return mcs_bool(in.state <= MCS_THREAD_RUNNING); }
NATIVE(w_id) { mcs_thread_info_t in; if (!info_of(vm, self, &in)) return mcs_null(); return mcs_int(in.id); }
NATIVE(w_core) { mcs_thread_info_t in; if (!info_of(vm, self, &in)) return mcs_null(); return mcs_int(in.core); }
NATIVE(w_runs) { mcs_thread_info_t in; if (!info_of(vm, self, &in)) return mcs_null(); return mcs_int((mcs_int_t)in.runs); }
static const char* state_name(int s) {
    return s == MCS_THREAD_STARTING ? "starting" : s == MCS_THREAD_RUNNING ? "running" : s == MCS_THREAD_DONE ? "done"
         : s == MCS_THREAD_FAILED ? "failed" : "stopped";
}
NATIVE(w_state) { mcs_thread_info_t in; if (!info_of(vm, self, &in)) return mcs_null(); return mcs_string(vm, state_name(in.state)); }
NATIVE(w_error) {
    mcs_thread_info_t in; if (!info_of(vm, self, &in)) return mcs_null();
    char e[TH_ERR_MAX];
    mcs_os_mutex_lock(G.lock); snprintf(e, sizeof e, "%s", in.error); mcs_os_mutex_unlock(G.lock);
    return e[0] ? mcs_string(vm, e) : mcs_null();
}
NATIVE(w_result) {
    int id = handle_id(vm, self); if (!id) return mcs_null();
    if (!join_vm(vm, id, MCS_OS_FOREVER)) return mcs_null();
    mcs_os_mutex_lock(G.lock);
    worker_t* w = find_locked(id);
    int st = w ? AT_LOAD(w->state) : MCS_THREAD_STOPPED;
    char e[TH_ERR_MAX]; snprintf(e, sizeof e, "%s", w ? w->error : "gone");
    const uint8_t* p = w ? w->result : NULL; size_t n = w ? w->result_len : 0;
    mcs_os_mutex_unlock(G.lock);
    if (st == MCS_THREAD_FAILED) { mcs_raise(vm, "InvalidOperationException", "thread %d failed: %s", id, e); return mcs_null(); }
    if (st == MCS_THREAD_STOPPED) { mcs_raise(vm, "InvalidOperationException", "thread %d was stopped", id); return mcs_null(); }
    return p ? unmarshal(vm, p, n) : mcs_null();   /* ended: w and its result stay until released */
}
static const mcs_reg_t worker_members[] = {
    MCS_FN("Join", w_join, -1), MCS_FN("Stop", w_stop, 0),
    MCS_GET("IsAlive", w_alive), MCS_GET("Id", w_id), MCS_GET("Core", w_core), MCS_GET("Runs", w_runs),
    MCS_GET("State", w_state), MCS_GET("Error", w_error), MCS_GET("Result", w_result),
    MCS_REG_END
};
static const mcs_class_def_t worker_def = { "Worker", sizeof(handle_t), NULL, handle_fin, worker_members, NULL };

/* ------------------------------------------------------------ C#: Thread.* */
static mcs_value_t start_from(mcs_vm_t* vm, mcs_thread_spec_t* s, int argc, mcs_value_t* argv) {
    uint8_t* args = NULL; size_t alen = 0;
    if (s->function) {
        mcs_value_t arr = mcs_new_array(vm, (uint32_t)(argc > 0 ? argc : 0));
        for (int i = 0; i < argc; i++) mcs_set_index(arr, (uint32_t)i, argv[i]);
        mcs_push_root(vm, arr);
        args = marshal(vm, arr, &alen);
        mcs_pop_root(vm, 1);
        if (!args) return mcs_null();
        if (argc > 16) { mcs_os_free(args); mcs_raise(vm, "ArgumentException", "at most 16 arguments"); return mcs_null(); }
    }
    mcs_value_t obj;
    if (mcs_new_object(vm, "Worker", 0, NULL, &obj) != MCS_OK) { mcs_os_free(args); return mcs_null(); }
    mcs_push_root(vm, obj);
    int id = start_ex(s, args, alen);
    if (id == -1) {          /* ended threads may still be held by unreachable Worker objects */
        mcs_gc(vm);
        id = start_ex(s, args, alen);
    }
    mcs_pop_root(vm, 1);
    if (id < 0) {
        mcs_os_free(args);
        if (id == -3) mcs_raise(vm, "ArgumentOutOfRangeException", "core %d: %s (%s has %d core(s))", s->core, mcs_thread_strerror(id), mcs_os_name(), mcs_os_cores());
        else mcs_raise(vm, id == -2 ? "OutOfMemoryException" : "InvalidOperationException", "%s", mcs_thread_strerror(id));
        return mcs_null();
    }
    ((handle_t*)mcs_userdata(obj))->id = id;
    return obj;
}
static bool str_arg(mcs_vm_t* vm, mcs_value_t v, const char** out, const char* what) {
    if (!mcs_is_string(v)) { mcs_raise(vm, "ArgumentException", "%s must be a string", what); return false; }
    *out = mcs_cstr(v);
    return true;
}
static bool core_arg(mcs_vm_t* vm, mcs_value_t v, int* core) {
    mcs_int_t c = mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return false;
    *core = c < 0 ? MCS_OS_ANY_CORE : (int)c;
    return true;
}
/* Thread.Start(path [, core [, heapKB]]) */
NATIVE(t_start) {
    mcs_thread_spec_t s = MCS_THREAD_SPEC_DEFAULTS;
    if (argc < 1 || argc > 3) { mcs_raise(vm, "ArgumentException", "Thread.Start(path [, core [, heapKB]])"); return mcs_null(); }
    if (!str_arg(vm, argv[0], &s.path, "path")) return mcs_null();
    if (argc > 1 && !core_arg(vm, argv[1], &s.core)) return mcs_null();
    if (argc > 2) { mcs_int_t kb = mcs_to_int(vm, argv[2]); if (mcs_has_exception(vm)) return mcs_null(); if (kb > 0) s.heap_size = (uint32_t)kb * 1024u; }
    return start_from(vm, &s, 0, NULL);
}
/* Thread.Run(path, function, args...) */
NATIVE(t_run) {
    mcs_thread_spec_t s = MCS_THREAD_SPEC_DEFAULTS;
    if (argc < 2) { mcs_raise(vm, "ArgumentException", "Thread.Run(path, function, args...)"); return mcs_null(); }
    if (!str_arg(vm, argv[0], &s.path, "path") || !str_arg(vm, argv[1], &s.function, "function")) return mcs_null();
    return start_from(vm, &s, argc - 2, argv + 2);
}
/* Thread.RunOn(core, path, function, args...) */
NATIVE(t_runon) {
    mcs_thread_spec_t s = MCS_THREAD_SPEC_DEFAULTS;
    if (argc < 3) { mcs_raise(vm, "ArgumentException", "Thread.RunOn(core, path, function, args...)"); return mcs_null(); }
    if (!core_arg(vm, argv[0], &s.core)) return mcs_null();
    if (!str_arg(vm, argv[1], &s.path, "path") || !str_arg(vm, argv[2], &s.function, "function")) return mcs_null();
    return start_from(vm, &s, argc - 3, argv + 3);
}
/* Thread.Every(ms, path [, function] [, core]) */
NATIVE(t_every) {
    mcs_thread_spec_t s = MCS_THREAD_SPEC_DEFAULTS;
    if (argc < 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "Thread.Every(ms, path [, function] [, core])"); return mcs_null(); }
    mcs_int_t ms = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (ms <= 0) { mcs_raise(vm, "ArgumentOutOfRangeException", "invalid interval %d ms", (int)ms); return mcs_null(); }
    s.period_ms = (uint32_t)ms;
    s.max_failures = 0;                   /* keep running: errors are reported, Error shows the last */
    if (!str_arg(vm, argv[1], &s.path, "path")) return mcs_null();
    int i = 2;
    if (argc > i && mcs_is_string(argv[i])) s.function = mcs_cstr(argv[i++]);
    if (argc > i && !core_arg(vm, argv[i++], &s.core)) return mcs_null();
    if (argc > i) { mcs_raise(vm, "ArgumentException", "Thread.Every(ms, path [, function] [, core])"); return mcs_null(); }
    return start_from(vm, &s, 0, NULL);
}
NATIVE(t_cores) { return mcs_int(mcs_os_cores()); }
NATIVE(t_core) { return mcs_int(mcs_os_core()); }
NATIVE(t_os) { return mcs_string(vm, mcs_os_name()); }
NATIVE(t_count) { return mcs_int(mcs_thread_count()); }
NATIVE(t_id) { worker_t* w = SELF(vm); return mcs_int(w ? w->id : 0); }
NATIVE(t_stopall) {
    mcs_os_mutex_lock(G.lock);
    worker_t* me = SELF(vm);
    for (int i = 0; i < TH_SLOTS; i++) if (G.w[i] && G.w[i] != me) AT_STORE(G.w[i]->stop, 1);
    mcs_os_mutex_unlock(G.lock);
    return mcs_null();
}
static const mcs_reg_t thread_fns[] = {
    MCS_FN("Start", t_start, -1), MCS_FN("Run", t_run, -1), MCS_FN("RunOn", t_runon, -1),
    MCS_FN("Every", t_every, -1), MCS_FN("StopAll", t_stopall, 0),
    MCS_GET("Cores", t_cores), MCS_GET("CurrentCore", t_core), MCS_GET("Os", t_os),
    MCS_GET("Count", t_count), MCS_GET("Id", t_id),
    MCS_REG_END
};

/* ------------------------------------------------------------ C#: Channel */
typedef struct { channel_t* c; } chref_t;
static const mcs_class_def_t channel_def;
static void ch_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    chref_t* r = (chref_t*)mcs_userdata(self);
    const char* name;
    if (argc < 1 || argc > 2 || !mcs_is_string(argv[0])) { mcs_raise(vm, "ArgumentException", "Channel(name [, capacity])"); return; }
    name = mcs_cstr(argv[0]);
    mcs_int_t cap = argc > 1 ? mcs_to_int(vm, argv[1]) : 8;
    if (mcs_has_exception(vm)) return;
    if (cap < 1 || cap > 256) { mcs_raise(vm, "ArgumentOutOfRangeException", "capacity must be 1..256"); return; }
    if (!*name || strlen(name) >= sizeof ((channel_t*)0)->name) { mcs_raise(vm, "ArgumentException", "channel name must be 1..23 characters"); return; }
    if (!G.inited) { mcs_raise(vm, "InvalidOperationException", "%s", mcs_thread_strerror(-5)); return; }
    mcs_os_mutex_lock(G.lock);
    channel_t* c = NULL;
    int free_slot = -1;
    for (int i = 0; i < MCS_CHANNELS_MAX && !c; i++) {
        if (G.ch[i] && !strcmp(G.ch[i]->name, name)) c = G.ch[i];
        else if (!G.ch[i] && free_slot < 0) free_slot = i;
    }
    const char* fail = NULL;
    if (!c && free_slot < 0) fail = "too many channels (MCS_CHANNELS_MAX)";
    else if (!c) {
        c = (channel_t*)mcs_os_malloc(sizeof *c);
        if (c) {
            memset(c, 0, sizeof *c);
            strcpy(c->name, name);
            c->cap = (unsigned)cap;
            c->q = (msg_t*)mcs_os_malloc(sizeof(msg_t) * c->cap);
            c->m = mcs_os_mutex_new();
            c->items = mcs_os_sem_new(0, c->cap);
            c->space = mcs_os_sem_new(c->cap, c->cap);
        }
        if (!c || !c->q || !c->m || !c->items || !c->space) {
            if (c) { mcs_os_free(c->q); mcs_os_mutex_free(c->m); mcs_os_sem_free(c->items); mcs_os_sem_free(c->space); mcs_os_free(c); }
            c = NULL; fail = "out of memory for the channel";
        } else G.ch[free_slot] = c;
    }
    mcs_os_mutex_unlock(G.lock);
    if (fail) { mcs_raise(vm, "InvalidOperationException", "%s", fail); return; }
    r->c = c;
}
static channel_t* ch_of(mcs_vm_t* vm, mcs_value_t self) {
    chref_t* r = (chref_t*)mcs_check_userdata(vm, self, &channel_def);
    if (r && !r->c) mcs_raise(vm, "InvalidOperationException", "channel is closed");
    return r ? r->c : NULL;
}
static mcs_value_t send(mcs_vm_t* vm, mcs_value_t self, mcs_value_t v, uint32_t timeout) {
    channel_t* c = ch_of(vm, self);
    if (!c) return mcs_null();
    size_t n = 0;
    uint8_t* blob = marshal(vm, v, &n);
    if (!blob) return mcs_null();
    if (!(timeout == 0 ? mcs_os_sem_take(c->space, 0) : vm_wait(vm, c->space, timeout))) { mcs_os_free(blob); return mcs_bool(false); }
    mcs_os_mutex_lock(c->m);
    c->q[(c->head + c->count) % c->cap].p = blob;
    c->q[(c->head + c->count) % c->cap].n = (uint32_t)n;
    c->count++;
    mcs_os_mutex_unlock(c->m);
    mcs_os_sem_give(c->items);
    return mcs_bool(true);
}
static mcs_value_t receive(mcs_vm_t* vm, mcs_value_t self, uint32_t timeout) {
    channel_t* c = ch_of(vm, self);
    if (!c) return mcs_null();
    if (!(timeout == 0 ? mcs_os_sem_take(c->items, 0) : vm_wait(vm, c->items, timeout))) return mcs_null();
    mcs_os_mutex_lock(c->m);
    msg_t m = c->q[c->head];
    c->head = (c->head + 1) % c->cap;
    c->count--;
    mcs_os_mutex_unlock(c->m);
    mcs_os_sem_give(c->space);
    mcs_value_t v = unmarshal(vm, m.p, m.n);
    mcs_os_free(m.p);
    return v;
}
NATIVE(ch_send) {
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Send(value [, timeoutMs])"); return mcs_null(); }
    uint32_t t = timeout_arg(vm, argc, argv, 1); if (mcs_has_exception(vm)) return mcs_null();
    return send(vm, self, argv[0], t);
}
NATIVE(ch_trysend) { return send(vm, self, argv[0], 0); }
NATIVE(ch_receive) {
    uint32_t t = timeout_arg(vm, argc, argv, 0); if (mcs_has_exception(vm)) return mcs_null();
    return receive(vm, self, t);
}
NATIVE(ch_tryreceive) { return receive(vm, self, 0); }
NATIVE(ch_count) {
    channel_t* c = ch_of(vm, self); if (!c) return mcs_null();
    mcs_os_mutex_lock(c->m); unsigned n = c->count; mcs_os_mutex_unlock(c->m);
    return mcs_int((mcs_int_t)n);
}
NATIVE(ch_cap) { channel_t* c = ch_of(vm, self); return c ? mcs_int((mcs_int_t)c->cap) : mcs_null(); }
NATIVE(ch_name) { channel_t* c = ch_of(vm, self); return c ? mcs_string(vm, c->name) : mcs_null(); }
static const mcs_reg_t channel_members[] = {
    MCS_FN("Send", ch_send, -1), MCS_FN("TrySend", ch_trysend, 1),
    MCS_FN("Receive", ch_receive, -1), MCS_FN("TryReceive", ch_tryreceive, 0),
    MCS_GET("Count", ch_count), MCS_GET("Capacity", ch_cap), MCS_GET("Name", ch_name),
    MCS_REG_END
};
static const mcs_class_def_t channel_def = { "Channel", sizeof(chref_t), ch_ctor, NULL, channel_members, NULL };

void mcs_threads_open_lib(mcs_vm_t* vm) {
    if (!mcs_get_ext(vm, MCS_EXT_THREADS)) mcs_set_ext(vm, MCS_EXT_THREADS, &g_main_tag);
    mcs_register_module(vm, "Thread", thread_fns);
    mcs_register_class(vm, &worker_def);
    mcs_register_class(vm, &channel_def);
}
#else
typedef int mcs_threads_unused_t;   /* no OS chosen: empty unit */
#endif
