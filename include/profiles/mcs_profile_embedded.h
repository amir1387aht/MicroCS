/* MicroCS build profile: embedded standalone runtime (on-device compiler,
 * filesystem, HAL, scheduler, script-manager shell). Intended for parts like
 * SiFli SF32LB525 (Cortex-M33, 512 KB SRAM).
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_embedded.h"'
 * Measured (arm-none-eabi-gcc 13.2, -Os, object totals): ~160 KB flash for MicroCS +
 * modules (+ newlib/libm ~58 KB); compiling a 3 KB script peaks ~160 KB heap. */
#define MCS_ENABLE_COMPILER 1
#define MCS_ENABLE_DISASM 0
#define MCS_ENABLE_FS 1
#define MCS_ENABLE_HAL 1
#define MCS_ENABLE_SCHED 1
#define MCS_ENABLE_SHELL 1
#define MCS_INT64 0
#define MCS_DEFAULT_STACK 512
#define MCS_DEFAULT_FRAMES 64
