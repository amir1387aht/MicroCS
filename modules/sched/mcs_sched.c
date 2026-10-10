/* MicroCS - polled job scheduler. */
#include "mcs_sched.h"
#if MCS_ENABLE_SCHED
#include <string.h>
#include <stdlib.h>
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#if MCS_ENABLE_HAL
#include "mcs_hal.h"
#endif
#if MCS_ENABLE_THREADS
#include "mcs_threads.h"
#endif

static uint32_t now(mcs_sched_t* s) { return s->ticks ? s->ticks(s->ticks_ud) : 0; }

int mcs_sched_init(mcs_sched_t* s, mcs_vm_t* vm, struct mcs_vfs* vfs, mcs_ticks_fn ticks, void* ud) {
    memset(s, 0, sizeof *s);
    s->vm = vm; s->vfs = vfs; s->ticks = ticks; s->ticks_ud = ud;
    s->next_id = 1;
    s->pin = mcs_pin(vm, mcs_new_list(vm));
    return s->pin < 0 ? -1 : 0;
}

#if MCS_SCHED_DURING_SLEEP
static int sched_idle(mcs_vm_t* vm, void* ud);
#endif
void mcs_sched_free(mcs_sched_t* s) {
#if MCS_SCHED_DURING_SLEEP
    if (s->idle_on) {
        void* ud; if (mcs_get_idle(s->vm, &ud) == sched_idle && ud == s) mcs_set_idle(s->vm, s->prev_idle, s->prev_idle_ud);
        s->idle_on = false;
    }
#endif
#if MCS_ENABLE_THREADS
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++)
        if (s->jobs[i].worker) { mcs_thread_stop(s->jobs[i].worker); mcs_thread_release(s->jobs[i].worker); s->jobs[i].worker = 0; }
#endif
    if (s->pin >= 0) mcs_unpin(s->vm, s->pin);
    s->pin = -1;
    if (mcs_get_ext(s->vm, MCS_EXT_SCHED) == s) mcs_set_ext(s->vm, MCS_EXT_SCHED, NULL);
}

static mcs_job_t* alloc_job(mcs_sched_t* s) {
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++)
        if (s->jobs[i].state != MCS_JOB_ACTIVE) {
            mcs_job_t* j = &s->jobs[i];
            memset(j, 0, sizeof *j);
            j->id = s->next_id++;
            j->fn_slot = -1;
            j->core = -1;
            return j;
        }
    return NULL;
}

static void release_fn(mcs_sched_t* s, mcs_job_t* j) {
    if (j->fn_slot < 0) return;
    mcs_set_index(mcs_pinned(s->vm, s->pin), (uint32_t)j->fn_slot, mcs_null());
    j->fn_slot = -1;
}

int mcs_sched_add_file(mcs_sched_t* s, const char* path, uint32_t delay, uint32_t period, uint16_t max_failures) {
    if (strlen(path) >= MCS_SCHED_PATH_MAX) return -1;
    mcs_job_t* j = alloc_job(s);
    if (!j) return -1;
    j->is_file = true;
    strcpy(j->path, path);
    j->periodic = period > 0; j->period_ms = period;
    j->max_failures = max_failures;
    j->next_due = now(s) + delay;
    j->state = MCS_JOB_ACTIVE;
    return j->id;
}

int mcs_sched_add_fn(mcs_sched_t* s, mcs_value_t fn, uint32_t delay, uint32_t period, uint16_t max_failures) {
    mcs_job_t* j = alloc_job(s);
    if (!j) return -1;
    mcs_value_t list = mcs_pinned(s->vm, s->pin);
    uint32_t n = mcs_len(list), slot = n;
    for (uint32_t i = 0; i < n; i++) if (mcs_is_null(mcs_index(list, i))) { slot = i; break; }
    if (slot == n) mcs_list_add(s->vm, list, fn); else mcs_set_index(list, slot, fn);
    j->fn_slot = (int)slot;
    j->periodic = period > 0; j->period_ms = period;
    j->max_failures = max_failures;
    j->next_due = now(s) + delay;
    j->state = MCS_JOB_ACTIVE;
    return j->id;
}

