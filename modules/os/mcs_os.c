/* MicroCS - OS layer (mcs_os.h) for FreeRTOS, Zephyr and POSIX threads.
 * Compiled to nothing unless an OS is chosen with MCS_OS (default: none). */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE                 /* sched_getcpu, pthread_setaffinity_np */
#endif
#include "mcs_os.h"
#if MCS_ENABLE_THREADS
#include <string.h>

/* ================================================================ FreeRTOS */
#if MCS_OS == MCS_OS_FREERTOS
#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#elif defined(__has_include) && !__has_include("FreeRTOS.h")
#error "MicroCS: MCS_OS=MCS_OS_FREERTOS but FreeRTOS.h is not on the include path. Tell the build where FreeRTOS is: CMake -DMICROCS_FREERTOS_PATH=<FreeRTOS-Kernel> -DMICROCS_FREERTOS_PORT=<portable/GCC/ARM_CM4F> -DMICROCS_FREERTOS_CONFIG_DIR=<folder of FreeRTOSConfig.h>, make FREERTOS_PATH=... FREERTOS_PORT=... FREERTOS_CONFIG_DIR=..., or add <FreeRTOS-Kernel>/include, the port folder and the FreeRTOSConfig.h folder to your IDE include paths. Or build without an OS (MCS_OS_NONE, the default). See docs/THREADS.md"
#elif defined(__has_include) && !__has_include("FreeRTOSConfig.h")
#error "MicroCS: MCS_OS=MCS_OS_FREERTOS but FreeRTOSConfig.h is not on the include path: add the folder of your project's FreeRTOSConfig.h (CMake -DMICROCS_FREERTOS_CONFIG_DIR=..., make FREERTOS_CONFIG_DIR=...). See docs/THREADS.md"
#else
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#endif
#if defined(INC_FREERTOS_H) && (!configUSE_MUTEXES || !configUSE_COUNTING_SEMAPHORES)
#error "MicroCS threads need configUSE_MUTEXES 1 and configUSE_COUNTING_SEMAPHORES 1 in FreeRTOSConfig.h"
#endif
#if defined(INC_FREERTOS_H)    /* else only the message above */
#if defined(configNUMBER_OF_CORES)
#define MCS_RTOS_CORES configNUMBER_OF_CORES
#elif defined(configNUM_CORES)
#define MCS_RTOS_CORES configNUM_CORES          /* FreeRTOS SMP branch (older pico-sdk examples) */
#elif defined(ESP_PLATFORM)
#define MCS_RTOS_CORES portNUM_PROCESSORS
#else
#define MCS_RTOS_CORES 1
#endif

struct mcs_os_thread { TaskHandle_t h; void (*fn)(void*); void* arg; volatile int finished; int core; };
struct mcs_os_mutex { SemaphoreHandle_t s; };
struct mcs_os_sem { SemaphoreHandle_t s; };

static TickType_t to_ticks(uint32_t ms) {
    if (ms == MCS_OS_FOREVER) return portMAX_DELAY;
    uint64_t t = ((uint64_t)ms * configTICK_RATE_HZ + 999u) / 1000u;   /* round up: never wait less */
    return t >= (uint64_t)portMAX_DELAY ? portMAX_DELAY - 1 : (TickType_t)t;
}

const char* mcs_os_name(void) { return "FreeRTOS"; }
int mcs_os_cores(void) { return MCS_RTOS_CORES; }
int mcs_os_core(void) {
#if defined(ESP_PLATFORM)
    return (int)xPortGetCoreID();
#elif MCS_RTOS_CORES > 1
    return (int)portGET_CORE_ID();
#else
    return 0;
#endif
}
int mcs_os_smp(void) { return MCS_RTOS_CORES > 1; }

static void task_entry(void* p) {
    mcs_os_thread_t* t = (mcs_os_thread_t*)p;
    t->fn(t->arg);
    t->finished = 1;
    vTaskDelete(NULL);
}

