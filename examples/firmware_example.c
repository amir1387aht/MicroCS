/*
 * MicroCS - example firmware integration (runs on the host for demonstration).
 *
 * Shows:
 *   - a static pool heap instead of malloc (deterministic RAM budget)
 *   - output / tick / delay / watchdog hooks
 *   - wrapping plain C HAL functions with mcs_bind.h one-liners
 *   - a native class (Uart) with per-instance C data and a finalizer
 *   - storing a script callback (lambda) in C and calling it from the
 *     main loop (e.g. after an interrupt set a flag)
 *   - running a precompiled bytecode image linked as a const array
 *     (generated with:  mcs -C app.cs -n app_image -o app_image.h)
 *   - calling script functions (App.Setup / App.Loop) from C
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mcs.h"
#include "mcs_bind.h"
#include "app_image.h"

/* ------------------------------------------------------------------ fake HAL */
static int g_led_state;
static void hal_gpio_mode(long pin, long mode) { printf("[hal] gpio %ld mode %ld\n", pin, mode); }
static void hal_gpio_write(long pin, long level) { g_led_state = (int)level; printf("[hal] gpio %ld <- %ld\n", pin, level); }
static int hal_gpio_read(long pin) { return pin == 0 ? g_led_state : 0; }
static double hal_read_temp(void) { static double t = 21.5; t += 0.25; return t; }
static uint32_t hal_millis(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
static int g_wdt_feeds;
static void hal_watchdog_feed(void) { g_wdt_feeds++; }

/* ------------------------------------------------------- one-line C wrappers */
MCS_WRAP_V_II(w_gpio_mode, hal_gpio_mode)
MCS_WRAP_V_II(w_gpio_write, hal_gpio_write)
MCS_WRAP_I_I(w_gpio_read, hal_gpio_read)
MCS_WRAP_F_V(w_read_temp, hal_read_temp)

static const mcs_reg_t gpio_fns[] = {
    MCS_FN("Mode", w_gpio_mode, 2),
    MCS_FN("Write", w_gpio_write, 2),
    MCS_FN("Read", w_gpio_read, 1),
    MCS_REG_END
};
static const mcs_reg_t sensor_fns[] = { MCS_FN("ReadTemp", w_read_temp, 0), MCS_REG_END };

/* ------------------------------------------- hand-written native: callbacks */
static int g_button_cb = -1; /* pin handle of the script lambda */

static mcs_value_t board_on_button(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    if (g_button_cb >= 0) mcs_unpin(vm, g_button_cb);
    g_button_cb = mcs_pin(vm, argv[0]); /* keep the delegate alive while C holds it */
    if (g_button_cb < 0) mcs_raise(vm, "InvalidOperationException", "too many pinned callbacks");
    return mcs_null();
}
static mcs_value_t board_name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; (void)argv;
    return mcs_string(vm, "SF32LB525-devkit (simulated)");
}
static const mcs_reg_t board_fns[] = {
    MCS_FN("OnButton", board_on_button, 1),
    MCS_GET("Name", board_name),        /* static property: Board.Name */
    MCS_REG_END
};

/* ------------------------------------------------ native class: Uart */
typedef struct { int port; int baud; int open; uint32_t tx_bytes; } uart_t;
static const mcs_class_def_t uart_def; /* forward */

static void uart_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    uart_t* u = (uart_t*)mcs_userdata(self);
    if (argc != 2) { mcs_raise(vm, "ArgumentException", "Uart(port, baud) expected"); return; }
    u->port = (int)mcs_to_int(vm, argv[0]);
    u->baud = (int)mcs_to_int(vm, argv[1]);
    u->open = 1;
    printf("[hal] uart%d open @ %d\n", u->port, u->baud);
}
static void uart_finalize(mcs_vm_t* vm, void* data) {
    (void)vm;
    uart_t* u = (uart_t*)data;
    if (u->open) printf("[hal] uart%d closed by GC (%u bytes sent)\n", u->port, (unsigned)u->tx_bytes);
}
static mcs_value_t uart_write_line(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)argc;
    uart_t* u = (uart_t*)mcs_check_userdata(vm, self, &uart_def);
    const char* s = mcs_to_cstr(vm, argv[0]);
    if (!u || mcs_has_exception(vm)) return mcs_null();
    u->tx_bytes += (uint32_t)strlen(s) + 2;
    printf("[uart%d] %s\n", u->port, s);
    return mcs_null();
}
static mcs_value_t uart_available(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)argc; (void)argv;
    uart_t* u = (uart_t*)mcs_check_userdata(vm, self, &uart_def);
    return mcs_int(u ? (mcs_int_t)(u->tx_bytes % 7) : 0);
}
static mcs_value_t uart_close(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)argc; (void)argv;
    uart_t* u = (uart_t*)mcs_check_userdata(vm, self, &uart_def);
    if (u) u->open = 0;
    return mcs_null();
}
static const mcs_reg_t uart_members[] = {
    MCS_FN("WriteLine", uart_write_line, 1),
    MCS_GET("Available", uart_available),
    MCS_FN("Dispose", uart_close, 0),   /* works with `using` */
    MCS_REG_END
};
static const mcs_class_def_t uart_def = {
    "Uart", sizeof(uart_t), uart_ctor, uart_finalize, uart_members, NULL
};

