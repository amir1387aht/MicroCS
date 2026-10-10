/*
 * MicroCS - project configuration header
 *
 * Copy this file into your project as mcs_user_config.h: that enables it, and
 * it holds every MicroCS option with its default value (the full profile), so
 * an unmodified copy builds exactly like no header at all. Change the values
 * you need. For another profile's defaults generate the file instead:
 *     python3 tools/gen_config.py --profile lowram -o mcs_user_config.h
 * (profiles: full, embedded, mcu, lowram, tiny, min, linux, or auto with
 * --ram-kb / --flash-kb). The few options left commented out are detected per
 * compiler, CPU or board; uncomment them only to force a value.
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
 * Compiler options still win over this file: every value below is guarded by
 * #ifndef, so an option given with -D (or by CMake, Kconfig, menuconfig) is
 * not taken from here. Values that follow another option (e.g.
 * MCS_ENABLE_FLASH = MCS_ENABLE_FS) are written as that option and keep
 * following it.
 *
 * Every file that includes a MicroCS header (the library and your application)
 * must see the same file, so put it on the include path of both.
 */
#ifndef MCS_USER_CONFIG_H
#define MCS_USER_CONFIG_H

/* ================================================================ profile and target */
/* The values in this file are the defaults of this profile (they replace the
 * profile's own, so the build system's profile choice - CMake MICROCS_PROFILE,
 * menuconfig, Kconfig - does not apply; MicroCS warns when it differs).
 * MCS_PROFILE still sets the defaults of options not listed here. */
#define MCS_USER_CONFIG_PROFILE   MCS_PROFILE_FULL
#ifndef MCS_PROFILE
#define MCS_PROFILE               MCS_USER_CONFIG_PROFILE
#endif
/* Memory of the target in KB, for MCS_PROFILE_AUTO (otherwise detected from
 * Zephyr's CONFIG_SRAM_SIZE / CONFIG_FLASH_SIZE or the STM32 / RP2 / nRF52 /
 * SAMD device macros). */
//#define MCS_TARGET_RAM_KB        264
//#define MCS_TARGET_FLASH_KB      2048
#ifndef MCS_ALLOW_SMALL_TARGET
#define MCS_ALLOW_SMALL_TARGET    0    /* 1 = build for < 16 KB RAM / < 64 KB flash anyway */
#endif
/* A board port is compiled in: the auto profile keeps the HAL classes when the
 * flash is >= 128 KB. Set by CMake MICROCS_PORT, ESP-IDF and Zephyr. */
//#define MCS_PORT_HAL             1

/* ================================================================ numbers */
#ifndef MCS_INT64
#define MCS_INT64                 0    /* 1 = 64-bit int/long values, 0 = 32-bit (recommended on Cortex-M0..M4) */
#endif
#ifndef MCS_ENABLE_FLOAT
#define MCS_ENABLE_FLOAT          1    /* float/double and Math; 0 removes all floating-point code */
#endif
#ifndef MCS_FLOAT_DOUBLE
#define MCS_FLOAT_DOUBLE          1    /* 1 = double precision, 0 = single (best for M4F/M33 FPUs) */
#endif
//#define MCS_COMPACT_VALUES       1    /* pack values to 4-byte alignment (default: 1 on 32-bit CPUs, 0 on 64-bit) */

/* ================================================================ compiler and bytecode */
#ifndef MCS_ENABLE_COMPILER
#define MCS_ENABLE_COMPILER       1    /* on-device compiler; 0 = run precompiled .mcsb images only (~60% less code) */
#endif
#ifndef MCS_ENABLE_BYTECODE_LOAD
#define MCS_ENABLE_BYTECODE_LOAD  1    /* load .mcsb images */
#endif
#ifndef MCS_ENABLE_BYTECODE_SAVE
#define MCS_ENABLE_BYTECODE_SAVE  MCS_ENABLE_COMPILER    /* save images */
#endif
#ifndef MCS_ENABLE_XIP
#define MCS_ENABLE_XIP            MCS_ENABLE_BYTECODE_LOAD    /* mcs_exec_image_xip(): run images in place from flash */
#endif
#ifndef MCS_ENABLE_SUPEROPS
#define MCS_ENABLE_SUPEROPS       1    /* the VM runs optimized superinstructions (~1.5 KB of flash) */
#endif
#ifndef MCS_ENABLE_OPTIMIZER
#define MCS_ENABLE_OPTIMIZER      (MCS_ENABLE_COMPILER && MCS_ENABLE_SUPEROPS)    /* the optimizer */
#endif
#ifndef MCS_OPTIMIZE_SOURCE
#define MCS_OPTIMIZE_SOURCE       0    /* 1 = mcs_exec_source() optimizes too (more compile time and RAM) */
#endif
#ifndef MCS_ENABLE_DISASM
#define MCS_ENABLE_DISASM         1    /* disassembler (debug builds, host tool) */
#endif
#ifndef MCS_ENABLE_LINES
#define MCS_ENABLE_LINES          1    /* line tables for error messages and stack traces */
#endif