mcs_os_thread_t* mcs_os_thread_start(const mcs_os_thread_cfg_t* cfg, void (*fn)(void*), void* arg) {
    if (cfg->core >= MCS_RTOS_CORES) return NULL;
    mcs_os_thread_t* t = (mcs_os_thread_t*)pvPortMalloc(sizeof *t);
    if (!t) return NULL;
    memset(t, 0, sizeof *t);
    t->fn = fn; t->arg = arg; t->core = cfg->core;
    uint32_t bytes = cfg->stack_bytes ? cfg->stack_bytes : MCS_THREAD_STACK;
    long prio = (long)uxTaskPriorityGet(NULL) + cfg->priority;
    if (prio < (long)tskIDLE_PRIORITY + 1) prio = (long)tskIDLE_PRIORITY + 1;
    if (prio > configMAX_PRIORITIES - 1) prio = configMAX_PRIORITIES - 1;
    const char* name = cfg->name ? cfg->name : "mcs";
    BaseType_t ok;
#if defined(ESP_PLATFORM)            /* ESP-IDF: stack depth in bytes */
    ok = xTaskCreatePinnedToCore(task_entry, name, bytes, t, (UBaseType_t)prio, &t->h,
                                 cfg->core < 0 ? tskNO_AFFINITY : (BaseType_t)cfg->core);
#else
    configSTACK_DEPTH_TYPE words = (configSTACK_DEPTH_TYPE)((bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t));
#if MCS_RTOS_CORES > 1 && defined(configUSE_CORE_AFFINITY) && configUSE_CORE_AFFINITY
    UBaseType_t mask = cfg->core < 0 ? (UBaseType_t)tskNO_AFFINITY : (UBaseType_t)(1u << cfg->core);
    ok = xTaskCreateAffinitySet(task_entry, name, words, t, (UBaseType_t)prio, mask, &t->h);
#else
    if (cfg->core > 0) { vPortFree(t); return NULL; }   /* single core, or SMP without configUSE_CORE_AFFINITY */
    ok = xTaskCreate(task_entry, name, words, t, (UBaseType_t)prio, &t->h);
#endif
#endif
    if (ok != pdPASS) { vPortFree(t); return NULL; }
    return t;
}
void mcs_os_thread_free(mcs_os_thread_t* t) {
    if (!t) return;
    while (!t->finished) vTaskDelay(1);    /* fn returned; the task is deleting itself */
    vPortFree(t);
}

void mcs_os_sleep(uint32_t ms) { if (ms) vTaskDelay(to_ticks(ms)); else taskYIELD(); }
void mcs_os_yield(void) { taskYIELD(); }
uint32_t mcs_os_ticks(void) { return (uint32_t)((uint64_t)xTaskGetTickCount() * 1000u / configTICK_RATE_HZ); }

mcs_os_mutex_t* mcs_os_mutex_new(void) {
    mcs_os_mutex_t* m = (mcs_os_mutex_t*)pvPortMalloc(sizeof *m);
    if (m && !(m->s = xSemaphoreCreateMutex())) { vPortFree(m); m = NULL; }
    return m;
}
void mcs_os_mutex_lock(mcs_os_mutex_t* m) { xSemaphoreTake(m->s, portMAX_DELAY); }
void mcs_os_mutex_unlock(mcs_os_mutex_t* m) { xSemaphoreGive(m->s); }
void mcs_os_mutex_free(mcs_os_mutex_t* m) { if (m) { vSemaphoreDelete(m->s); vPortFree(m); } }

mcs_os_sem_t* mcs_os_sem_new(unsigned initial, unsigned max) {
    mcs_os_sem_t* s = (mcs_os_sem_t*)pvPortMalloc(sizeof *s);
    if (s && !(s->s = xSemaphoreCreateCounting(max, initial))) { vPortFree(s); s = NULL; }
    return s;
}
bool mcs_os_sem_take(mcs_os_sem_t* s, uint32_t ms) { return xSemaphoreTake(s->s, to_ticks(ms)) == pdTRUE; }
void mcs_os_sem_give(mcs_os_sem_t* s) { xSemaphoreGive(s->s); }
void mcs_os_sem_free(mcs_os_sem_t* s) { if (s) { vSemaphoreDelete(s->s); vPortFree(s); } }

