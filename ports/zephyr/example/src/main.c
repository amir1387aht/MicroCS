/* MicroCS on any Zephyr board: a C# REPL on the console (UART or USB), files
 * on LittleFS (boot.cs / jobs.cfg / main.cs run at start-up), every peripheral
 * from the devicetree, and the machine protocol MicroCS Studio and
 * tools/mcs_remote.py use (Ctrl-A on the console). */
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include "mcs_runtime.h"
#include "mcs_port_zephyr.h"
#if defined(CONFIG_MICROCS_YAFFS2)
#include <zephyr/storage/flash_map.h>
#endif

static uint8_t heap[CONFIG_MICROCS_HEAP_SIZE] __aligned(8);
static mcs_runtime_t rt;
static mcs_hal_t hal;
#if defined(CONFIG_MICROCS_YAFFS2)
static mcs_zephyr_flash_t flash;
static mcs_flashfs_t flashfs;
#elif defined(CONFIG_MICROCS_FS)
static mcs_zephyr_fs_t files;
#endif

static void say(const mcs_transport_t* con, const char* s) { con->write(con->ud, s, strlen(s)); }

int main(void) {
#if DT_HAS_CHOSEN(zephyr_console) && DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart) && defined(CONFIG_UART_LINE_CTRL)
    /* USB console: wait (up to 3 s) for the host to open the port so the banner is not lost */
    const struct device* usb = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    for (int i = 0; i < 300; i++) {
        uint32_t dtr = 0;
        if (uart_line_ctrl_get(usb, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr) break;
        k_msleep(10);
    }
#endif
    mcs_zephyr_hal_init(&hal, NULL);
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = sizeof heap;
    cfg.console = mcs_zephyr_console();
    cfg.ticks = mcs_zephyr_ticks;
    cfg.delay = mcs_zephyr_delay;
    cfg.echo = IS_ENABLED(CONFIG_MICROCS_CONSOLE_ECHO);
    cfg.hal = &hal;
#if defined(CONFIG_MICROCS_YAFFS2)
#if FIXED_PARTITION_EXISTS(storage_partition)
    if (mcs_zephyr_flash_area_init(&flash, FIXED_PARTITION_ID(storage_partition)) == 0 &&
        mcs_flashfs_mount(&flashfs, &flash.flash, 0, 0, MCS_FLASHFS_YAFFS2, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
        cfg.fs_ops = flashfs.ops;
        cfg.fs_ctx = flashfs.ctx;
    } else {
        say(&cfg.console, "MicroCS: YAFFS2 mount failed - using a RAM filesystem\r\n");
        cfg.ramfs_size = CONFIG_MICROCS_RAMFS_SIZE;
    }
#else
    cfg.ramfs_size = CONFIG_MICROCS_RAMFS_SIZE;           /* no storage_partition on this board */
#endif
#elif defined(CONFIG_MICROCS_FS)
    int e = mcs_zephyr_fs_mount(&files);
    if (e == 0) {
        cfg.fs_ops = &mcs_zephyr_fs_ops;
        cfg.fs_ctx = &files;
    } else {
        say(&cfg.console, "MicroCS: no storage partition - using a RAM filesystem\r\n");
        cfg.ramfs_size = CONFIG_MICROCS_RAMFS_SIZE;
    }
#else
    cfg.ramfs_size = CONFIG_MICROCS_RAMFS_SIZE;
#endif
    for (;;) mcs_runtime_run(&rt, &cfg);     /* restarts the VM if the console ever closes */
    return 0;
}
