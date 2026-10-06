/* MicroCS build profile: embedded standalone runtime (on-device compiler,
 * filesystem, HAL, scheduler, script-manager shell). Intended for
 * parts with 96 KB+ SRAM (STM32F4/G4/L4, ESP32-C2/C3, RP2040; the auto profile
 * picks it for 96-256 KB).
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_embedded.h"'
 * Measured (arm-none-eabi-gcc 13.2, -Os, Cortex-M0 object totals): ~169 KB flash for
 * MicroCS + modules (+ newlib/libm); compiling + running the 2.5 KB ports/cortex-m
 * demo from source peaks at ~72 KB of pool on the emulated M33 target. */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 1
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 0
#endif
#ifndef MCS_ENABLE_FS
#define MCS_ENABLE_FS 1
#endif
#ifndef MCS_ENABLE_HAL
#define MCS_ENABLE_HAL 1
#endif
#ifndef MCS_ENABLE_SCHED
#define MCS_ENABLE_SCHED 1
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL 1
#endif
#ifndef MCS_INT64
#define MCS_INT64 0
#endif
#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK 512
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES 64
#endif
