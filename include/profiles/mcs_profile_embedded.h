/* MicroCS build profile: embedded standalone runtime (on-device compiler,
 * filesystem, HAL, scheduler, script-manager shell). Intended for
 * Cortex-M33/M7 or ESP32-class parts with 256 KB+ SRAM (e.g. STM32H5, RP2350, ESP32-S3).
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_embedded.h"'
 * Measured (arm-none-eabi-gcc 13.2, -Os, Cortex-M0 object totals): ~169 KB flash for
 * MicroCS + modules (+ newlib/libm); compiling + running the 2.5 KB ports/cortex-m
 * demo from source peaks at ~88 KB of pool on the emulated M33 target. */
#define MCS_ENABLE_COMPILER 1
#define MCS_ENABLE_DISASM 0
#define MCS_ENABLE_FS 1
#define MCS_ENABLE_HAL 1
#define MCS_ENABLE_SCHED 1
#define MCS_ENABLE_SHELL 1
#define MCS_INT64 0
#define MCS_DEFAULT_STACK 512
#define MCS_DEFAULT_FRAMES 64
