/*
 * MicroCS - project configuration header (template)
 *
 * Copy this file into your project as mcs_user_config.h and uncomment the
 * options you want to change. Every MicroCS option is listed with its default
 * value; a line that stays commented keeps the profile's or the built-in
 * default, so an unmodified copy changes nothing.
 *
 * How MicroCS finds this file (details: docs/CONFIGURATION.md):
 *   - automatically, when mcs_user_config.h is on the include path
 *     (CMake / ESP-IDF / Zephyr: next to the top-level CMakeLists.txt or in its
 *     include/, src/ or main/ folder; PlatformIO: the project's include/ folder;
 *     Arduino IDE: the library's src/ folder, written by tools/make_arduino.py);
 *   - by name: -DMCS_USER_CONFIG_FILE='"path/to/my_config.h"' (CMake MICROCS_CONFIG,
 *     microcs.mk MICROCS_CONFIG, Kconfig CONFIG_MICROCS_USER_CONFIG_FILE);
 *   - with compilers without __has_include: -DMCS_USER_CONFIG=1.
 *   -DMCS_USER_CONFIG=0 ignores the file.
 *
 * Compiler options still work and win over this file: an option given with
 * -D (or by CMake, Kconfig, menuconfig) is not taken from here. Set each option
 * in one place - defining it both here and with -D makes the compiler warn
 * about a redefinition. Values in this file win over the profile's, and
 * MCS_PROFILE here wins over the profile picked by the build system.
 *
 * Every file that includes a MicroCS header (the library and your application)
 * must see the same file, so put it on the include path of both.
 */
#ifndef MCS_USER_CONFIG_H
#define MCS_USER_CONFIG_H

/* ================================================================ profile and target */
/* Coherent set of defaults from include/profiles/ (see include/mcs_config.h):
 * MCS_PROFILE_FULL (everything on), _AUTO (picked from the target's RAM/flash),
 * _EMBEDDED (96-256 KB RAM), _MCU (64-96 KB), _LOWRAM (32-64 KB), _TINY,
 * _MIN (64 KB flash / 16 KB RAM), _LINUX. The options below win over it.
 * Default: the build system's choice (CMake MICROCS_PROFILE - auto with
 * MICROCS_PORT=stm32 -, menuconfig, Kconfig), otherwise MCS_PROFILE_FULL;
 * a profile chosen here wins over the build system's. */
//#define MCS_PROFILE              MCS_PROFILE_FULL
/* Memory of the target in KB, for MCS_PROFILE_AUTO (otherwise detected from
 * Zephyr's CONFIG_SRAM_SIZE / CONFIG_FLASH_SIZE or the STM32 / RP2 / nRF52 /
 * SAMD device macros). */
//#define MCS_TARGET_RAM_KB        264
//#define MCS_TARGET_FLASH_KB      2048
//#define MCS_ALLOW_SMALL_TARGET   0    /* 1 = build for < 16 KB RAM / < 64 KB flash anyway */
/* A board port is compiled in: the auto profile keeps the HAL classes when the
 * flash is >= 128 KB. Set by CMake MICROCS_PORT, ESP-IDF and Zephyr. */
//#define MCS_PORT_HAL             1

/* ================================================================ numbers */
//#define MCS_INT64                0    /* 1 = 64-bit int/long values, 0 = 32-bit (recommended on Cortex-M0..M4) */
//#define MCS_ENABLE_FLOAT         1    /* float/double and Math; 0 removes all floating-point code */
//#define MCS_FLOAT_DOUBLE         1    /* 1 = double precision, 0 = single (best for M4F/M33 FPUs) */
//#define MCS_COMPACT_VALUES       1    /* pack values to 4-byte alignment (default: 1 on 32-bit CPUs, 0 on 64-bit) */

/* ================================================================ compiler and bytecode */
//#define MCS_ENABLE_COMPILER      1    /* on-device compiler; 0 = run precompiled .mcsb images only (~60% less code) */
//#define MCS_ENABLE_BYTECODE_LOAD 1    /* load .mcsb images */
//#define MCS_ENABLE_BYTECODE_SAVE 1    /* save images (default: = MCS_ENABLE_COMPILER) */
//#define MCS_ENABLE_XIP           1    /* mcs_exec_image_xip(): run images in place from flash (default: = BYTECODE_LOAD) */
//#define MCS_ENABLE_SUPEROPS      1    /* the VM runs optimized superinstructions (~1.5 KB of flash) */
//#define MCS_ENABLE_OPTIMIZER     1    /* the optimizer (default: COMPILER && SUPEROPS) */
//#define MCS_OPTIMIZE_SOURCE      0    /* 1 = mcs_exec_source() optimizes too (more compile time and RAM) */
//#define MCS_ENABLE_DISASM        1    /* disassembler (debug builds, host tool) */
//#define MCS_ENABLE_LINES         1    /* line tables for error messages and stack traces */