/* ================================================================ standard library */
#ifndef MCS_ENABLE_LIST
#define MCS_ENABLE_LIST           1    /* List<T> */
#endif
#ifndef MCS_ENABLE_LINQ
#define MCS_ENABLE_LINQ           1    /* Where/Select/OrderBy/GroupBy/Sum/... and Enumerable */
#endif
#ifndef MCS_ENABLE_DICT
#define MCS_ENABLE_DICT           1    /* Dictionary<K,V>, HashSet<T> */
#endif
#ifndef MCS_ENABLE_STACK_QUEUE
#define MCS_ENABLE_STACK_QUEUE    1    /* Stack<T>, Queue<T> (with MCS_ENABLE_LIST) */
#endif
#ifndef MCS_ENABLE_STRINGBUILDER
#define MCS_ENABLE_STRINGBUILDER  1    /* StringBuilder */
#endif
#ifndef MCS_ENABLE_RANDOM
#define MCS_ENABLE_RANDOM         1    /* Random */
#endif
#ifndef MCS_ENABLE_MATH
#define MCS_ENABLE_MATH           MCS_ENABLE_FLOAT    /* Math floating point */
#endif
#ifndef MCS_ENABLE_FORMAT
#define MCS_ENABLE_FORMAT         1    /* String.Format / {x:F2} format specifiers */
#endif
#ifndef MCS_ENABLE_STRING_EXTRA
#define MCS_ENABLE_STRING_EXTRA   1    /* Split/Join/Replace/PadLeft/PadRight/Insert/Remove/LastIndexOf/... (~3 KB) */
#endif
#ifndef MCS_ENABLE_ARRAY_EXTRA
#define MCS_ENABLE_ARRAY_EXTRA    1    /* Array.Sort/Copy/Fill/Find..., List AddRange/RemoveAll/Sort/... (~6 KB) */
#endif
#ifndef MCS_ENABLE_BYTES
#define MCS_ENABLE_BYTES          1    /* Encoding, BitConverter, BinaryPrimitives, Base64 / hex (~3 KB) */
#endif
#ifndef MCS_ENABLE_CONVERT
#define MCS_ENABLE_CONVERT        1    /* the Convert class */
#endif
#ifndef MCS_ENABLE_DIAGNOSTICS
#define MCS_ENABLE_DIAGNOSTICS    1    /* GC, Debug and Stopwatch */
#endif

/* ================================================================ C library */
#ifndef MCS_ENABLE_STDIO
#define MCS_ENABLE_STDIO          1    /* stdout/stdin console and clock() fallbacks; 0 = no stdio at all */
#endif
#ifndef MCS_ENABLE_MALLOC
#define MCS_ENABLE_MALLOC         1    /* realloc()/free() when cfg.realloc_fn is NULL; 0 = pass an allocator */
#endif
#ifndef MCS_TINY_PRINTF
#define MCS_TINY_PRINTF           0    /* 1 = built-in formatter instead of snprintf (saves 2-3 KB on newlib-nano) */
#endif
#ifndef MCS_ENABLE_POOL_HEAP
#define MCS_ENABLE_POOL_HEAP      1    /* built-in fixed-pool allocator (mcs_pool_realloc) */
#endif
#ifndef MCS_POOL_ALIGN
#define MCS_POOL_ALIGN            8    /* pool block alignment; 4 is safe on Cortex-M and saves RAM */
#endif

/* ================================================================ VM speed and memory */
//#define MCS_COMPUTED_GOTO        1    /* computed-goto dispatch (default: 1 with GCC/Clang) */
#ifndef MCS_FIELD_CACHE
#define MCS_FIELD_CACHE           1    /* inline cache for obj.field (~8 B per name constant on 32-bit) */
#endif
#ifndef MCS_LAZY_REGS
#define MCS_LAZY_REGS             1    /* native member tables stay in flash until first use */
#endif
#ifndef MCS_LAZY_CLASSES
#define MCS_LAZY_CLASSES          1    /* built-in classes created on first use (VM starts in 1.7-8 KB) */
#endif
#ifndef MCS_TABLE_MIN_CAP
#define MCS_TABLE_MIN_CAP         4    /* first size of every hash table (power of two, >= 4) */
#endif