/* ------------------------------------------------------------ port hooks */
static void out_write(void* ud, const char* s, size_t n) { (void)ud; fwrite(s, 1, n, stdout); }
static uint32_t ticks(void* ud) { (void)ud; return hal_millis(); }
static void delay(void* ud, uint32_t ms) {
    (void)ud;
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
static int hook(mcs_vm_t* vm, void* ud) { (void)vm; (void)ud; hal_watchdog_feed(); return 0; }

/* -------------------------------------------------------------- firmware */
static uint8_t g_heap[96 * 1024] __attribute__((aligned(8)));
static mcs_pool_t g_pool;

static int check(mcs_vm_t* vm, mcs_result_t r, const char* what) {
    if (r != MCS_OK) { printf("%s failed: %s\n", what, mcs_last_error(vm)); return 0; }
    return 1;
}

int main(void) {
    mcs_pool_init(&g_pool, g_heap, sizeof g_heap);

    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = mcs_pool_realloc;
    cfg.alloc_ud = &g_pool;
    cfg.heap_limit = sizeof g_heap - 4096;   /* keep a little slack for fragmentation */
    cfg.write_fn = out_write;
    cfg.ticks_fn = ticks;
    cfg.delay_fn = delay;
    cfg.hook_fn = hook;
    cfg.stack_slots = 256;
    cfg.max_frames = 48;

    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) { printf("cannot create VM\n"); return 1; }

    /* bindings */
    mcs_register_module(vm, "Gpio", gpio_fns);
    MCS_CONST_INT(vm, "Gpio", "INPUT", 0);
    MCS_CONST_INT(vm, "Gpio", "OUTPUT", 1);
    mcs_register_module(vm, "Sensor", sensor_fns);
    mcs_register_module(vm, "Board", board_fns);
    MCS_CONST_INT(vm, "Board", "LED", 0);
    mcs_register_class(vm, &uart_def);

    /* load the precompiled application from "flash" */
    if (!check(vm, mcs_exec_image(vm, app_image, app_image_len), "load image")) return 1;
    if (!check(vm, mcs_call(vm, "App.Setup", 0, NULL, NULL), "App.Setup")) return 1;

    /* super loop */
    for (int i = 0; i < 6; i++) {
        if (!check(vm, mcs_call(vm, "App.Loop", 0, NULL, NULL), "App.Loop")) break;
        if (i == 2 || i == 4) {           /* pretend a GPIO interrupt set a flag */
            mcs_value_t arg = mcs_bool(i == 2);
            mcs_value_t cb = mcs_pinned(vm, g_button_cb);
            if (!mcs_is_null(cb)) check(vm, mcs_call_value(vm, cb, 1, &arg, NULL), "button callback");
        }
        delay(NULL, 100);
    }

    /* run some source at runtime too (on-device compiler, e.g. from a REPL) */
#if MCS_ENABLE_COMPILER
    check(vm, mcs_exec_source(vm, "console", "Console.WriteLine($\"stats from C#: {App.Stats()}\");"), "exec");
#endif

    mcs_value_t res;
    if (check(vm, mcs_call(vm, "App.Stats", 0, NULL, &res), "App.Stats"))
        printf("[c] App.Stats() = %ld\n", (long)res.as.i);

    mcs_mem_stats_t st;
    mcs_gc(vm);
    mcs_mem_stats(vm, &st);
    printf("[c] heap in use %zu B (peak %zu B, pool peak %zu B of %zu), %u objects, %u GCs, watchdog fed %d times\n",
           st.bytes_in_use, st.peak_bytes, g_pool.peak, sizeof g_heap, st.objects, st.collections, g_wdt_feeds);
    mcs_free(vm);
    return 0;
}
