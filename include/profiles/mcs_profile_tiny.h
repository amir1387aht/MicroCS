/* MicroCS build profile: smallest configuration the CURRENT code base supports.
 * (A 2 KB RAM / 16 KB flash class would need a different, non-object, no-GC VM
 * design; see docs/PERFORMANCE.md.) Measured: ~84 KB flash (Cortex-M0, -Os, object totals) for the VM +
 * reduced stdlib. For the smallest flash use profiles/mcs_profile_min.h (64 KB flash /
 * 16 KB RAM firmware); see docs/LOW_RESOURCE.md.
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_tiny.h"' */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 0
#endif
#ifndef MCS_ENABLE_FLOAT
#define MCS_ENABLE_FLOAT 0
#endif
#ifndef MCS_ENABLE_DICT
#define MCS_ENABLE_DICT 0
#endif
#ifndef MCS_ENABLE_STRINGBUILDER
#define MCS_ENABLE_STRINGBUILDER 0
#endif
#ifndef MCS_ENABLE_RANDOM
#define MCS_ENABLE_RANDOM 0
#endif
#ifndef MCS_ENABLE_FORMAT
#define MCS_ENABLE_FORMAT 0
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 0
#endif
#ifndef MCS_ENABLE_LINES
#define MCS_ENABLE_LINES 0
#endif
#ifndef MCS_ENABLE_FS
#define MCS_ENABLE_FS 0
#endif
#ifndef MCS_ENABLE_HAL
#define MCS_ENABLE_HAL 0
#endif
#ifndef MCS_ENABLE_SCHED
#define MCS_ENABLE_SCHED 0
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL 0
#endif
#ifndef MCS_INT64
#define MCS_INT64 0
#endif
#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK 128
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES 24
#endif