/* ================================================================ VM limits */
#ifndef MCS_DEFAULT_STACK
#define MCS_DEFAULT_STACK         1024    /* value-stack slots (cfg.stack_slots = 0) */
#endif
#ifndef MCS_DEFAULT_FRAMES
#define MCS_DEFAULT_FRAMES        64    /* max call depth */
#endif
#ifndef MCS_MAX_HANDLERS
#define MCS_MAX_HANDLERS          32    /* nested try blocks */
#endif
#ifndef MCS_MAX_ROOTS
#define MCS_MAX_ROOTS             32    /* temporary GC roots for natives */
#endif
#ifndef MCS_MAX_PINS
#define MCS_MAX_PINS              16    /* persistent handles for C code (mcs_pin) */
#endif
#ifndef MCS_STACK_MARGIN
#define MCS_STACK_MARGIN          32    /* operand-stack headroom per frame */
#endif
#ifndef MCS_ERROR_SIZE
#define MCS_ERROR_SIZE            256    /* last-error message buffer (mcs_last_error) */
#endif
#ifndef MCS_GC_INITIAL
#define MCS_GC_INITIAL            (16 * 1024)    /* bytes allocated before the first GC */
#endif
#ifndef MCS_GC_GROW
#define MCS_GC_GROW               2    /* next GC threshold multiplier */
#endif
#ifndef MCS_GC_STRESS
#define MCS_GC_STRESS             0    /* debug: full collection on every allocation (very slow) */
#endif
#ifndef MCS_HOOK_INTERVAL
#define MCS_HOOK_INTERVAL         1000    /* backward jumps/calls between hook calls */
#endif
#ifndef MCS_SLEEP_SLICE_MS
#define MCS_SLEEP_SLICE_MS        10    /* Thread.Sleep granularity for abort checks / events */
#endif

/* ================================================================ modules (modules/) */
#ifndef MCS_ENABLE_FS
#define MCS_ENABLE_FS             1    /* VFS + C# File/Directory */
#endif
#ifndef MCS_ENABLE_FLASH
#define MCS_ENABLE_FLASH          MCS_ENABLE_FS    /* NOR/NAND flash layer + SPI NOR/NAND drivers */
#endif
#ifndef MCS_ENABLE_LFS
#define MCS_ENABLE_LFS            0    /* LittleFS backend - also compile lfs.c + lfs_util.c (not bundled) */
#endif
#ifndef MCS_ENABLE_YAFFS
#define MCS_ENABLE_YAFFS          0    /* YAFFS2 backend (GPLv2) - also compile the yaffs2 sources + their CONFIG_YAFFS_* defines */
#endif
#ifndef MCS_ENABLE_TINYFS
#define MCS_ENABLE_TINYFS         0    /* TinyFS backend: built-in power-fail-safe flash filesystem for a few KB of internal MCU flash (no extra sources) */
#endif
#ifndef MCS_ENABLE_HAL
#define MCS_ENABLE_HAL            1    /* C# GPIO/UART/I2C/SPI/ADC/DAC/PWM/Timer/I2S/QSPI/CAN/... */
#endif
#ifndef MCS_ENABLE_DRIVERS
#define MCS_ENABLE_DRIVERS        MCS_ENABLE_HAL    /* device-driver registry + C# Drivers */
#endif
#ifndef MCS_ENABLE_WS2812
#define MCS_ENABLE_WS2812         MCS_ENABLE_DRIVERS    /* built-in "ws2812" driver: C# LedStrip */
#endif
#ifndef MCS_ENABLE_SERVO
#define MCS_ENABLE_SERVO          MCS_ENABLE_DRIVERS    /* built-in "servo" driver: C# Servo (hobby servos on a PWM channel) */
#endif
#ifndef MCS_ENABLE_SCHED
#define MCS_ENABLE_SCHED          1    /* job scheduler */
#endif
#ifndef MCS_ENABLE_SHELL
#define MCS_ENABLE_SHELL          MCS_ENABLE_FS    /* script manager / REPL shell */
#endif
#ifndef MCS_ENABLE_RUNTIME
#define MCS_ENABLE_RUNTIME        MCS_ENABLE_SHELL    /* mcs_runtime_run() */
#endif