/* ================================================================ standard library */
//#define MCS_ENABLE_LIST          1    /* List<T> */
//#define MCS_ENABLE_LINQ          1    /* Where/Select/OrderBy/GroupBy/Sum/... and Enumerable */
//#define MCS_ENABLE_DICT          1    /* Dictionary<K,V>, HashSet<T> */
//#define MCS_ENABLE_STACK_QUEUE   1    /* Stack<T>, Queue<T> (with MCS_ENABLE_LIST) */
//#define MCS_ENABLE_STRINGBUILDER 1    /* StringBuilder */
//#define MCS_ENABLE_RANDOM        1    /* Random */
//#define MCS_ENABLE_MATH          1    /* Math floating point (default: = MCS_ENABLE_FLOAT) */
//#define MCS_ENABLE_FORMAT        1    /* String.Format / {x:F2} format specifiers */
//#define MCS_ENABLE_STRING_EXTRA  1    /* Split/Join/Replace/PadLeft/PadRight/Insert/Remove/LastIndexOf/... (~3 KB) */
//#define MCS_ENABLE_ARRAY_EXTRA   1    /* Array.Sort/Copy/Fill/Find..., List AddRange/RemoveAll/Sort/... (~6 KB) */
//#define MCS_ENABLE_BYTES         1    /* Encoding, BitConverter, BinaryPrimitives, Base64 / hex (~3 KB) */
//#define MCS_ENABLE_CONVERT       1    /* the Convert class */
//#define MCS_ENABLE_DIAGNOSTICS   1    /* GC, Debug and Stopwatch */

/* ================================================================ C library */
//#define MCS_ENABLE_STDIO         1    /* stdout/stdin console and clock() fallbacks; 0 = no stdio at all */
//#define MCS_ENABLE_MALLOC        1    /* realloc()/free() when cfg.realloc_fn is NULL; 0 = pass an allocator */
//#define MCS_TINY_PRINTF          0    /* 1 = built-in formatter instead of snprintf (saves 2-3 KB on newlib-nano) */
//#define MCS_ENABLE_POOL_HEAP     1    /* built-in fixed-pool allocator (mcs_pool_realloc) */
//#define MCS_POOL_ALIGN           8    /* pool block alignment; 4 is safe on Cortex-M and saves RAM */

/* ================================================================ VM speed and memory */
//#define MCS_COMPUTED_GOTO        1    /* computed-goto dispatch (default: 1 with GCC/Clang) */
//#define MCS_FIELD_CACHE          1    /* inline cache for obj.field (~8 B per name constant on 32-bit) */
//#define MCS_LAZY_REGS            1    /* native member tables stay in flash until first use */
//#define MCS_LAZY_CLASSES         1    /* built-in classes created on first use (VM starts in 1.7-8 KB) */
//#define MCS_TABLE_MIN_CAP        4    /* first size of every hash table (power of two, >= 4) */

/* ================================================================ VM limits */
//#define MCS_DEFAULT_STACK        1024 /* value-stack slots (cfg.stack_slots = 0) */
//#define MCS_DEFAULT_FRAMES       64   /* max call depth */
//#define MCS_MAX_HANDLERS         32   /* nested try blocks */
//#define MCS_MAX_ROOTS            32   /* temporary GC roots for natives */
//#define MCS_MAX_PINS             16   /* persistent handles for C code (mcs_pin) */
//#define MCS_STACK_MARGIN         32   /* operand-stack headroom per frame */
//#define MCS_ERROR_SIZE           256  /* last-error message buffer (mcs_last_error) */
//#define MCS_GC_INITIAL           (16 * 1024) /* bytes allocated before the first GC */
//#define MCS_GC_GROW              2    /* next GC threshold multiplier */
//#define MCS_GC_STRESS            0    /* debug: full collection on every allocation (very slow) */
//#define MCS_HOOK_INTERVAL        1000 /* backward jumps/calls between hook calls */
//#define MCS_SLEEP_SLICE_MS       10   /* Thread.Sleep granularity for abort checks / events */

