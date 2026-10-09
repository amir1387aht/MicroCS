/*
 * MicroCS - C# interpreter for microcontrollers
 * mcs_config.h - compile-time configuration and feature selection.
 *
 * Override any option by defining it on the compiler command line or by
 * defining MCS_USER_CONFIG_FILE="my_mcs_config.h" which is included first.
 */
#ifndef MCS_CONFIG_H
#define MCS_CONFIG_H

#ifdef MCS_USER_CONFIG_FILE
#include MCS_USER_CONFIG_FILE
#endif

/* ---------------------------------------------------------------- numbers */
/* 1 = 64-bit `int`/`long` values, 0 = 32-bit (recommended for Cortex-M0..M4) */
#ifndef MCS_INT64
#define MCS_INT64 0
#endif
/* Floating point support (float/double, Math class). 0 removes all FP code. */
#ifndef MCS_ENABLE_FLOAT
#define MCS_ENABLE_FLOAT 1
#endif
/* 1 = double precision, 0 = single precision (best for M4F/M33 FPUs) */
#ifndef MCS_FLOAT_DOUBLE
#define MCS_FLOAT_DOUBLE 1
#endif

/* Pack mcs_value_t to 4-byte alignment. Only matters when the payload is
 * 8 bytes (MCS_FLOAT_DOUBLE=1 or MCS_INT64=1) on a 32-bit CPU: values shrink
 * from 16 to 12 bytes (stack, globals, fields, arrays, tables: -25% RAM) and
 * GCC copies them with LDM/STM instead of calling memcpy - on Cortex-M0 with
 * newlib-nano that alone made the demo ~4x faster. The compiler knows the
 * payload is only word aligned, so the generated code is correct on every
 * CPU. Default: on for 32-bit targets, off for 64-bit hosts. */
#ifndef MCS_COMPACT_VALUES
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 4
#define MCS_COMPACT_VALUES 1
#else
#define MCS_COMPACT_VALUES 0
#endif
#endif

/* ---------------------------------------------------------------- features */
/* On-device compiler (source -> bytecode). Set 0 to run only precompiled
 * .mcsb images produced by the host tool: saves ~60% of code size. */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER 1
#endif
/* Loading / saving of bytecode images */
#ifndef MCS_ENABLE_BYTECODE_LOAD
#define MCS_ENABLE_BYTECODE_LOAD 1
#endif
#ifndef MCS_ENABLE_BYTECODE_SAVE
#define MCS_ENABLE_BYTECODE_SAVE MCS_ENABLE_COMPILER
#endif
/* Execute-in-place images: mcs_exec_image_xip() runs bytecode directly from
 * the image buffer (e.g. flash) instead of copying every function's code to
 * the heap. Costs one table lookup per global access in all builds. */
#ifndef MCS_ENABLE_XIP
#define MCS_ENABLE_XIP MCS_ENABLE_BYTECODE_LOAD
#endif
/* Standard library pieces */
#ifndef MCS_ENABLE_LIST
#define MCS_ENABLE_LIST 1          /* List<T>, LINQ-style helpers */
#endif
#ifndef MCS_ENABLE_LINQ
#define MCS_ENABLE_LINQ 1          /* Where/Select/OrderBy/GroupBy/Sum/... + Enumerable */
#endif
#ifndef MCS_ENABLE_DICT
#define MCS_ENABLE_DICT 1          /* Dictionary<K,V> */
#endif
#ifndef MCS_ENABLE_STRINGBUILDER
#define MCS_ENABLE_STRINGBUILDER 1
#endif
#ifndef MCS_ENABLE_RANDOM
#define MCS_ENABLE_RANDOM 1
#endif
#ifndef MCS_ENABLE_MATH
#define MCS_ENABLE_MATH MCS_ENABLE_FLOAT
#endif
#ifndef MCS_ENABLE_FORMAT
#define MCS_ENABLE_FORMAT 1        /* String.Format / {x:F2} specifiers */
#endif
/* Optional library members (all on by default; turning one off removes the
 * listed members, scripts that call them get a MissingMemberException). */
#ifndef MCS_ENABLE_STRING_EXTRA
#define MCS_ENABLE_STRING_EXTRA 1  /* string Split/Join/Replace/PadLeft/PadRight/Insert/Remove/
                                      LastIndexOf/IndexOfAny/ToCharArray (~3 KB) */
#endif
#ifndef MCS_ENABLE_ARRAY_EXTRA
#define MCS_ENABLE_ARRAY_EXTRA 1   /* Array.Sort/Copy/Fill/Find.../BinarySearch statics, the
                                      Find/Exists/TrueForAll/ConvertAll/CopyTo/BinarySearch/
                                      LastIndexOf members of arrays and lists, and List
                                      AddRange/InsertRange/RemoveRange/RemoveAll/GetRange/
                                      Sort/Reverse/TrimExcess/AsReadOnly (~6 KB) */