void* mcs_os_malloc(size_t n) { return pvPortMalloc(n); }
void mcs_os_free(void* p) { if (p) vPortFree(p); }

#if defined(ESP_PLATFORM)
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
unsigned mcs_os_irq_lock(void) { portENTER_CRITICAL_SAFE(&g_mux); return 0; }
void mcs_os_irq_unlock(unsigned key) { (void)key; portEXIT_CRITICAL_SAFE(&g_mux); }
#elif MCS_RTOS_CORES == 1 && defined(__GNUC__) && (defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || \
      defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__))
/* single core Cortex-M: PRIMASK also holds off interrupts above configMAX_SYSCALL_INTERRUPT_PRIORITY */
unsigned mcs_os_irq_lock(void) { uint32_t k; __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(k) :: "memory"); return k; }
void mcs_os_irq_unlock(unsigned key) { __asm volatile("msr primask, %0" :: "r"(key) : "memory"); }
#else
unsigned mcs_os_irq_lock(void) { return (unsigned)taskENTER_CRITICAL_FROM_ISR(); }
void mcs_os_irq_unlock(unsigned key) { taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)key); (void)key; }
#endif

#endif /* INC_FREERTOS_H */
/* ================================================================ Zephyr */
#elif MCS_OS == MCS_OS_ZEPHYR
#if !defined(__ZEPHYR__)
#error "MicroCS: MCS_OS=MCS_OS_ZEPHYR outside a Zephyr build. Build with west (ZEPHYR_BASE set, MicroCS as a module: -DZEPHYR_EXTRA_MODULES=<MicroCS>) and CONFIG_MICROCS_THREADS=y, or build without an OS (MCS_OS_NONE, the default). See docs/THREADS.md"
#endif
#if defined(__ZEPHYR__)    /* else only the message above */
#include <zephyr/kernel.h>
#if !defined(CONFIG_DYNAMIC_THREAD)
#error "MicroCS threads on Zephyr need CONFIG_DYNAMIC_THREAD=y (CONFIG_MICROCS_THREADS selects it)"
#endif

struct mcs_os_thread { struct k_thread th; k_thread_stack_t* stack; int core; };
struct mcs_os_mutex { struct k_mutex m; };
struct mcs_os_sem { struct k_sem s; };

static k_timeout_t to_timeout(uint32_t ms) { return ms == MCS_OS_FOREVER ? K_FOREVER : K_MSEC(ms); }

const char* mcs_os_name(void) { return "Zephyr"; }
int mcs_os_cores(void) {
#if defined(CONFIG_SMP)
    return (int)arch_num_cpus();
#else
    return 1;
#endif
}
int mcs_os_core(void) {
#if defined(CONFIG_SMP)
    unsigned k = irq_lock();               /* no migration while reading the CPU id */
    int id = (int)arch_curr_cpu()->id;
    irq_unlock(k);
    return id;
#else
    return 0;
#endif
}
int mcs_os_smp(void) { return mcs_os_cores() > 1; }

static void thread_entry(void* a, void* b, void* c) { (void)c; ((void (*)(void*))a)(b); }