/* ================================================================ modules (modules/) */
//#define MCS_ENABLE_FS            1    /* VFS + C# File/Directory */
//#define MCS_ENABLE_FLASH         1    /* NOR/NAND flash layer + SPI NOR/NAND drivers (default: = MCS_ENABLE_FS) */
//#define MCS_ENABLE_LFS           0    /* LittleFS backend - also compile lfs.c + lfs_util.c (not bundled) */
//#define MCS_ENABLE_YAFFS         0    /* YAFFS2 backend (GPLv2) - also compile the yaffs2 sources + their CONFIG_YAFFS_* defines */
//#define MCS_ENABLE_HAL           1    /* C# GPIO/UART/I2C/SPI/ADC/DAC/PWM/Timer/I2S/QSPI/CAN/... */
//#define MCS_ENABLE_DRIVERS       1    /* device-driver registry + C# Drivers (default: = MCS_ENABLE_HAL) */
//#define MCS_ENABLE_WS2812        1    /* built-in "ws2812" driver: C# LedStrip (default: = MCS_ENABLE_DRIVERS) */
//#define MCS_ENABLE_SCHED         1    /* job scheduler */
//#define MCS_ENABLE_SHELL         1    /* script manager / REPL shell (default: = MCS_ENABLE_FS) */
//#define MCS_ENABLE_RUNTIME       1    /* mcs_runtime_run() (default: = MCS_ENABLE_SHELL) */

/* ---- filesystem */
//#define MCS_VFS_MAX_MOUNTS       4
//#define MCS_VFS_PATH_MAX         128
//#define MCS_FLASHFS_MAX          2    /* mcs_flashfs_mount() filesystems at the same time */
//#define MCS_LFS_MAX_FILES        4    /* LittleFS files open at the same time */
//#define MCS_FLASH_POLL_MAX       20000000UL /* status polls before giving up (no wait fn) */
//#define MCS_FLASH_WAIT_US        50   /* wait fn period */
//#define MCS_YAFFS_OSGLUE         0    /* 1 = built-in single-threaded yaffs_osglue (malloc, no locks) */
//#define MCS_YAFFS_NAND_INBAND    1    /* tags inside the page data (covered by the chip's ECC) */
//#define MCS_YAFFS_OOB_OFFSET     2    /* spare-area tags start after the bad-block marker */
//#define MCS_YAFFS_CACHES         4    /* short-op cache entries (one chunk of RAM each) */
//#define MCS_YAFFS_RESERVED_BLOCKS 5

/* ---- hardware (HAL) and drivers */
//#define MCS_HAL_MAX_XFER         256  /* largest transfer per call (buffers on the C stack) */
//#define MCS_HAL_EVENT_QUEUE      32   /* ISR -> VM event ring (power of two) */
//#define MCS_HAL_MAX_CALLBACKS    16   /* GPIO + timer + user event handlers per VM */
//#define MCS_HAL_MAX_VMS          2    /* VMs with the HAL open at the same time */
//#define MCS_HAL_MAX_BUSES        4    /* per peripheral type, cached bus settings */
//#define MCS_HAL_SIM_FLASH        4096 /* simulator board: QSPI NOR size */
/* Critical section around the event ring when ISRs of different priorities
 * post concurrently (default on Cortex-M: PRIMASK; elsewhere none). */
//#define MCS_HAL_CRITICAL_ENTER() my_irq_lock()
//#define MCS_HAL_CRITICAL_EXIT()  my_irq_unlock()
//#define MCS_MAX_DRIVERS          8    /* registered drivers (built-in + yours) */
//#define MCS_LEDSTRIP_MAX         1024 /* LEDs per LedStrip */

/* ---- scheduler and shell */
//#define MCS_SCHED_MAX_JOBS       8
//#define MCS_SCHED_PATH_MAX       64
//#define MCS_SCHED_DURING_SLEEP   1    /* due jobs run while a script waits in Thread.Sleep */
//#define MCS_SHELL_LINE_MAX       256
//#define MCS_SHELL_REPL_MAX       1024 /* multi-line REPL buffer; 0 removes the REPL */

