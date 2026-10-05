/* MicroCS build profile: smallest configuration the CURRENT code base supports.
 * This is NOT the 2 KB / 16 KB "Tiny" target from the Phase 2 brief - that would
 * need a different (non-object, no-GC) VM design and is listed as *planned* in
 * docs/PHASE2_ROADMAP.md. Measured: ~81 KB flash (Cortex-M0, -Os, object totals) for the VM +
 * reduced stdlib; RAM is dominated by the ~40-50 KB stdlib metadata heap.
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