mcs_job_t* mcs_sched_job(mcs_sched_t* s, int id) {
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++)
        if (s->jobs[i].id == id && s->jobs[i].state != MCS_JOB_FREE) return &s->jobs[i];
    return NULL;
}

bool mcs_sched_cancel(mcs_sched_t* s, int id) {
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++)
        if (s->jobs[i].id == id && s->jobs[i].state == MCS_JOB_ACTIVE) {
            s->jobs[i].state = MCS_JOB_CANCELLED;
#if MCS_ENABLE_THREADS
            if (s->jobs[i].worker) { mcs_thread_stop(s->jobs[i].worker); mcs_thread_release(s->jobs[i].worker); s->jobs[i].worker = 0; }
#endif
            release_fn(s, &s->jobs[i]);
            return true;
        }
    return false;
}

int mcs_sched_cancel_all(mcs_sched_t* s, int which) {
    int n = 0;
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) {
        mcs_job_t* j = &s->jobs[i];
        if (j->state != MCS_JOB_ACTIVE) continue;
        if (which == MCS_SCHED_DELEGATES && j->is_file) continue;
        if (which == MCS_SCHED_FILES && !j->is_file) continue;
        n += mcs_sched_cancel(s, j->id);
    }
    return n;
}

int mcs_sched_active(const mcs_sched_t* s) {
    int n = 0;
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) n += s->jobs[i].state == MCS_JOB_ACTIVE;
    return n;
}

static mcs_result_t run_job(mcs_sched_t* s, mcs_job_t* j) {
    if (j->is_file) {
#if MCS_ENABLE_FS
        if (s->vfs) {
            if (!mcs_running(s->vm)) return mcs_exec_file(s->vm, s->vfs, j->path);
            /* inside a sleeping script: keep its top-level variables intact */
            int h = mcs_globals_save(s->vm);
            mcs_result_t r = mcs_exec_file(s->vm, s->vfs, j->path);
            mcs_globals_restore(s->vm, h);
            return r;
        }
#endif
        return mcs_fail(s->vm, MCS_ERR_RUNTIME, "job %d: no filesystem for '%s'", j->id, j->path);
    }
    mcs_value_t fn = mcs_index(mcs_pinned(s->vm, s->pin), (uint32_t)j->fn_slot);
    return mcs_call_value(s->vm, fn, 0, NULL, NULL);
}

#if MCS_ENABLE_THREADS
static void thread_start(mcs_sched_t* s, mcs_job_t* j) {
    mcs_thread_spec_t sp = MCS_THREAD_SPEC_DEFAULTS;
    sp.path = j->path;
    sp.period_ms = j->periodic ? j->period_ms : 0;
    sp.max_failures = j->max_failures;
    sp.core = j->core;
    sp.priority = j->prio;
    sp.stack_size = (uint32_t)j->stack_kb * 1024u;
    sp.heap_size = (uint32_t)j->heap_kb * 1024u;
    int id = mcs_thread_start(&sp);
    if (id < 0) {
        j->state = MCS_JOB_FAILED;
        mcs_fail(s->vm, MCS_ERR_RUNTIME, "job %d: cannot start a thread for '%s': %s", j->id, j->path, mcs_thread_strerror(id));
        return;
    }
    j->worker = id;
}
static void thread_status(mcs_sched_t* s, mcs_job_t* j) {
    mcs_thread_info_t in;
    if (!mcs_thread_info(j->worker, &in)) { j->state = MCS_JOB_DONE; j->worker = 0; return; }
    j->runs = in.runs;
    j->failures = (uint16_t)(in.failures > 65535 ? 65535 : in.failures);
    if (in.state <= MCS_THREAD_RUNNING) return;
    j->state = in.state == MCS_THREAD_DONE ? MCS_JOB_DONE : in.state == MCS_THREAD_FAILED ? MCS_JOB_FAILED : MCS_JOB_CANCELLED;
    mcs_thread_release(j->worker);
    j->worker = 0;
    (void)s;
}
#endif

