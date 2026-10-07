/* MicroCS build profile: the smallest flash footprint - a complete firmware
 * (VM + reduced standard library + C library + a precompiled script) in
 * 64 KB of flash and 16 KB of RAM, e.g. STM32F103C8 / STM32F070CB /
 * STM32G070 / ATSAMD21E16 / nRF51 class parts. Runs precompiled .mcsb images
 * only (build them with `mcs -c` / `mcs -C` on the host).
 *
 * Everything below is a switch, not a removal: scripts that use a disabled
 * member fail with MissingMemberException, and any switch can be turned back
 * on with -D... (each costs flash, see docs/LOW_RESOURCE.md for the sizes).
 * Kept: classes/structs/interfaces/enums, exceptions, arrays, strings
 * (Substring/IndexOf/Contains/Trim/ToUpper/... and interpolation), List<T>,
 * Console, Math (integer), Thread.Sleep, Environment.TickCount, Char.Is...,
 * int.Parse/TryParse, tuples, lambdas/closures, switch expressions.
 * Removed: float/double, Dictionary/HashSet, LINQ operators, StringBuilder,
 * Random, String.Format specifiers, Convert, GC/Debug/Stopwatch, the extra
 * string/array members (MCS_ENABLE_STRING_EXTRA / MCS_ENABLE_ARRAY_EXTRA),
 * line tables (errors report no line numbers) and the stdio console fallback
 * (set cfg.write_fn).
 *
 * Measured (tools/cm_check.sh target m0-64k, Cortex-M0, -Os, newlib-nano,
 * no printf in the firmware): see docs/LOW_RESOURCE.md.
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_min.h"' */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 0
#endif
/* superinstructions cost ~8 KB of Thumb code; without them the VM runs
 * images compiled with `mcs -c -O0` (the loader rejects optimized images) */
#ifndef MCS_ENABLE_SUPEROPS
#define MCS_ENABLE_SUPEROPS 0
#endif
/* inline field/method caches: ~1 KB of code and 8 B per name constant */
#ifndef MCS_FIELD_CACHE
#define MCS_FIELD_CACHE 0
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 0
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL 0
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
#ifndef MCS_ENABLE_FLOAT
#define MCS_ENABLE_FLOAT 0
#endif
#ifndef MCS_INT64
#define MCS_INT64 0
#endif
#ifndef MCS_ENABLE_DICT
#define MCS_ENABLE_DICT 0
#endif
#ifndef MCS_ENABLE_LINQ
#define MCS_ENABLE_LINQ 0
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
#ifndef MCS_ENABLE_STRING_EXTRA
#define MCS_ENABLE_STRING_EXTRA 0
#endif
#ifndef MCS_ENABLE_ARRAY_EXTRA
#define MCS_ENABLE_ARRAY_EXTRA 0
#endif
#ifndef MCS_ENABLE_CONVERT
#define MCS_ENABLE_CONVERT 0
#endif
#ifndef MCS_ENABLE_DIAGNOSTICS
#define MCS_ENABLE_DIAGNOSTICS 0
#endif
#ifndef MCS_ENABLE_LINES
#define MCS_ENABLE_LINES 0
#endif
#ifndef MCS_ENABLE_STDIO
#define MCS_ENABLE_STDIO 0
#endif
#ifndef MCS_TINY_PRINTF
#define MCS_TINY_PRINTF 1
#endif
#ifndef MCS_ENABLE_MALLOC
#define MCS_ENABLE_MALLOC 0
#endif
#ifndef MCS_ENABLE_STACK_QUEUE
#define MCS_ENABLE_STACK_QUEUE 0
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
#define MCS_ERROR_SIZE 96
#endif
#ifndef MCS_GC_INITIAL
#define MCS_GC_INITIAL (4 * 1024)
#endif
#ifndef MCS_POOL_ALIGN
#define MCS_POOL_ALIGN 4
#endif
