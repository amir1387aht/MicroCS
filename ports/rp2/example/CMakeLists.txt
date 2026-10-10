# MicroCS firmware for Raspberry Pi Pico / Pico 2: a C# REPL on USB, all peripherals.
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   cmake -B build -DPICO_BOARD=pico -DMICROCS_LITTLEFS_DIR=/path/to/littlefs   (pico2 for RP2350)
#   cmake --build build                   -> build/microcs_pico.uf2
# Files: LittleFS on the on-board flash. Its sources are not bundled: give the folder
#   (-DMICROCS_LITTLEFS_DIR=..., git clone -b v2.9.3 https://github.com/littlefs-project/littlefs),
#   keep a copy in third_party/littlefs, or add -DMICROCS_FS_DOWNLOAD=ON to fetch v2.9.3 once.
#   -DMICROCS_FS=yaffs2 for YAFFS2 (GPLv2; MICROCS_YAFFS2_DIR), -DMICROCS_FS= for a RAM disk only.
#   -DMICROCS_WS2812=OFF leaves out the LedStrip driver (and hardware_pio).
# Threads on both cores (C# Thread.RunOn(1, ...), `thread core=1` jobs; docs/THREADS.md):
#   -DMICROCS_OS=freertos -DFREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel   (Raspberry Pi's fork,
#   github.com/raspberrypi/FreeRTOS-Kernel: SMP on RP2040 and RP2350; config in freertos/FreeRTOSConfig.h).
#   The release .uf2 files are built this way. Default: no OS, single-threaded as always.
# Options: a mcs_user_config.h next to this file (template: MicroCS/config/mcs_user_config.h)
#   is used automatically, e.g. #define MCS_RP2_FS_SIZE (512 * 1024); -D options win over it.
cmake_minimum_required(VERSION 3.13)
# PICO_SDK_PATH may come from the environment or from -DPICO_SDK_PATH=...
if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH)
  message(FATAL_ERROR "Set PICO_SDK_PATH (environment or -DPICO_SDK_PATH=...)")
endif()
set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Raspberry Pi Pico SDK")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(microcs_pico C CXX ASM)
pico_sdk_init()
if(MICROCS_OS STREQUAL "freertos")
  if(NOT FREERTOS_KERNEL_PATH AND DEFINED ENV{FREERTOS_KERNEL_PATH})
    set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
  endif()
  if(NOT FREERTOS_KERNEL_PATH OR NOT EXISTS "${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake")
    message(FATAL_ERROR "MICROCS_OS=freertos: set FREERTOS_KERNEL_PATH (environment or -DFREERTOS_KERNEL_PATH=...) to "
      "Raspberry Pi's FreeRTOS-Kernel (RP2040 + RP2350 SMP ports, the one pico-examples uses): "
      "git clone https://github.com/raspberrypi/FreeRTOS-Kernel")
  endif()
  # after pico_sdk_init(): PICO_PLATFORM picks the RP2040 or RP2350_ARM_NTZ port
  include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
  set(MICROCS_FREERTOS_CONFIG_DIR ${CMAKE_CURRENT_LIST_DIR}/freertos)
endif()

set(MICROCS_PORT rp2)
if(NOT DEFINED MICROCS_FS)
  set(MICROCS_FS littlefs)
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../.. microcs)

add_executable(microcs_pico main.c)
target_link_libraries(microcs_pico PRIVATE microcs)
if(MICROCS_OS STREQUAL "freertos")
  target_link_libraries(microcs_pico PRIVATE FreeRTOS-Kernel-Heap4)
endif()
pico_enable_stdio_usb(microcs_pico 1)
pico_enable_stdio_uart(microcs_pico 0)
pico_add_extra_outputs(microcs_pico)
