/* MicroCS build profile: small MCUs with roughly 32-64 KB of RAM
 * (Cortex-M0+/M3/M4 parts such as STM32F0/F1/G0/L4, nRF52810, RP2040 with a
 * small heap budget). Runs precompiled .mcsb images only.
 *
 * What it trades away, compared with the default build:
 *   - single-precision float/double  -> 8-byte values instead of 12
 *     (value stack, globals, fields, arrays, dictionaries all shrink by 1/3)
 *   - no on-device compiler / disassembler (build images with `mcs -c` on the host)
 *   - smaller VM limits: 192 value-stack slots, 32 call frames, 16 nested try blocks
 *   - first GC after 4 KB instead of 16 KB (lower peak, more collections)
 * The LINQ operators stay enabled; add -DMCS_ENABLE_LINQ=0 to save ~12 KB flash.
 *
 * Measured on the emulated Cortex-M0 target `m0-lowram` (tools/cm_check.sh,
 * 64 KB RAM part): the VM needs ~20 KB of heap after mcs_new(); the
 * ports/cortex-m demo image runs in a 40 KB pool (VM heap + 1 KB RAM
 * filesystem) with a 33 KB peak. ~111 KB of MicroCS objects (-Os, before
 * --gc-sections, all modules), ~100 KB with LINQ off. See docs/LOW_RESOURCE.md.
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_lowram.h"' */
#define MCS_ENABLE_COMPILER 0
#define MCS_ENABLE_DISASM 0
#define MCS_ENABLE_SHELL 0
#define MCS_INT64 0
#define MCS_FLOAT_DOUBLE 0
#define MCS_DEFAULT_STACK 192
#define MCS_DEFAULT_FRAMES 32
#define MCS_MAX_HANDLERS 16
#define MCS_MAX_PINS 8
#define MCS_ERROR_SIZE 128
#define MCS_GC_INITIAL (4 * 1024)
#define MCS_POOL_ALIGN 4
