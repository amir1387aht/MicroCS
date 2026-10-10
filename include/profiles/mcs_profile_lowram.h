/* MicroCS build profile: small MCUs with roughly 16-64 KB of RAM
 * (Cortex-M0+/M3/M4 parts such as STM32F0/F1/G0/L4, nRF52810, RP2040 with a
 * small heap budget). Runs precompiled .mcsb images only.
 *
 * What it trades away, compared with the default build:
 *   - single-precision float/double  -> 8-byte values instead of 12
 *     (value stack, globals, fields, arrays, dictionaries all shrink by 1/3)
 *   - no on-device compiler / disassembler (build images with `mcs -c` on the host)
 *   - smaller VM limits: 128 value-stack slots, 24 call frames, 12 nested try blocks, 16 C roots
 *   - first GC after 4 KB instead of 16 KB (lower peak, more collections)
 * The LINQ operators stay enabled; MCS_ENABLE_LINQ 0 saves ~12 KB flash.
 *
 * Measured on the emulated Cortex-M0 target `m0-lowram` (tools/cm_check.sh,
 * 64 KB RAM part): the VM needs ~5 KB of heap after mcs_new() (class tables
 * are built lazily); the ports/cortex-m demo image runs in a 40 KB pool (VM
 * heap + 1 KB RAM filesystem) with a 30 KB peak. The examples/lowram node
 * runs in 16 KB of RAM (target m0-16k: 12 KB pool, 11.2 KB peak).
 * See docs/LOW_RESOURCE.md.
 * Use: #define MCS_PROFILE MCS_PROFILE_LOWRAM in mcs_user_config.h, or
 *      -DMCS_PROFILE=MCS_PROFILE_LOWRAM (CMake: MICROCS_PROFILE=lowram) */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 0
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 0
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL 0
#endif
#ifndef MCS_INT64
#define MCS_INT64 0
#endif
#ifndef MCS_FLOAT_DOUBLE
#define MCS_FLOAT_DOUBLE 0
#endif
#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK 128
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES 24
#endif
#ifndef MCS_MAX_HANDLERS
#define MCS_MAX_HANDLERS 12
#endif
#ifndef MCS_MAX_ROOTS
#define MCS_MAX_ROOTS 16
#endif
#ifndef MCS_MAX_PINS
#define MCS_MAX_PINS 8
#endif
#ifndef MCS_ERROR_SIZE
#define MCS_ERROR_SIZE 128
#endif
#ifndef MCS_GC_INITIAL
#define MCS_GC_INITIAL (4 * 1024)
#endif
#ifndef MCS_POOL_ALIGN
#define MCS_POOL_ALIGN 4
#endif