int32_t mcs_sched_poll(mcs_sched_t* s) {
    bool outer = !s->busy;              /* a job that sleeps does not start other jobs */
    s->busy = true;
    for (int i = 0; outer && i < MCS_SCHED_MAX_JOBS; i++) {
        mcs_job_t* j = &s->jobs[i];
#if MCS_ENABLE_THREADS
        if (j->state == MCS_JOB_ACTIVE && j->thread && j->is_file) {   /* the OS thread runs and times it */
            if (j->worker) thread_status(s, j);
            else if ((int32_t)(now(s) - j->next_due) >= 0) thread_start(s, j);
            continue;
        }
#endif
        if (j->state != MCS_JOB_ACTIVE || (int32_t)(now(s) - j->next_due) < 0) continue;
        int id = j->id;
        mcs_result_t r = run_job(s, j);
        /* the job may have cancelled itself (or been replaced) while running */
        if (j->id != id || j->state != MCS_JOB_ACTIVE) continue;
        j->runs++;
        if (r == MCS_OK) j->failures = 0;
        else if (++j->failures >= j->max_failures && j->max_failures) { j->state = MCS_JOB_FAILED; release_fn(s, j); continue; }
        if (!j->periodic) { j->state = r == MCS_OK ? MCS_JOB_DONE : MCS_JOB_FAILED; release_fn(s, j); continue; }
        j->next_due += j->period_ms;
        if ((int32_t)(now(s) - j->next_due) >= 0) j->next_due = now(s) + j->period_ms;
    }
    if (outer) s->busy = false;
    int32_t best = -1;
    uint32_t t = now(s);
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) {
        mcs_job_t* j = &s->jobs[i];
        if (j->state != MCS_JOB_ACTIVE) continue;
        int32_t d = (int32_t)(j->next_due - t);
#if MCS_ENABLE_THREADS
        if (j->worker) d = 100;             /* only its status to check */
#endif
        if (d < 0) d = 0;
        if (best < 0 || d < best) best = d;
    }
    return best;
}

void mcs_sched_run(mcs_sched_t* s, mcs_delay_fn delay, void* ud, volatile int* stop, uint32_t max_sleep, uint32_t run_for) {
    uint32_t start = now(s);
    if (!max_sleep) max_sleep = 100;
    while (!(stop && *stop)) {
        int32_t next = mcs_sched_poll(s);
        if (next < 0) break;
        uint32_t elapsed = now(s) - start;
        if (run_for && elapsed >= run_for) break;
        uint32_t cap = max_sleep;
#if MCS_ENABLE_HAL
        if (mcs_hal_get(s->vm)) { mcs_hal_poll(s->vm); if (cap > MCS_SLEEP_SLICE_MS) cap = MCS_SLEEP_SLICE_MS; }  /* GPIO / timer callbacks */
#endif
        uint32_t sl = (uint32_t)next < cap ? (uint32_t)next : cap;
        if (run_for && sl > run_for - elapsed) sl = run_for - elapsed;
        if (sl && delay) delay(ud, sl);
    }
}

/* ------------------------------------------------------------ config */
static const char* skip_ws(const char* p) { while (*p == ' ' || *p == '\t') p++; return p; }
static size_t word(const char* p) { size_t n = 0; while (p[n] && p[n] != ' ' && p[n] != '\t' && p[n] != '\n' && p[n] != '\r' && p[n] != '#') n++; return n; }

static bool parse_time(const char* p, size_t n, uint32_t* out) {
    size_t i = 0; uint32_t v = 0;
    if (!n || p[0] < '0' || p[0] > '9') return false;
    while (i < n && p[i] >= '0' && p[i] <= '9') v = v * 10 + (uint32_t)(p[i++] - '0');
    size_t u = n - i;
    if (u == 0 || (u == 2 && !strncmp(p + i, "ms", 2))) *out = v;
    else if (u == 1 && p[i] == 's') *out = v * 1000;
    else if (u == 1 && p[i] == 'm') *out = v * 60000;
    else if (u == 1 && p[i] == 'h') *out = v * 3600000;
    else return false;
    return true;
}

