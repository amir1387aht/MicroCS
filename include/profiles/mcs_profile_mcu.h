/* MicroCS build profile: MCU runtime (precompiled .mcsb images only).
 * Use: #define MCS_PROFILE MCS_PROFILE_MCU in mcs_user_config.h, or
 *      -DMCS_PROFILE=MCS_PROFILE_MCU (CMake: MICROCS_PROFILE=mcu)
 * Measured (arm-none-eabi-gcc 13.2, Cortex-M0, -Os, object totals before
 * --gc-sections): ~121 KB flash incl. FS/HAL/scheduler modules, plus newlib/libm; ~8 KB heap
 * after mcs_new() with the full stdlib (emulated m0-runtime target). The auto profile picks
 * it for 64-96 KB of RAM. See docs/LOW_RESOURCE.md. */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 0
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 0
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL 0
#endif
#ifndef MCS_ENABLE_SCHED
#define MCS_ENABLE_SCHED 1
#endif
#ifndef MCS_INT64
#define MCS_INT64 0
#endif
#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK 256
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES 48
#endif
