/*
 * MicroCS - optional operating-system layer for real OS threads (MCS_ENABLE_THREADS).
 *
 * Only compiled when an OS is chosen with MCS_OS (mcs_config.h; default MCS_OS_NONE =
 * no OS, nothing here exists). The thread module (mcs_threads.h) runs C# on threads of
 * that OS: FreeRTOS tasks (vanilla, SMP or ESP-IDF), Zephyr kernel threads, or pthreads
 * on a host. modules/os/mcs_os.c implements this small surface once per OS. Everything
 * may be called from any thread; mcs_os_irq_lock() also from interrupt handlers.
 */
#ifndef MCS_OS_H
#define MCS_OS_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#if MCS_ENABLE_THREADS

#define MCS_OS_FOREVER 0xFFFFFFFFu      /* timeout: wait without limit */
#define MCS_OS_ANY_CORE (-1)

typedef struct mcs_os_thread mcs_os_thread_t;   /* opaque, one per thread */
typedef struct mcs_os_mutex mcs_os_mutex_t;     /* mutex (not recursive) */
typedef struct mcs_os_sem mcs_os_sem_t;         /* counting semaphore */

typedef struct {
    const char* name;       /* task / thread name */
    uint32_t stack_bytes;   /* 0 = MCS_THREAD_STACK */
    int priority;           /* relative to the creating thread: 0 = same, +n = n levels more
                               urgent, -n = less urgent (clamped to the application range) */
    int core;               /* MCS_OS_ANY_CORE or a core number < mcs_os_cores() */
} mcs_os_thread_cfg_t;

const char* mcs_os_name(void);          /* "FreeRTOS", "Zephyr", "POSIX" */
int mcs_os_cores(void);                 /* cores threads can run on (1 = single core) */
int mcs_os_core(void);                  /* core of the calling thread (0 when unknown) */

/* Start fn(arg) on a new thread. NULL when out of memory, or `core` does not exist or
 * cannot be pinned on this OS build (no core affinity configured). */
mcs_os_thread_t* mcs_os_thread_start(const mcs_os_thread_cfg_t* cfg, void (*fn)(void*), void* arg);
/* Release a thread whose fn has returned (joins it and frees its stack). */
void mcs_os_thread_free(mcs_os_thread_t* t);

void mcs_os_sleep(uint32_t ms);         /* blocks the calling thread only */
void mcs_os_yield(void);
uint32_t mcs_os_ticks(void);            /* monotonic milliseconds */

mcs_os_mutex_t* mcs_os_mutex_new(void);
void mcs_os_mutex_lock(mcs_os_mutex_t* m);
void mcs_os_mutex_unlock(mcs_os_mutex_t* m);
void mcs_os_mutex_free(mcs_os_mutex_t* m);

mcs_os_sem_t* mcs_os_sem_new(unsigned initial, unsigned max);
bool mcs_os_sem_take(mcs_os_sem_t* s, uint32_t timeout_ms);  /* false on timeout */
void mcs_os_sem_give(mcs_os_sem_t* s);
void mcs_os_sem_free(mcs_os_sem_t* s);

/* thread-safe heap of the OS (pvPortMalloc / k_malloc / malloc) */
void* mcs_os_malloc(size_t n);
void mcs_os_free(void* p);

/* Short critical section that also excludes interrupt handlers and the other cores
 * (HAL event queue on multi-core parts). Never block inside. */
unsigned mcs_os_irq_lock(void);
void mcs_os_irq_unlock(unsigned key);
/* non-zero when the OS runs threads on more than one core */
int mcs_os_smp(void);

#endif /* MCS_ENABLE_THREADS */

#ifdef __cplusplus
}
#endif
#endif
