/* MicroCS - polled job scheduler. */
#include "mcs_sched.h"
#if MCS_ENABLE_SCHED
#include <string.h>
#include <stdlib.h>
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif

static uint32_t now(mcs_sched_t* s) { return s->ticks ? s->ticks(s->ticks_ud) : 0; }

int mcs_sched_init(mcs_sched_t* s, mcs_vm_t* vm, struct mcs_vfs* vfs, mcs_ticks_fn ticks, void* ud) {
    memset(s, 0, sizeof *s);
    s->vm = vm; s->vfs = vfs; s->ticks = ticks; s->ticks_ud = ud;
    s->next_id = 1;
    s->pin = mcs_pin(vm, mcs_new_list(vm));
    return s->pin < 0 ? -1 : 0;
}

void mcs_sched_free(mcs_sched_t* s) {
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

bool mcs_sched_cancel(mcs_sched_t* s, int id) {
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++)
        if (s->jobs[i].id == id && s->jobs[i].state == MCS_JOB_ACTIVE) {
            s->jobs[i].state = MCS_JOB_CANCELLED;
            release_fn(s, &s->jobs[i]);
            return true;
        }
    return false;
}

int mcs_sched_active(const mcs_sched_t* s) {
    int n = 0;
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) n += s->jobs[i].state == MCS_JOB_ACTIVE;
    return n;
}

static mcs_result_t run_job(mcs_sched_t* s, mcs_job_t* j) {
    if (j->is_file) {
#if MCS_ENABLE_FS
        if (s->vfs) return mcs_exec_file(s->vm, s->vfs, j->path);
#endif
        return mcs_fail(s->vm, MCS_ERR_RUNTIME, "job %d: no filesystem for '%s'", j->id, j->path);
    }
    mcs_value_t fn = mcs_index(mcs_pinned(s->vm, s->pin), (uint32_t)j->fn_slot);
    return mcs_call_value(s->vm, fn, 0, NULL, NULL);
}

int32_t mcs_sched_poll(mcs_sched_t* s) {
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) {
        mcs_job_t* j = &s->jobs[i];
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
    int32_t best = -1;
    uint32_t t = now(s);
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) {
        mcs_job_t* j = &s->jobs[i];
        if (j->state != MCS_JOB_ACTIVE) continue;
        int32_t d = (int32_t)(j->next_due - t);
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
        uint32_t sl = (uint32_t)next < max_sleep ? (uint32_t)next : max_sleep;
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
            size_t on = word(q);
            if (on) {
                if (on > 8 && !strncmp(q, "restart=", 8)) {
                    const char* v = q + 8; size_t vn = on - 8;
                    if (vn == 5 && !strncmp(v, "never", 5)) maxf = 1;
                    else if (vn == 6 && !strncmp(v, "always", 6)) maxf = 0;
                    else { uint32_t n; if (!parse_time(v, vn, &n) || n > 65535) return -line_no; maxf = (uint16_t)n; }
                } else return -line_no;
            }
            (void)once;
            if (mcs_sched_add_file(s, path, startup ? 0 : (periodic ? 0 : t), periodic ? t : 0, maxf) < 0) return -line_no;
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
    mcs_sched_t* s = SCHED();
    for (int i = 0; i < MCS_SCHED_MAX_JOBS; i++) if (s->jobs[i].state == MCS_JOB_ACTIVE) mcs_sched_cancel(s, s->jobs[i].id);
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

void mcs_sched_open_lib(mcs_vm_t* vm, mcs_sched_t* s) {
    mcs_set_ext(vm, MCS_EXT_SCHED, s);
    mcs_register_module(vm, "Scheduler", sched_fns);
}
#endif