#endif
#ifndef MCS_ENABLE_STACK_QUEUE
#define MCS_ENABLE_STACK_QUEUE 1   /* Stack<T> and Queue<T> (with MCS_ENABLE_LIST) */
#endif
#ifndef MCS_ENABLE_BYTES
#define MCS_ENABLE_BYTES 1         /* Encoding.UTF8/ASCII, BitConverter, BinaryPrimitives, Convert.To/FromBase64String, To/FromHexString */
#endif
#ifndef MCS_ENABLE_CONVERT
#define MCS_ENABLE_CONVERT 1       /* the Convert class (ToInt32/ToString(x, base)/ToBoolean/...) */
#endif
#ifndef MCS_ENABLE_DIAGNOSTICS
#define MCS_ENABLE_DIAGNOSTICS 1   /* GC, Debug and Stopwatch classes */
#endif
/* Default console on the C library (stdout/stdin/stderr) when the host does
 * not set write_fn/readline_fn/error_fn. 0 = no stdio dependency at all:
 * output without write_fn is dropped, Console.ReadLine returns null. */
#ifndef MCS_ENABLE_STDIO
#define MCS_ENABLE_STDIO 1         /* (also the clock() fallback when ticks_fn is not set) */
#endif
/* Format the VM's messages and numbers with the small built-in formatter
 * (src/mcs_fmt.c) instead of the C library's snprintf; saves ~2-3 KB of flash
 * on newlib-nano when the firmware itself does not use printf. Floating-point
 * output still goes through the C library when MCS_ENABLE_FLOAT=1. */
/* realloc()/free() from the C library when cfg.realloc_fn is NULL. 0 = the
 * host must pass an allocator (e.g. mcs_pool_realloc); mcs_new fails otherwise. */
#ifndef MCS_ENABLE_MALLOC
#define MCS_ENABLE_MALLOC 1
#endif
#ifndef MCS_TINY_PRINTF
#define MCS_TINY_PRINTF 0
#endif
/* Disassembler (debug builds / host tool) */
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM 1
#endif
/* Keep line-number tables for error messages and stack traces */
#ifndef MCS_ENABLE_LINES
#define MCS_ENABLE_LINES 1
#endif
/* Use GCC computed-goto dispatch when available (~15-25% faster) */
/* Optimized bytecode. MCS_ENABLE_SUPEROPS: the VM runs the superinstructions
 * that the optimizer emits (~1.5 KB of flash on Thumb-2). MCS_ENABLE_OPTIMIZER:
 * the optimizer itself (compiler side); mcs_compile_image / `mcs -c` always use
 * it, mcs_exec_source only with MCS_OPTIMIZE_SOURCE=1 (costs compile time/RAM). */
#ifndef MCS_ENABLE_SUPEROPS
#define MCS_ENABLE_SUPEROPS 1
#endif
#ifndef MCS_ENABLE_OPTIMIZER
#define MCS_ENABLE_OPTIMIZER (MCS_ENABLE_COMPILER && MCS_ENABLE_SUPEROPS)
#endif
#ifndef MCS_OPTIMIZE_SOURCE
#define MCS_OPTIMIZE_SOURCE 0
#endif
#ifndef MCS_FIELD_CACHE
#define MCS_FIELD_CACHE 1   /* per-function inline cache for obj.field (class -> slot); ~8 B per constant on 32-bit */
#endif
#ifndef MCS_COMPUTED_GOTO
#if defined(__GNUC__)
#define MCS_COMPUTED_GOTO 1
#else
#define MCS_COMPUTED_GOTO 0
#endif
#endif
/* Built-in fixed-pool allocator (mcs_heap.c) for targets without malloc */
#ifndef MCS_ENABLE_POOL_HEAP
#define MCS_ENABLE_POOL_HEAP 1
#endif
/* Block alignment of the pool allocator (also its per-block header size).
 * 8 is safe everywhere. 4 saves 4+ bytes per allocation and is safe on
 * Cortex-M (ARMv6-M/v7-M/v8-M only need word alignment for every load,
 * including LDRD/VLDR); never set below the pointer size (it is clamped). */
#ifndef MCS_POOL_ALIGN
#define MCS_POOL_ALIGN 8
#endif

/* ---------------------------------------------------------------- limits */
/* Native members registered via mcs_reg_t tables stay in ROM/flash and are
 * materialized on first use (saves tens of KB of RAM). Tables passed to the
 * registration API must then have static storage duration. */
#ifndef MCS_LAZY_REGS
#define MCS_LAZY_REGS 1
#endif

/* Built-in classes (exceptions, collections, Math, Console, ...) and the
 * classes registered by modules (HAL, filesystem, scheduler) are created the
 * first time a script or C code names them, instead of in mcs_new(). A VM
 * then starts in 1.7-8 KB on a 32-bit MCU instead of 13-25 KB, and a class that
 * is never used costs no RAM at all. 0 = create everything in mcs_new(). */
