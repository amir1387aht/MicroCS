/* MicroCS build profile: pick a profile from the target's memory.
 *
 *   MCS_TARGET_RAM_KB / MCS_TARGET_FLASH_KB   sizes in KB (set by the build:
 *       CMake MICROCS_RAM_KB/MICROCS_FLASH_KB, PlatformIO board data, Zephyr
 *       CONFIG_SRAM_SIZE/CONFIG_FLASH_SIZE) - or detected below from the
 *       compiler's device macros (STM32 CMSIS, RP2040/RP2350, nRF52, SAMD).
 *
 *   flash < 128 KB or RAM < 32 KB  -> mcs_profile_min.h    (64 KB flash / 16 KB RAM class)
 *   flash < 256 KB                 -> mcs_profile_tiny.h + low-RAM VM limits
 *   RAM < 64 KB                    -> mcs_profile_lowram.h (precompiled images)
 *   RAM < 96 KB                    -> mcs_profile_mcu.h    (precompiled images)
 *   RAM < 256 KB                   -> mcs_profile_embedded.h (on-device compiler + REPL)
 *   otherwise / unknown            -> default configuration (everything on)
 *
 * With MCS_PORT_HAL=1 (board ports) the HAL stays enabled when flash >= 128 KB.
 * Less than 16 KB of RAM stops the build (MCS_ALLOW_SMALL_TARGET=1 to try anyway).
 * Any option can still be overridden with -D on the command line.
 * Use: -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_auto.h"' (CMake: MICROCS_PROFILE=auto). */
#ifndef MCS_PROFILE_AUTO_H
#define MCS_PROFILE_AUTO_H

#ifndef MCS_ALLOW_SMALL_TARGET
#define MCS_ALLOW_SMALL_TARGET 0
#endif

/* ---- memory sizes from the build environment ---- */
#if !defined(MCS_TARGET_RAM_KB) && defined(CONFIG_SRAM_SIZE)          /* Zephyr (KB) */
#define MCS_TARGET_RAM_KB CONFIG_SRAM_SIZE
#if !defined(MCS_TARGET_FLASH_KB) && defined(CONFIG_FLASH_SIZE) && CONFIG_FLASH_SIZE > 0
#define MCS_TARGET_FLASH_KB CONFIG_FLASH_SIZE
#endif
#endif
#if !defined(MCS_TARGET_RAM_KB)
#include "mcs_target_stm32.h"          /* STM32 CMSIS device macros (no-op otherwise) */
#endif
#if !defined(MCS_TARGET_RAM_KB)
#if defined(__AVR__)
#error "MicroCS does not support 8-bit AVR (2-16 KB RAM); use a 32-bit MCU with 16 KB+ RAM"
#elif defined(PICO_RP2350) || defined(ARDUINO_ARCH_RP2350)
#define MCS_TARGET_RAM_KB 520
#elif defined(PICO_RP2040) || defined(ARDUINO_ARCH_RP2040)
#define MCS_TARGET_RAM_KB 264
#elif defined(NRF52840_XXAA)
#define MCS_TARGET_RAM_KB 256
#elif defined(NRF52833_XXAA)
#define MCS_TARGET_RAM_KB 128
#elif defined(NRF52832_XXAA)
#define MCS_TARGET_RAM_KB 64
#elif defined(NRF52820_XXAA)
#define MCS_TARGET_RAM_KB 32
#define MCS_TARGET_FLASH_KB 256
#elif defined(NRF52810_XXAA) || defined(NRF52811_XXAA) || defined(NRF52805_XXAA)
#define MCS_TARGET_RAM_KB 24
#define MCS_TARGET_FLASH_KB 192
#elif defined(__SAMD51__)
#define MCS_TARGET_RAM_KB 192
#elif defined(__SAMD21G18A__) || defined(__SAMD21E18A__) || defined(__SAMD21J18A__)
#define MCS_TARGET_RAM_KB 32
#define MCS_TARGET_FLASH_KB 256
#elif defined(__SAMD21G17A__) || defined(__SAMD21E17A__)
#define MCS_TARGET_RAM_KB 16
#define MCS_TARGET_FLASH_KB 128
#endif
#endif

/* ---- the profile ---- */
#if defined(MCS_TARGET_RAM_KB) && MCS_TARGET_RAM_KB < 16 && !MCS_ALLOW_SMALL_TARGET
#error "MicroCS needs at least 16 KB of RAM (MCS_TARGET_RAM_KB < 16)"
#elif defined(MCS_TARGET_FLASH_KB) && MCS_TARGET_FLASH_KB < 64 && !MCS_ALLOW_SMALL_TARGET
#error "MicroCS needs at least 64 KB of flash (MCS_TARGET_FLASH_KB < 64)"
#endif

/* A board port (CMake MICROCS_PORT sets MCS_PORT_HAL=1) keeps the HAL classes
 * whenever the flash has room for them (~26 KB on Cortex-M0), even in the
 * smaller profiles - the HAL is the reason to run on that board. */
#if defined(MCS_PORT_HAL) && MCS_PORT_HAL && !defined(MCS_ENABLE_HAL) && \
    !(defined(MCS_TARGET_FLASH_KB) && MCS_TARGET_FLASH_KB < 128)
#define MCS_ENABLE_HAL 1
#endif

#if !defined(MCS_TARGET_RAM_KB)
/* unknown target: default configuration */
#elif (defined(MCS_TARGET_FLASH_KB) && MCS_TARGET_FLASH_KB < 128) || MCS_TARGET_RAM_KB < 32
#include "mcs_profile_min.h"
#elif defined(MCS_TARGET_FLASH_KB) && MCS_TARGET_FLASH_KB < 256
#include "mcs_profile_tiny.h"
#ifndef MCS_MAX_HANDLERS
#define MCS_MAX_HANDLERS 12
#endif
#ifndef MCS_MAX_ROOTS
#define MCS_MAX_ROOTS 16
#endif
#ifndef MCS_GC_INITIAL
#define MCS_GC_INITIAL (4 * 1024)
#endif
#ifndef MCS_POOL_ALIGN
#define MCS_POOL_ALIGN 4
#endif
#elif MCS_TARGET_RAM_KB < 64
#include "mcs_profile_lowram.h"
#elif MCS_TARGET_RAM_KB < 96
#include "mcs_profile_mcu.h"
#elif MCS_TARGET_RAM_KB < 256
#include "mcs_profile_embedded.h"
#endif

#endif