static bool parse_int(const char* p, size_t n, int32_t* out) {
    bool neg = n && *p == '-';
    size_t i = neg ? 1 : 0;
    if (i >= n) return false;
    int32_t v = 0;
    for (; i < n; i++) { if (p[i] < '0' || p[i] > '9' || v > 100000) return false; v = v * 10 + (p[i] - '0'); }
    *out = neg ? -v : v;
    return true;
}
/* 4096, 8k, 1m (bytes), at most 32 MB (kept in KB in 16 bits) */
static bool parse_size(const char* p, size_t n, uint32_t* out) {
    uint32_t mul = 1;
    if (n && (p[n - 1] == 'k' || p[n - 1] == 'K')) { mul = 1024; n--; }
    else if (n && (p[n - 1] == 'm' || p[n - 1] == 'M')) { mul = 1024 * 1024; n--; }
    int32_t v;
    if (!parse_int(p, n, &v) || v <= 0 || (uint64_t)v * mul > 32u * 1024 * 1024) return false;
    *out = (uint32_t)v * mul;
    return true;
}

int mcs_sched_load_config(mcs_sched_t* s, const char* text) {
    int added = 0, line_no = 0;
    const char* p = text;
    while (*p) {
        line_no++;
        const char* eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        const char* q = skip_ws(p);
        size_t kw = word(q);
        if (kw && *q != '#') {
            uint32_t t = 0;
            bool once = false, periodic = false, startup = false;
            if (kw == 7 && !strncmp(q, "startup", 7)) startup = true;
            else if (kw == 5 && !strncmp(q, "after", 5)) once = true;
            else if (kw == 5 && !strncmp(q, "every", 5)) periodic = true;
            else return -line_no;
            q = skip_ws(q + kw);
            if (!startup) {
                size_t tn = word(q);
                if (!parse_time(q, tn, &t)) return -line_no;
                q = skip_ws(q + tn);
            }
            size_t pn = word(q);
            if (!pn || pn >= MCS_SCHED_PATH_MAX) return -line_no;
            char path[MCS_SCHED_PATH_MAX];
            memcpy(path, q, pn); path[pn] = 0;
            q = skip_ws(q + pn);
            uint16_t maxf = 1;
            bool thread = false;
            int32_t core = -1, prio = 0;
            uint32_t stack = 0, heap = 0;
            size_t on;
            while ((on = word(q)) != 0) {
                if (on > 8 && !strncmp(q, "restart=", 8)) {
                    const char* v = q + 8; size_t vn = on - 8;
                    if (vn == 5 && !strncmp(v, "never", 5)) maxf = 1;
                    else if (vn == 6 && !strncmp(v, "always", 6)) maxf = 0;
                    else { uint32_t n; if (!parse_time(v, vn, &n) || n > 65535) return -line_no; maxf = (uint16_t)n; }
                } else if (on == 6 && !strncmp(q, "thread", 6)) thread = true;
                else if (on > 5 && !strncmp(q, "core=", 5)) { if (!parse_int(q + 5, on - 5, &core) || core > 127) return -line_no; thread = true; }
                else if (on > 5 && !strncmp(q, "prio=", 5)) { if (!parse_int(q + 5, on - 5, &prio) || prio < -64 || prio > 64) return -line_no; thread = true; }
                else if (on > 6 && !strncmp(q, "stack=", 6)) { if (!parse_size(q + 6, on - 6, &stack)) return -line_no; thread = true; }
                else if (on > 5 && !strncmp(q, "heap=", 5)) { if (!parse_size(q + 5, on - 5, &heap)) return -line_no; thread = true; }
                else return -line_no;
                q = skip_ws(q + on);
            }
            (void)once;
            int id = mcs_sched_add_file(s, path, startup ? 0 : (periodic ? 0 : t), periodic ? t : 0, maxf);
            if (id < 0) return -line_no;
            if (thread) {
                mcs_job_t* j = mcs_sched_job(s, id);
#if MCS_ENABLE_THREADS
                j->thread = true;
#endif
                j->core = (int8_t)core; j->prio = (int8_t)prio;
                j->stack_kb = (uint16_t)((stack + 1023) / 1024); j->heap_kb = (uint16_t)((heap + 1023) / 1024);
            }
            added++;
        }
        p = *eol ? eol + 1 : eol;
    }
    return added;
}

