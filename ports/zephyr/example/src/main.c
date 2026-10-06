/* MicroCS on any Zephyr board: C# REPL on the console UART + all peripherals. */
#include <zephyr/kernel.h>
#include "mcs_runtime.h"
#include "mcs_port_zephyr.h"

static uint8_t heap[96 * 1024] __aligned(8);
static mcs_runtime_t rt;
static mcs_hal_t hal;

int main(void) {
    mcs_zephyr_hal_init(&hal, NULL);
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = sizeof heap;
    cfg.ramfs_size = 16 * 1024;
    cfg.console = mcs_zephyr_console();
    cfg.ticks = mcs_zephyr_ticks;
    cfg.delay = mcs_zephyr_delay;
    cfg.hal = &hal;
    for (;;) mcs_runtime_run(&rt, &cfg);
    return 0;
}
