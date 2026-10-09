/*
 * MicroCS - job scheduler (optional module, MCS_ENABLE_SCHED).
 *
 * The scheduler does not own a thread. The host calls mcs_sched_poll() from
 * its main loop or RTOS task; due jobs run there, one after another, as normal
 * top-level runs (so execution limits, the hook and error reporting apply to
 * each job). This works the same on bare metal and under an RTOS, and keeps
 * the single-threaded VM rule intact.
 *
 * Periodic jobs that fall behind are not replayed in a burst: the next run is
 * scheduled one period after the late run.
 */
#ifndef MCS_SCHED_H
#define MCS_SCHED_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef MCS_SCHED_DURING_SLEEP
#define MCS_SCHED_DURING_SLEEP 1    /* due jobs run while a script waits in Thread.Sleep */
#endif
#ifndef MCS_SCHED_MAX_JOBS
#define MCS_SCHED_MAX_JOBS 8
#endif
#ifndef MCS_SCHED_PATH_MAX
#define MCS_SCHED_PATH_MAX 64
#endif

typedef enum { MCS_JOB_FREE = 0, MCS_JOB_ACTIVE, MCS_JOB_DONE, MCS_JOB_FAILED, MCS_JOB_CANCELLED } mcs_job_state_t;

typedef struct {
    int id;
    uint8_t state;
    bool periodic;
    bool is_file;
    uint16_t max_failures;      /* consecutive failures before the job stops; 0 = never stop */
    uint16_t failures;
    uint32_t period_ms;
    uint32_t next_due;
    uint32_t runs;
    int fn_slot;                /* index into the pinned delegate list */
    char path[MCS_SCHED_PATH_MAX];
} mcs_job_t;

struct mcs_vfs;
typedef struct mcs_sched {
    mcs_vm_t* vm;
    struct mcs_vfs* vfs;        /* for file jobs; may be NULL */
    mcs_ticks_fn ticks;
    void* ticks_ud;
    mcs_job_t jobs[MCS_SCHED_MAX_JOBS];
    int next_id;
    int pin;                    /* pinned List holding job delegates */
    bool busy;                  /* jobs are running (no nested poll) */
    mcs_idle_fn prev_idle;      /* chained idle handler (HAL), see mcs_sched_open_lib */
    void* prev_idle_ud;
    bool idle_on;
} mcs_sched_t;

/* `ticks` is the millisecond clock (usually the same as cfg.ticks_fn). */
int mcs_sched_init(mcs_sched_t* s, mcs_vm_t* vm, struct mcs_vfs* vfs, mcs_ticks_fn ticks, void* ticks_ud);
void mcs_sched_free(mcs_sched_t* s);

/* Add jobs. Return job id (>0) or -1 when the table is full. delay_ms is the
 * time until the first run; period_ms 0 = run once. */
int mcs_sched_add_file(mcs_sched_t* s, const char* path, uint32_t delay_ms, uint32_t period_ms, uint16_t max_failures);
int mcs_sched_add_fn(mcs_sched_t* s, mcs_value_t fn, uint32_t delay_ms, uint32_t period_ms, uint16_t max_failures);
bool mcs_sched_cancel(mcs_sched_t* s, int id);
/* Cancel every active job, only delegate jobs (Scheduler.Every/After from
 * scripts) or only file jobs (jobs.cfg, shell every/after). Returns the count. */
enum { MCS_SCHED_ALL = 0, MCS_SCHED_DELEGATES = 1, MCS_SCHED_FILES = 2 };
int mcs_sched_cancel_all(mcs_sched_t* s, int which);
int mcs_sched_active(const mcs_sched_t* s);

/* Run every due job once. Returns ms until the next job is due (0 = something
 * is already due again), or -1 when no active jobs remain. */
int32_t mcs_sched_poll(mcs_sched_t* s);

/* Convenience loop: poll, then sleep with `delay` (at most `max_sleep_ms` at a
 * time so `*stop` is noticed). Ends when no jobs remain, *stop becomes non-zero,
 * or `run_for_ms` elapses (0 = forever). */
void mcs_sched_run(mcs_sched_t* s, mcs_delay_fn delay, void* delay_ud, volatile int* stop,
                   uint32_t max_sleep_ms, uint32_t run_for_ms);

/* Load a jobs table, one job per line:
 *     startup <path>              run once at start
 *     after   <time> <path>       run once after <time>
 *     every   <time> <path> [restart=never|always|<n>]
 * <time> is a number with an optional ms/s/m/h suffix (default ms). '#' starts a
 * comment. Returns the number of jobs added, or -(line number) on a syntax error. */
int mcs_sched_load_config(mcs_sched_t* s, const char* text);

/* C# API: Scheduler.Every(ms, action [, maxFailures]), Scheduler.After(ms, action),
 * Scheduler.Cancel(id), Scheduler.Count. Stored in MCS_EXT_SCHED. */
void mcs_sched_open_lib(mcs_vm_t* vm, mcs_sched_t* s);

#ifdef __cplusplus
}
#endif
#endif