/* ------------------------------------------------------------ C# API */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define SCHED() ((mcs_sched_t*)mcs_get_ext(vm, MCS_EXT_SCHED))

static mcs_value_t add_common(mcs_vm_t* vm, int argc, mcs_value_t* argv, bool periodic) {
    mcs_int_t ms = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (ms < 0 || (periodic && ms == 0)) { mcs_raise(vm, "ArgumentOutOfRangeException", "invalid interval %d ms", (int)ms); return mcs_null(); }
    if (mcs_is_null(argv[1])) { mcs_raise(vm, "ArgumentNullException", "action"); return mcs_null(); }
    mcs_int_t maxf = argc > 2 ? mcs_to_int(vm, argv[2]) : 1;
    if (mcs_has_exception(vm)) return mcs_null();
    int id = mcs_sched_add_fn(SCHED(), argv[1], periodic ? (uint32_t)ms : (uint32_t)ms, periodic ? (uint32_t)ms : 0,
                              (uint16_t)(maxf < 0 ? 0 : maxf));
    if (id < 0) { mcs_raise(vm, "InvalidOperationException", "too many scheduled jobs (max %d)", MCS_SCHED_MAX_JOBS); return mcs_null(); }
    return mcs_int(id);
}
NATIVE(sc_every) { if (argc < 2) { mcs_raise(vm, "ArgumentException", "Every(ms, action)"); return mcs_null(); } return add_common(vm, argc, argv, true); }
NATIVE(sc_after) { if (argc < 2) { mcs_raise(vm, "ArgumentException", "After(ms, action)"); return mcs_null(); } return add_common(vm, argc, argv, false); }
NATIVE(sc_cancel) {
    mcs_int_t id = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs_bool(mcs_sched_cancel(SCHED(), (int)id));
}
NATIVE(sc_count) { return mcs_int(mcs_sched_active(SCHED())); }
NATIVE(sc_cancel_all) {
    mcs_sched_cancel_all(SCHED(), MCS_SCHED_ALL);
    return mcs_null();
}
static const mcs_reg_t sched_fns[] = {
    MCS_FN("Every", sc_every, -1),
    MCS_FN("After", sc_after, -1),
    MCS_FN("Cancel", sc_cancel, 1),
    MCS_FN("CancelAll", sc_cancel_all, 0),
    MCS_GET("Count", sc_count),
    MCS_REG_END
};

#if MCS_SCHED_DURING_SLEEP
/* While a script waits in Thread.Sleep, due jobs run (cooperative multitasking): a main
 * loop `while (true) { ...; Thread.Sleep(100); }` no longer starves Scheduler.Every
 * delegates and jobs.cfg scripts. The previous idle handler (HAL events) runs first. */
static int sched_idle(mcs_vm_t* vm, void* ud) {
    mcs_sched_t* s = (mcs_sched_t*)ud;
    if (s->prev_idle) { s->prev_idle(vm, s->prev_idle_ud); if (mcs_has_exception(vm)) return 0; }
    if (!s->busy) mcs_sched_poll(s);
    return 0;
}
#endif

void mcs_sched_open_lib(mcs_vm_t* vm, mcs_sched_t* s) {
    mcs_set_ext(vm, MCS_EXT_SCHED, s);
#if MCS_SCHED_DURING_SLEEP
    if (!s->idle_on) {
        s->prev_idle = mcs_get_idle(vm, &s->prev_idle_ud);
        mcs_set_idle(vm, sched_idle, s);
        s->idle_on = true;
    }
#endif
    mcs_register_module(vm, "Scheduler", sched_fns);
}
#endif
