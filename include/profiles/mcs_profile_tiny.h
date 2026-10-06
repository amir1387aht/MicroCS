/* MicroCS build profile: smallest configuration the CURRENT code base supports.
 * (A 2 KB RAM / 16 KB flash class would need a different, non-object, no-GC VM
 * design; see docs/PERFORMANCE.md.) Measured: ~84 KB flash (Cortex-M0, -Os, object totals) for the VM +
 * reduced stdlib. For the smallest RAM footprint use profiles/mcs_profile_lowram.h
 * (~20 KB heap after mcs_new on Cortex-M0); see docs/LOW_RESOURCE.md.
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_tiny.h"' */
#define MCS_ENABLE_COMPILER 0
#define MCS_ENABLE_FLOAT 0
#define MCS_ENABLE_DICT 0
#define MCS_ENABLE_STRINGBUILDER 0
#define MCS_ENABLE_RANDOM 0
#define MCS_ENABLE_FORMAT 0
#define MCS_ENABLE_DISASM 0
#define MCS_ENABLE_LINES 0
#define MCS_ENABLE_FS 0
#define MCS_ENABLE_HAL 0
#define MCS_ENABLE_SCHED 0
#define MCS_ENABLE_SHELL 0
#define MCS_INT64 0
#define MCS_DEFAULT_STACK 128
#define MCS_DEFAULT_FRAMES 24