/* ---- filesystem */
#ifndef MCS_VFS_MAX_MOUNTS
#define MCS_VFS_MAX_MOUNTS        4
#endif
#ifndef MCS_VFS_PATH_MAX
#define MCS_VFS_PATH_MAX          128
#endif
#ifndef MCS_FLASHFS_MAX
#define MCS_FLASHFS_MAX           2    /* mcs_flashfs_mount() filesystems at the same time */
#endif
#ifndef MCS_LFS_MAX_FILES
#define MCS_LFS_MAX_FILES         4    /* LittleFS files open at the same time */
#endif
#ifndef MCS_FLASH_POLL_MAX
#define MCS_FLASH_POLL_MAX        20000000UL    /* status polls before giving up (no wait fn) */
#endif
#ifndef MCS_FLASH_WAIT_US
#define MCS_FLASH_WAIT_US         50    /* wait fn period */
#endif
#ifndef MCS_YAFFS_OSGLUE
#define MCS_YAFFS_OSGLUE          0    /* 1 = built-in single-threaded yaffs_osglue (malloc, no locks) */
#endif
#ifndef MCS_YAFFS_NAND_INBAND
#define MCS_YAFFS_NAND_INBAND     1    /* tags inside the page data (covered by the chip's ECC) */
#endif
#ifndef MCS_YAFFS_OOB_OFFSET
#define MCS_YAFFS_OOB_OFFSET      2    /* spare-area tags start after the bad-block marker */
#endif
#ifndef MCS_YAFFS_CACHES
#define MCS_YAFFS_CACHES          4    /* short-op cache entries (one chunk of RAM each) */
#endif
#ifndef MCS_YAFFS_RESERVED_BLOCKS
#define MCS_YAFFS_RESERVED_BLOCKS 5
#endif
#ifndef MCS_TINYFS_MAX_FILES
#define MCS_TINYFS_MAX_FILES      16    /* TinyFS: files + directories (RAM index, 24 bytes each) */
#endif
#ifndef MCS_TINYFS_CHUNK
#define MCS_TINYFS_CHUNK          128    /* TinyFS: write buffer per open file = largest data record */
#endif
#ifndef MCS_TINYFS_HANDLES
#define MCS_TINYFS_HANDLES        2    /* TinyFS: files open at the same time */
#endif
#ifndef MCS_TINYFS_MAX_UNIT
#define MCS_TINYFS_MAX_UNIT       64    /* TinyFS: largest flash program unit supported (stack buffer) */
#endif
#ifndef MCS_INTFLASH_SIZE
#define MCS_INTFLASH_SIZE         0    /* bytes of internal MCU flash for files (STM32/RP2 ports; 0 = port default) */
#endif

/* ---- hardware (HAL) and drivers */
#ifndef MCS_HAL_MAX_XFER
#define MCS_HAL_MAX_XFER          256    /* largest transfer per call (buffers on the C stack) */
#endif
#ifndef MCS_HAL_EVENT_QUEUE
#define MCS_HAL_EVENT_QUEUE       32    /* ISR -> VM event ring (power of two) */
#endif
#ifndef MCS_HAL_MAX_CALLBACKS
#define MCS_HAL_MAX_CALLBACKS     16    /* GPIO + timer + user event handlers per VM */
#endif
#ifndef MCS_HAL_MAX_VMS
#define MCS_HAL_MAX_VMS           2    /* VMs with the HAL open at the same time */
#endif
#ifndef MCS_HAL_MAX_BUSES
#define MCS_HAL_MAX_BUSES         4    /* per peripheral type, cached bus settings */
#endif
#ifndef MCS_HAL_SIM_FLASH
#define MCS_HAL_SIM_FLASH         4096    /* simulator board: QSPI NOR size */
#endif
/* Critical section around the event ring when ISRs of different priorities
 * post concurrently (default on Cortex-M: PRIMASK; elsewhere none). */
