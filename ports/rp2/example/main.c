/* MicroCS on a Raspberry Pi Pico: the whole firmware is this file.
 * Connect a serial terminal to the USB port (115200, any setting works) and
 * type C# at the "> " prompt. Files live in a RAM disk here; see
 * docs/STANDALONE.md for LittleFS on the on-board flash. */
#include "pico/stdlib.h"
#include "mcs_runtime.h"
#include "mcs_port_rp2.h"

static uint8_t heap[160 * 1024] __attribute__((aligned(8)));
static mcs_runtime_t rt;
static mcs_hal_t hal;

int main(void) {
    stdio_init_all();
    mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;
    pins.name = "Raspberry Pi Pico";
    mcs_rp2_hal_init(&hal, &pins);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = sizeof heap;
    cfg.ramfs_size = 32 * 1024;
    cfg.console = mcs_rp2_console_stdio();
    cfg.ticks = mcs_rp2_ticks;
    cfg.delay = mcs_rp2_delay;
    cfg.hal = &hal;
    for (;;) mcs_runtime_run(&rt, &cfg);     /* restarts if the session ends */
}