mcs_os_thread_t* mcs_os_thread_start(const mcs_os_thread_cfg_t* cfg, void (*fn)(void*), void* arg) {
    if (cfg->core >= mcs_os_cores()) return NULL;
#if !defined(CONFIG_SCHED_CPU_MASK)
    if (cfg->core > 0) return NULL;        /* pinning needs CONFIG_SCHED_CPU_MASK=y */
#endif
    mcs_os_thread_t* t = (mcs_os_thread_t*)k_malloc(sizeof *t);
    if (!t) return NULL;
    memset(t, 0, sizeof *t);
    size_t bytes = cfg->stack_bytes ? cfg->stack_bytes : MCS_THREAD_STACK;
    t->stack = k_thread_stack_alloc(bytes, 0);
    if (!t->stack) { k_free(t); return NULL; }
    int prio = k_thread_priority_get(k_current_get());
    if (prio >= 0) {                        /* preemptible creator: stay preemptible */
        prio -= cfg->priority;
        if (prio < 0) prio = 0;
        if (prio > CONFIG_NUM_PREEMPT_PRIORITIES - 1) prio = CONFIG_NUM_PREEMPT_PRIORITIES - 1;
    }
    t->core = cfg->core;
    k_thread_create(&t->th, t->stack, bytes, thread_entry, (void*)fn, arg, NULL, prio, 0, K_FOREVER);
#if defined(CONFIG_THREAD_NAME)
    k_thread_name_set(&t->th, cfg->name ? cfg->name : "mcs");
#endif
#if defined(CONFIG_SCHED_CPU_MASK)
    if (cfg->core >= 0) k_thread_cpu_pin(&t->th, cfg->core);
#endif
    k_thread_start(&t->th);
    return t;
}
void mcs_os_thread_free(mcs_os_thread_t* t) {
    if (!t) return;
    k_thread_join(&t->th, K_FOREVER);
    k_thread_stack_free(t->stack);
    k_free(t);
}

void mcs_os_sleep(uint32_t ms) { if (ms) k_msleep((int32_t)ms); else k_yield(); }
void mcs_os_yield(void) { k_yield(); }
uint32_t mcs_os_ticks(void) { return k_uptime_get_32(); }

mcs_os_mutex_t* mcs_os_mutex_new(void) {
    mcs_os_mutex_t* m = (mcs_os_mutex_t*)k_malloc(sizeof *m);
    if (m) k_mutex_init(&m->m);
    return m;
}
void mcs_os_mutex_lock(mcs_os_mutex_t* m) { k_mutex_lock(&m->m, K_FOREVER); }
void mcs_os_mutex_unlock(mcs_os_mutex_t* m) { k_mutex_unlock(&m->m); }
void mcs_os_mutex_free(mcs_os_mutex_t* m) { k_free(m); }

mcs_os_sem_t* mcs_os_sem_new(unsigned initial, unsigned max) {
    mcs_os_sem_t* s = (mcs_os_sem_t*)k_malloc(sizeof *s);
    if (s) k_sem_init(&s->s, initial, max);
    return s;
}
bool mcs_os_sem_take(mcs_os_sem_t* s, uint32_t ms) { return k_sem_take(&s->s, to_timeout(ms)) == 0; }
void mcs_os_sem_give(mcs_os_sem_t* s) { k_sem_give(&s->s); }
void mcs_os_sem_free(mcs_os_sem_t* s) { k_free(s); }

void* mcs_os_malloc(size_t n) { return k_malloc(n); }
void mcs_os_free(void* p) { k_free(p); }

static struct k_spinlock g_lock;
unsigned mcs_os_irq_lock(void) { k_spinlock_key_t k = k_spin_lock(&g_lock); return (unsigned)k.key; }
void mcs_os_irq_unlock(unsigned key) { k_spinlock_key_t k; k.key = (int)key; k_spin_unlock(&g_lock, k); }

#endif /* __ZEPHYR__ */
/* ================================================================ POSIX */
#elif MCS_OS == MCS_OS_POSIX
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

struct mcs_os_thread { pthread_t th; int core; };
struct mcs_os_mutex { pthread_mutex_t m; };
struct mcs_os_sem { pthread_mutex_t m; pthread_cond_t c; unsigned count, max; };