//#define MCS_HAL_CRITICAL_ENTER() my_irq_lock()
//#define MCS_HAL_CRITICAL_EXIT()  my_irq_unlock()
#ifndef MCS_MAX_DRIVERS
#define MCS_MAX_DRIVERS           8    /* registered drivers (built-in + yours) */
#endif
#ifndef MCS_LEDSTRIP_MAX
#define MCS_LEDSTRIP_MAX          1024    /* LEDs per LedStrip */
#endif
#ifndef MCS_SERVO_PERIOD_US
#define MCS_SERVO_PERIOD_US       20000    /* servo PWM frame (50 Hz) */
#endif
#ifndef MCS_SERVO_FRAME_MS
#define MCS_SERVO_FRAME_MS        20    /* Servo.MoveTo update interval */
#endif
#ifndef MCS_SERVO_MOVE_MAX_MS
#define MCS_SERVO_MOVE_MAX_MS     600000    /* longest Servo.MoveTo time */
#endif

/* ---- scheduler and shell */
#ifndef MCS_SCHED_MAX_JOBS
#define MCS_SCHED_MAX_JOBS        8
#endif
#ifndef MCS_SCHED_PATH_MAX
#define MCS_SCHED_PATH_MAX        64
#endif
#ifndef MCS_SCHED_DURING_SLEEP
#define MCS_SCHED_DURING_SLEEP    1    /* due jobs run while a script waits in Thread.Sleep */
#endif
#ifndef MCS_SHELL_LINE_MAX
#define MCS_SHELL_LINE_MAX        256
#endif
#ifndef MCS_SHELL_REPL_MAX
#define MCS_SHELL_REPL_MAX        1024    /* multi-line REPL buffer; 0 removes the REPL */
#endif

/* ================================================================ board ports (ports/) */
/* ---- ESP32 (ESP-IDF, ports/esp32) */
#ifndef MCS_ESP32_UARTS
#define MCS_ESP32_UARTS           3
#endif
#ifndef MCS_ESP32_I2C_BUSES
#define MCS_ESP32_I2C_BUSES       2
#endif
#ifndef MCS_ESP32_I2C_DEVICES
#define MCS_ESP32_I2C_DEVICES     8    /* cached device handles per bus (new I2C driver) */
#endif
#ifndef MCS_ESP32_SPI_BUSES
#define MCS_ESP32_SPI_BUSES       2    /* C# bus 0 = SPI2_HOST, 1 = SPI3_HOST */
#endif
#ifndef MCS_ESP32_PWM_CHANNELS
#define MCS_ESP32_PWM_CHANNELS    8    /* LEDC channels */
#endif
#ifndef MCS_ESP32_TIMERS
#define MCS_ESP32_TIMERS          4    /* gptimers for Timer.Start (capped at the chip's count) */
#endif
#ifndef MCS_ESP32_UART_RXBUF
#define MCS_ESP32_UART_RXBUF      1024
#endif
#ifndef MCS_ESP32_EVENT_QUEUE
#define MCS_ESP32_EVENT_QUEUE     32
#endif
#ifndef MCS_ESP32_TIMEOUT_MS
#define MCS_ESP32_TIMEOUT_MS      100    /* I2C / SPI transfer timeout */
#endif
#ifndef MCS_ESP32_FS_PATH
#define MCS_ESP32_FS_PATH         "/mcs"    /* VFS mount point of mcs_esp32_littlefs() */
#endif
//#define MCS_ESP32_LEDC_CLK_HZ    80000000u /* LEDC clock used to pick the PWM resolution (C2: 60 MHz, H2: 96 MHz) */
//#define MCS_ESP32_RGB_LED        48   /* GPIO of the "NEOPIXEL" pin name (S3: 48, C3/C6/H2: 8, else -1); also Arduino-ESP32 */
#ifndef MCS_ESP32_LEDSTRIPS
#define MCS_ESP32_LEDSTRIPS       2    /* LedStrips on different pins (RMT channels); also Arduino-ESP32 */
#endif

/* ---- Raspberry Pi RP2040 / RP2350 (pico-sdk, ports/rp2) */
#ifndef MCS_RP2_UART_RXBUF
#define MCS_RP2_UART_RXBUF        256    /* receive ring (power of two) */
#endif
#ifndef MCS_RP2_TIMERS
#define MCS_RP2_TIMERS            4
#endif
#ifndef MCS_RP2_TIMEOUT_US
#define MCS_RP2_TIMEOUT_US        50000    /* I2C / SPI transfer timeout */
#endif
//#define MCS_RP2_FS_SIZE          (1u << 20) /* mcs_rp2_flash_init(&f, 0, 0) region at the end of flash
//                                               (default: half of 2 MB, all but 1 MB of >= 4 MB) */
#ifndef MCS_RP2_LEDSTRIPS
#define MCS_RP2_LEDSTRIPS         4    /* LedStrips on different pins (PIO state machines); also Arduino-Pico */
#endif