/* ================================================================ board ports (ports/) */
/* ---- ESP32 (ESP-IDF, ports/esp32) */
//#define MCS_ESP32_UARTS          3
//#define MCS_ESP32_I2C_BUSES      2
//#define MCS_ESP32_I2C_DEVICES    8    /* cached device handles per bus (new I2C driver) */
//#define MCS_ESP32_SPI_BUSES      2    /* C# bus 0 = SPI2_HOST, 1 = SPI3_HOST */
//#define MCS_ESP32_PWM_CHANNELS   8    /* LEDC channels */
//#define MCS_ESP32_TIMERS         4    /* gptimers for Timer.Start (capped at the chip's count) */
//#define MCS_ESP32_UART_RXBUF     1024
//#define MCS_ESP32_EVENT_QUEUE    32
//#define MCS_ESP32_TIMEOUT_MS     100  /* I2C / SPI transfer timeout */
//#define MCS_ESP32_FS_PATH        "/mcs" /* VFS mount point of mcs_esp32_littlefs() */
//#define MCS_ESP32_LEDC_CLK_HZ    80000000u /* LEDC clock used to pick the PWM resolution (C2: 60 MHz, H2: 96 MHz) */
//#define MCS_ESP32_RGB_LED        48   /* GPIO of the "NEOPIXEL" pin name (S3: 48, C3/C6/H2: 8, else -1); also Arduino-ESP32 */
//#define MCS_ESP32_LEDSTRIPS      2    /* LedStrips on different pins (RMT channels); also Arduino-ESP32 */

/* ---- Raspberry Pi RP2040 / RP2350 (pico-sdk, ports/rp2) */
//#define MCS_RP2_UART_RXBUF       256  /* receive ring (power of two) */
//#define MCS_RP2_TIMERS           4
//#define MCS_RP2_TIMEOUT_US       50000 /* I2C / SPI transfer timeout */
//#define MCS_RP2_FS_SIZE          (1u << 20) /* mcs_rp2_flash_init(&f, 0, 0) region at the end of flash
//                                               (default: half of 2 MB, all but 1 MB of >= 4 MB) */
//#define MCS_RP2_LEDSTRIPS        4    /* LedStrips on different pins (PIO state machines); also Arduino-Pico */

/* ---- STM32 (STM32Cube HAL, ports/stm32) */
//#define MCS_STM32_HAL_HEADER     "stm32g4xx_hal.h" /* family header (default: detected) */
//#define MCS_STM32_UARTS          9    /* UART.Open(n) table size */
//#define MCS_STM32_UART_RXBUF     256  /* receive ring per UART (power of two) */
//#define MCS_STM32_BUSES          5    /* I2C / SPI / I2S / CAN / QSPI handles */
//#define MCS_STM32_ADC_CHANNELS   16
//#define MCS_STM32_PWM_CHANNELS   12
//#define MCS_STM32_TIMERS         4
//#define MCS_STM32_IRQ_PRIORITY   5    /* EXTI priority (>= configMAX_SYSCALL_INTERRUPT_PRIORITY with FreeRTOS) */
//#define MCS_STM32_TIMEOUT_MS     100  /* I2C / SPI / QSPI transfer timeout */
//#define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_480CYCLES /* default: the family's longest */
//#define MCS_STM32_FS_SIZE        0    /* mcs_stm32_flash_init() region; 0 = a quarter of the flash */
//#define MCS_STM32_DEFINE_CALLBACKS 1  /* 0 = your project defines the HAL UART/EXTI callbacks */
//#define MCS_STM32_DEFINE_TIM_CALLBACK 0 /* 1 = the port defines HAL_TIM_PeriodElapsedCallback */
//#define MCS_STM32_EXTI_HANDLERS  0    /* 1 = the port defines the EXTIx_IRQHandler functions */

/* ---- Zephyr (ports/zephyr; most settings come from Kconfig and the devicetree) */
//#define MCS_ZEPHYR_UART_RXBUF    256
//#define MCS_ZEPHYR_TIMERS        4
//#define MCS_ZEPHYR_DAC_BITS      12
//#define MCS_ZEPHYR_I2S_BLOCK     1024 /* bytes per DMA block */
//#define MCS_ZEPHYR_I2S_BLOCKS    4
//#define MCS_ZEPHYR_FS_FILES      4    /* files open at the same time */
//#define MCS_ZEPHYR_FS_MOUNT      "/lfs"

/* ---- Arduino (ports/arduino) */
//#define MCS_ARDUINO_UARTS        4
//#define MCS_ARDUINO_TIMERS       4
//#define MCS_ARDUINO_ADC_BITS     12   /* analogReadResolution() (default: 12 on ESP32, else 10) */
//#define MCS_ARDUINO_NO_WIRE           /* defined = no I2C (Wire library) */
//#define MCS_ARDUINO_NO_SPI            /* defined = no SPI library */

#endif /* MCS_USER_CONFIG_H */
