/* MicroCS - STM32 device line -> MCS_TARGET_RAM_KB / MCS_TARGET_FLASH_KB.
 * Included by mcs_profile_auto.h when a CMSIS device macro (STM32F072xB,
 * STM32G071xx, ...) is defined. Where one macro covers several memory sizes
 * (e.g. STM32F103xB = F103x8 64 KB and F103xB 128 KB flash) the smallest one is
 * used; define MCS_TARGET_RAM_KB / MCS_TARGET_FLASH_KB yourself to override.
 *
 * Not supported (less than 16 KB of SRAM, or less than 64 KB of flash):
 *   C0: C011, C031, C051          F0: F030x4/x6/x8, F031, F038, F042, F048, F051, F058, F070x6
 *   F1: F100 (except xE), F101x4..xB, F102, F103x4/x6
 *   F3: F301x6, F302x6, F303x6/x8, F328, F334 (12 KB SRAM + 4 KB CCM)
 *   G0: G030, G031, G041 (G050x6/G051x6/G061x6/G431x6 with 32 KB flash share a macro with
 *       their 64 KB versions and are not detected - do not use them)
 *   L0: L010x4..x8, L011, L021, L031, L041, L051, L052, L053, L062, L063
 *   L1: L100x6/x8/xB, L151x6/x8, L152x6/x8   U0: U031
 * Their device macros stop the build with #error (MCS_ALLOW_SMALL_TARGET=1 skips it). */
#ifndef MCS_TARGET_STM32_H
#define MCS_TARGET_STM32_H

#define MCS_STM32_TOO_SMALL 0
/* ---- below the 16 KB RAM / 64 KB flash minimum ---- */
#if defined(STM32C011xx) || defined(STM32C031xx) || defined(STM32C051xx) || \
    defined(STM32F030x6) || defined(STM32F030x8) || defined(STM32F031x6) || defined(STM32F038xx) || \
    defined(STM32F042x6) || defined(STM32F048xx) || defined(STM32F051x8) || defined(STM32F058xx) || \
    defined(STM32F070x6) || \
    defined(STM32F100xB) || defined(STM32F101x6) || defined(STM32F101xB) || defined(STM32F102x6) || \
    defined(STM32F102xB) || defined(STM32F103x6) || \
    defined(STM32F301x6) || defined(STM32F302x6) || defined(STM32F303x6) || defined(STM32F303x8) || \
    defined(STM32F328xx) || defined(STM32F334x4) || defined(STM32F334x6) || defined(STM32F334x8) || \
    defined(STM32G030xx) || defined(STM32G031xx) || defined(STM32G041xx) || \
    defined(STM32L010x4) || defined(STM32L010x6) || defined(STM32L010x8) || defined(STM32L011xx) || \
    defined(STM32L021xx) || defined(STM32L031xx) || defined(STM32L041xx) || defined(STM32L051xx) || \
    defined(STM32L052xx) || defined(STM32L053xx) || defined(STM32L062xx) || defined(STM32L063xx) || \
    defined(STM32L100xB) || defined(STM32L151xB) || defined(STM32L152xB) || \
    defined(STM32U031xx)
#undef MCS_STM32_TOO_SMALL
#define MCS_STM32_TOO_SMALL 1
#define MCS_TARGET_RAM_KB 12
/* ---- 16 KB+ RAM, 64 KB flash (smallest size of the line): the minimal profile ---- */
#elif defined(STM32F103xB) || defined(STM32F071xB) || defined(STM32F301x8) || defined(STM32F302x8) || \
      defined(STM32F318xx) || defined(STM32G050xx) || defined(STM32G051xx) || defined(STM32G061xx) || \
      defined(STM32G071xx) || defined(STM32C071xx) || defined(STM32L071xx) || defined(STM32L072xx) || \
      defined(STM32L073xx) || defined(STM32L081xx) || defined(STM32L082xx) || defined(STM32L083xx) || \
      defined(STM32G431xx) || defined(STM32L412xx) || defined(STM32L422xx)