/* ---- STM32 (STM32Cube HAL, ports/stm32) */
//#define MCS_STM32_HAL_HEADER     "stm32g4xx_hal.h" /* family header (default: detected) */
#ifndef MCS_STM32_UARTS
#define MCS_STM32_UARTS           9    /* UART.Open(n) table size */
#endif
#ifndef MCS_STM32_UART_RXBUF
#define MCS_STM32_UART_RXBUF      256    /* receive ring per UART (power of two) */
#endif
#ifndef MCS_STM32_BUSES
#define MCS_STM32_BUSES           5    /* I2C / SPI / I2S / CAN / QSPI handles */
#endif
#ifndef MCS_STM32_ADC_CHANNELS
#define MCS_STM32_ADC_CHANNELS    16
#endif
#ifndef MCS_STM32_PWM_CHANNELS
#define MCS_STM32_PWM_CHANNELS    12
#endif
#ifndef MCS_STM32_TIMERS
#define MCS_STM32_TIMERS          4
#endif
#ifndef MCS_STM32_IRQ_PRIORITY
#define MCS_STM32_IRQ_PRIORITY    5    /* EXTI priority (>= configMAX_SYSCALL_INTERRUPT_PRIORITY with FreeRTOS) */
#endif
#ifndef MCS_STM32_TIMEOUT_MS
#define MCS_STM32_TIMEOUT_MS      100    /* I2C / SPI / QSPI transfer timeout */
#endif
//#define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_480CYCLES /* default: the family's longest */
//#define MCS_STM32_FS_BLOCK       2048 /* erase-block size of the region (default: the page/sector size) */
#ifndef MCS_STM32_FS_SIZE
#define MCS_STM32_FS_SIZE         MCS_INTFLASH_SIZE    /* mcs_stm32_flash_init() region; 0 = a quarter of the flash */
#endif
#ifndef MCS_STM32_DEFINE_CALLBACKS
#define MCS_STM32_DEFINE_CALLBACKS 1    /* 0 = your project defines the HAL UART/EXTI callbacks */
#endif
#ifndef MCS_STM32_DEFINE_TIM_CALLBACK
#define MCS_STM32_DEFINE_TIM_CALLBACK 0    /* 1 = the port defines HAL_TIM_PeriodElapsedCallback */
#endif
#ifndef MCS_STM32_EXTI_HANDLERS
#define MCS_STM32_EXTI_HANDLERS   0    /* 1 = the port defines the EXTIx_IRQHandler functions */
#endif

/* ---- Zephyr (ports/zephyr; most settings come from Kconfig and the devicetree) */
#ifndef MCS_ZEPHYR_UART_RXBUF
#define MCS_ZEPHYR_UART_RXBUF     256
#endif
#ifndef MCS_ZEPHYR_TIMERS
#define MCS_ZEPHYR_TIMERS         4
#endif
#ifndef MCS_ZEPHYR_DAC_BITS
#define MCS_ZEPHYR_DAC_BITS       12
#endif
#ifndef MCS_ZEPHYR_I2S_BLOCK
#define MCS_ZEPHYR_I2S_BLOCK      1024    /* bytes per DMA block */
#endif
#ifndef MCS_ZEPHYR_I2S_BLOCKS
#define MCS_ZEPHYR_I2S_BLOCKS     4
#endif
#ifndef MCS_ZEPHYR_FS_FILES
#define MCS_ZEPHYR_FS_FILES       4    /* files open at the same time */
#endif
#ifndef MCS_ZEPHYR_FS_MOUNT
#define MCS_ZEPHYR_FS_MOUNT       "/lfs"
#endif

/* ---- Arduino (ports/arduino) */
#ifndef MCS_ARDUINO_UARTS
#define MCS_ARDUINO_UARTS         4
#endif
#ifndef MCS_ARDUINO_TIMERS
#define MCS_ARDUINO_TIMERS        4
#endif
//#define MCS_ARDUINO_ADC_BITS     12   /* analogReadResolution() (default: 12 on ESP32, else 10) */
//#define MCS_ARDUINO_NO_WIRE           /* defined = no I2C (Wire library) */
//#define MCS_ARDUINO_NO_SPI            /* defined = no SPI library */

#endif /* MCS_USER_CONFIG_H */