const char* mcs_os_name(void) { return "POSIX"; }
int mcs_os_cores(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
int mcs_os_core(void) {
#if defined(__linux__)
    int c = sched_getcpu();
    return c < 0 ? 0 : c;
#else
    return 0;
#endif
}
int mcs_os_smp(void) { return mcs_os_cores() > 1; }

typedef struct { void (*fn)(void*); void* arg; } start_t;
static void* thread_entry(void* p) {
    start_t s = *(start_t*)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}
mcs_os_thread_t* mcs_os_thread_start(const mcs_os_thread_cfg_t* cfg, void (*fn)(void*), void* arg) {
    if (cfg->core >= mcs_os_cores()) return NULL;
    mcs_os_thread_t* t = (mcs_os_thread_t*)malloc(sizeof *t);
    start_t* s = (start_t*)malloc(sizeof *s);
    if (!t || !s) { free(t); free(s); return NULL; }
    s->fn = fn; s->arg = arg; t->core = cfg->core;
    pthread_attr_t a;
    pthread_attr_init(&a);
    /* host C code (and sanitizers) need more than an MCU task: at least 512 KB */
    size_t bytes = cfg->stack_bytes ? cfg->stack_bytes : MCS_THREAD_STACK;
    if (bytes < 512u * 1024u) bytes = 512u * 1024u;
    pthread_attr_setstacksize(&a, bytes);
    int e = pthread_create(&t->th, &a, thread_entry, s);
    pthread_attr_destroy(&a);
    if (e) { free(t); free(s); return NULL; }
#if defined(__linux__)
    if (cfg->core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(cfg->core, &set);
        pthread_setaffinity_np(t->th, sizeof set, &set);    /* best effort (containers may refuse) */
    }
#endif
    return t;
}
void mcs_os_thread_free(mcs_os_thread_t* t) {
    if (!t) return;
    pthread_join(t->th, NULL);
    free(t);
}

void mcs_os_sleep(uint32_t ms) {
    if (!ms) { sched_yield(); return; }
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) && errno == EINTR) {}
}
void mcs_os_yield(void) { sched_yield(); }
uint32_t mcs_os_ticks(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

mcs_os_mutex_t* mcs_os_mutex_new(void) {
    mcs_os_mutex_t* m = (mcs_os_mutex_t*)malloc(sizeof *m);
    if (m) pthread_mutex_init(&m->m, NULL);
    return m;
}
void mcs_os_mutex_lock(mcs_os_mutex_t* m) { pthread_mutex_lock(&m->m); }
void mcs_os_mutex_unlock(mcs_os_mutex_t* m) { pthread_mutex_unlock(&m->m); }
void mcs_os_mutex_free(mcs_os_mutex_t* m) { if (m) { pthread_mutex_destroy(&m->m); free(m); } }

mcs_os_sem_t* mcs_os_sem_new(unsigned initial, unsigned max) {
    mcs_os_sem_t* s = (mcs_os_sem_t*)malloc(sizeof *s);
    if (!s) return NULL;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    s->count = initial; s->max = max;
    return s;
}
bool mcs_os_sem_take(mcs_os_sem_t* s, uint32_t ms) {
    struct timespec until;
    if (ms != MCS_OS_FOREVER) {
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += (time_t)(ms / 1000);
        until.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
    }
    pthread_mutex_lock(&s->m);
    int e = 0;
    while (!s->count && e != ETIMEDOUT)
        e = ms == MCS_OS_FOREVER ? pthread_cond_wait(&s->c, &s->m) : pthread_cond_timedwait(&s->c, &s->m, &until);
    bool got = s->count > 0;
    if (got) s->count--;
    pthread_mutex_unlock(&s->m);
    return got;
}
void mcs_os_sem_give(mcs_os_sem_t* s) {
    pthread_mutex_lock(&s->m);
    if (s->count < s->max) s->count++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
}
void mcs_os_sem_free(mcs_os_sem_t* s) {
    if (!s) return;
    pthread_cond_destroy(&s->c); pthread_mutex_destroy(&s->m); free(s);
}

void* mcs_os_malloc(size_t n) { return malloc(n); }
void mcs_os_free(void* p) { free(p); }

static pthread_mutex_t g_irq = PTHREAD_MUTEX_INITIALIZER;
unsigned mcs_os_irq_lock(void) { pthread_mutex_lock(&g_irq); return 0; }
void mcs_os_irq_unlock(unsigned key) { (void)key; pthread_mutex_unlock(&g_irq); }

#else
#error "MicroCS: unknown MCS_OS (MCS_OS_NONE, MCS_OS_FREERTOS, MCS_OS_ZEPHYR, MCS_OS_POSIX or MCS_OS_AUTO)"
#endif
#else
typedef int mcs_os_unused_t;   /* no OS chosen: empty unit */
#endif