#ifndef MCS_LAZY_CLASSES
#define MCS_LAZY_CLASSES 1
#endif

/* First allocation size (entries) of every hash table: class member tables,
 * Dictionary/HashSet indexes. Most built-in classes have 1-3 members per table,
 * so 4 halves their RAM compared with 8. Power of two, >= 4. */
#ifndef MCS_TABLE_MIN_CAP
#define MCS_TABLE_MIN_CAP 4
#endif

#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK 1024     /* value-stack slots */
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES 64      /* max call depth */
#endif
#ifndef MCS_MAX_HANDLERS
#define MCS_MAX_HANDLERS 32        /* nested try blocks (dynamic) */
#endif
#ifndef MCS_MAX_ROOTS
#define MCS_MAX_ROOTS 32           /* temporary GC roots for natives */
#endif
/* Debug: run a full collection on every allocation (very slow). */
#ifndef MCS_GC_STRESS
#define MCS_GC_STRESS 0
#endif
#ifndef MCS_GC_INITIAL
#define MCS_GC_INITIAL (16 * 1024) /* bytes allocated before first GC */
#endif
#ifndef MCS_GC_GROW
#define MCS_GC_GROW 2              /* next GC threshold multiplier */
#endif
#ifndef MCS_HOOK_INTERVAL
#define MCS_HOOK_INTERVAL 1000     /* backward jumps/calls between hook calls */
#endif
/* ---------------------------------------------------------------- modules
 * Optional modules in modules/. They depend only on the public API; a flag of 0
 * compiles the module to nothing, so "add every .c file" builds still work. */
#ifndef MCS_ENABLE_FS
#define MCS_ENABLE_FS 1            /* VFS + C# File/Directory      */
#endif
#ifndef MCS_ENABLE_FLASH
#define MCS_ENABLE_FLASH MCS_ENABLE_FS /* mcs_flash.h: NOR/NAND device layer + SPI NOR/NAND drivers */
#endif
#ifndef MCS_ENABLE_LFS
#define MCS_ENABLE_LFS 0           /* LittleFS backend (needs littlefs sources, not bundled) */
#endif
#ifndef MCS_ENABLE_YAFFS
#define MCS_ENABLE_YAFFS 0         /* YAFFS2 backend (needs yaffs2 direct sources, not bundled) */
#endif
#ifndef MCS_ENABLE_HAL
#define MCS_ENABLE_HAL 1           /* C# GPIO/UART/I2C/SPI/ADC/PWM  */
#endif
#ifndef MCS_ENABLE_SCHED
#define MCS_ENABLE_SCHED 1         /* job scheduler                 */
#endif
#ifndef MCS_ENABLE_DRIVERS
#define MCS_ENABLE_DRIVERS MCS_ENABLE_HAL /* device drivers (mcs_driver.h): registry + C# Drivers class */
#endif
#if MCS_ENABLE_DRIVERS && !MCS_ENABLE_HAL
#undef MCS_ENABLE_DRIVERS
#define MCS_ENABLE_DRIVERS 0       /* drivers are opened with the HAL library */
#endif
#ifndef MCS_ENABLE_WS2812
#define MCS_ENABLE_WS2812 MCS_ENABLE_DRIVERS /* built-in "ws2812" driver: C# LedStrip (WS2812/SK6812) */
#endif
#if MCS_ENABLE_WS2812 && !MCS_ENABLE_DRIVERS
#undef MCS_ENABLE_WS2812
#define MCS_ENABLE_WS2812 0
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL MCS_ENABLE_FS /* standalone runtime / script manager */
#endif
#ifndef MCS_ENABLE_RUNTIME
#define MCS_ENABLE_RUNTIME MCS_ENABLE_SHELL /* mcs_runtime_run(): whole firmware in one call */
#endif
#if MCS_ENABLE_RUNTIME && !MCS_ENABLE_SHELL
#undef MCS_ENABLE_RUNTIME
#define MCS_ENABLE_RUNTIME 0       /* the runtime is built on the shell */
#endif

/* Size of the last-error message buffer inside the VM (mcs_last_error). */
#ifndef MCS_ERROR_SIZE
#define MCS_ERROR_SIZE 256
#endif
/* Persistent handles available to C code via mcs_pin(). */
#ifndef MCS_MAX_PINS
#define MCS_MAX_PINS 16
#endif

#ifndef MCS_SLEEP_SLICE_MS
#define MCS_SLEEP_SLICE_MS 10      /* Thread.Sleep granularity for abort checks / event dispatch */
#endif

#ifndef MCS_STACK_MARGIN
#define MCS_STACK_MARGIN 32        /* operand-stack headroom per frame */
#endif

#endif
