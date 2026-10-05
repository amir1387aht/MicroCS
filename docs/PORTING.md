# Porting MicroCS

MicroCS needs a C99 compiler, ~170–210 KB of flash (full build incl. newlib/libm) or
~165 KB (runtime only), and RAM for: the VM heap (≥ ~50 KB baseline + script working
set; ~150–170 KB when compiling scripts on the device), ~3–8 KB of C stack for the VM
task, and static module state. Numbers: `docs/PERFORMANCE.md`.

## Files to compile
`src/*.c` (core) + the modules you want from `modules/*/` (drop `mcs_vfs_posix.c` on MCUs;
`mcs_vfs_lfs.c` needs littlefs and `-DMCS_ENABLE_LFS=1`). Include path: `include/`.
Choose a profile: `-DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_embedded.h"'`.

## Host hooks
| Hook | Bare metal | RT-Thread | FreeRTOS | Zephyr |
|---|---|---|---|---|
| `realloc_fn` | `mcs_pool_realloc` on a static buffer (recommended everywhere) | same, or `rt_realloc` wrapper | same | same |
| `write_fn` | UART TX | `rt_device_write(console, ...)` / `rt_kprintf("%.*s")` | UART driver | `printk` / UART API |
| `ticks_fn` | SysTick ms | `rt_tick_get_millisecond()` | `xTaskGetTickCount()*portTICK_PERIOD_MS` | `k_uptime_get_32()` |
| `delay_fn` | busy wait / WFI | `rt_thread_mdelay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` | `k_msleep(ms)` |
| `hook_fn` | feed watchdog, poll break flag | `rt_wdt` feed, `rt_thread_yield()` | `taskYIELD()` | `k_yield()` |

Run each VM in one task/thread; the VM is not thread-safe. `mcs_request_abort()` may be
called from an ISR or another task to stop a script at the next safepoint.

## Reference: bare-metal Cortex-M (`ports/cortex-m/`) — experimental
* `startup.c` (vector table, .data/.bss init, stack painting, FPU enable), `cm.ld`,
  `board.c` (UART/tick drivers for the emulator's virtual board + newlib stubs), `main.c`
  (pool heap, VM, RAM FS, HAL simulator, scheduler, demo image, optional shell on UART).
* Targets (`make cm`): `m0-runtime` (Cortex-M0, no compiler), `m4-full` (M4F), `m33-full`
  and `m33-shell` (M33 with FPv5, sized like a 512 KB-SRAM part).
* `make cm-check` runs every target in the Unicorn emulator (`tools/cm_emu.py`) and requires
  the script output to be byte-identical to the host interpreter, then runs the UART
  protocol test against `m33-shell`. **This validates code generation, ABI, alignment,
  soft/hard float and memory budgets for these cores — not real peripherals, flash wait
  states, caches or interrupts.** No physical board has been tested.
* To move to real hardware replace `board.c` with your vendor UART/SysTick code, change the
  memory map in `cm.ld` (or use the vendor's linker script), and fill an `mcs_hal_t` with
  real drivers.

## SiFli SF32LB525 + RT-Thread (planned — integration outline, untested)
The SiFli SDK is RT-Thread based and its SF32LB52x parts use a Cortex-M33-class core with
on the order of 512 KB SRAM (check the exact variant's datasheet/memory map). Suggested
integration:
1. Add `src/*.c`, `modules/{fs,hal,sched,shell}/*.c` (minus `mcs_vfs_posix.c`) to the
   project's SConscript; `CPPDEFINES += MCS_USER_CONFIG_FILE=\"profiles/mcs_profile_embedded.h\"`.
2. Create an RT-Thread thread (stack 8 KB to start; measured peak for the demo is 2.7 KB)
   that owns the VM and calls `mcs_sched_poll` / `mcs_shell_step`.
3. Static pool of 192–256 KB for the VM heap if scripts are compiled on the device; ~100 KB
   if only `.mcsb` images are run.
4. Filesystem: either mount LittleFS directly on the SPI/NOR flash via `mcs_lfs_ops`, or
   write a small VFS backend over RT-Thread DFS (`open/read/write/stat/opendir`, ~150
   lines, modelled on `mcs_vfs_posix.c`).
5. HAL: map `mcs_hal_t` to `rt_pin_mode/rt_pin_write/rt_pin_read`, `rt_device_find("uartX")`
   + `rt_device_read/write`, `rt_i2c_transfer`, `rt_spi_transfer`, `rt_adc_read`,
   `rt_pwm_set`.
6. Shell transport: the console UART or a dedicated UART/USB-CDC device.
7. LVGL: expose widgets as a native module (`mcs_define_class` with userdata wrapping
   `lv_obj_t*`); the VM must run in the LVGL thread or post requests to it. Planned.
