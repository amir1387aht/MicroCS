/* MicroCS build profile: MCU runtime (precompiled .mcsb images only).
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_mcu.h"'
 * Measured (arm-none-eabi-gcc 13.2, Cortex-M0, -Os, object totals before
 * --gc-sections): ~121 KB flash incl. FS/HAL/scheduler modules, plus newlib/libm; ~25 KB heap
 * after mcs_new() with the full stdlib (emulated m0-runtime target). See docs/LOW_RESOURCE.md. */
#define MCS_ENABLE_COMPILER 0
#define MCS_ENABLE_DISASM 0
#define MCS_ENABLE_SHELL 0
#define MCS_ENABLE_SCHED 1
#define MCS_INT64 0
#define MCS_DEFAULT_STACK 256
#define MCS_DEFAULT_FRAMES 48