#define MCS_TARGET_RAM_KB 16
#define MCS_TARGET_FLASH_KB 64
/* ---- 16-32 KB RAM, 128 KB+ flash ---- */
#elif defined(STM32F070xB) || defined(STM32F072xB) || defined(STM32F078xx) || defined(STM32L010xB) || \
      defined(STM32L100xBA) || defined(STM32L100xC) || defined(STM32L151xBA) || defined(STM32L152xBA) || \
      defined(STM32F030xC) || defined(STM32F091xC) || defined(STM32F098xx) || defined(STM32G070xx) || \
      defined(STM32G081xx) || defined(STM32C091xx) || defined(STM32C092xx) || defined(STM32L151xC) || \
      defined(STM32L152xC) || defined(STM32L162xC) || defined(STM32F302xC) || defined(STM32F303xC) || \
      defined(STM32F358xx) || defined(STM32G441xx) || defined(STM32U073xx) || defined(STM32U083xx) || \
      defined(STM32H503xx) || defined(STM32F100xE) || defined(STM32F373xC) || defined(STM32F378xx) || \
      defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F410Tx)
#define MCS_TARGET_RAM_KB 16
#define MCS_TARGET_FLASH_KB 128
/* ---- 32-96 KB RAM, mostly 256 KB+ flash (the 128 KB versions - F105x8, F401xB,
 *      L431xB, L432xB, L433xB, WLE5x8 - need MCS_TARGET_FLASH_KB=128) ---- */
#elif defined(STM32F101xE) || defined(STM32F103xE) || defined(STM32F105xC) || defined(STM32F107xC) || \
      defined(STM32F302xE) || defined(STM32F303xE) || defined(STM32F398xx) || defined(STM32F401xC) || \
      defined(STM32F205xx) || defined(STM32F215xx) || defined(STM32L431xx) || defined(STM32L432xx) || \
      defined(STM32L433xx) || defined(STM32L442xx) || defined(STM32L443xx) || defined(STM32WL54xx) || \
      defined(STM32WL55xx) || defined(STM32WLE5xx) || defined(STM32WLE4xx) || defined(STM32WB15xx) || \
      defined(STM32WB1Mxx) || defined(STM32L151xD) || defined(STM32L152xD) || defined(STM32L162xD) || \
      defined(STM32L151xE) || defined(STM32L152xE) || defined(STM32L162xE) || defined(STM32L151xDX) || \
      defined(STM32L152xDX) || defined(STM32L162xDX) || defined(STM32F101xG) || defined(STM32F103xG)
#define MCS_TARGET_RAM_KB 32
#define MCS_TARGET_FLASH_KB 256
/* ---- 96-192 KB RAM, 256 KB+ flash ---- */
#elif defined(STM32F401xE) || defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || \
      defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F446xx) || defined(STM32F405xx) || \
      defined(STM32F415xx) || defined(STM32F407xx) || defined(STM32F417xx) || defined(STM32F413xx) || \
      defined(STM32F423xx) || defined(STM32F207xx) || defined(STM32F217xx) || defined(STM32G0B0xx) || \
      defined(STM32G0B1xx) || defined(STM32G0C1xx) || defined(STM32G473xx) || defined(STM32G474xx) || \
      defined(STM32G483xx) || defined(STM32G484xx) || defined(STM32G491xx) || defined(STM32G4A1xx) || \
      defined(STM32L451xx) || defined(STM32L452xx) || defined(STM32L462xx) || defined(STM32L471xx) || \
      defined(STM32L475xx) || defined(STM32L476xx) || defined(STM32L486xx) || defined(STM32WB35xx) || \
      defined(STM32WB55xx) || defined(STM32WB5Mxx) || defined(STM32L552xx) || defined(STM32L562xx)
#define MCS_TARGET_RAM_KB 96
#define MCS_TARGET_FLASH_KB 256
/* ---- 192 KB+ RAM (F42x/F43x/F469, F7, H5, H7, L4+, U5, ...) ---- */
#elif defined(STM32F427xx) || defined(STM32F437xx) || defined(STM32F429xx) || defined(STM32F439xx) || \
      defined(STM32F469xx) || defined(STM32F479xx) || defined(STM32L496xx) || defined(STM32L4A6xx)
#define MCS_TARGET_RAM_KB 192
#define MCS_TARGET_FLASH_KB 512
#endif

#if MCS_STM32_TOO_SMALL && !MCS_ALLOW_SMALL_TARGET
#error "MicroCS needs an STM32 with at least 16 KB of SRAM and 64 KB of flash; this device line has less (see include/profiles/mcs_target_stm32.h). Define MCS_TARGET_RAM_KB/MCS_TARGET_FLASH_KB if your part is bigger, or MCS_ALLOW_SMALL_TARGET=1 to try anyway."
#endif
#endif
