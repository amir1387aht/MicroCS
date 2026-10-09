# MicroCS firmware for Raspberry Pi Pico / Pico 2: a C# REPL on USB, all peripherals.
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   cmake -B build -DPICO_BOARD=pico -DMICROCS_LITTLEFS_DIR=/path/to/littlefs   (pico2 for RP2350)
#   cmake --build build                   -> build/microcs_pico.uf2
# Files: LittleFS on the on-board flash. Its sources are not bundled: give the folder
#   (-DMICROCS_LITTLEFS_DIR=..., git clone -b v2.9.3 https://github.com/littlefs-project/littlefs),
#   keep a copy in third_party/littlefs, or add -DMICROCS_FS_DOWNLOAD=ON to fetch v2.9.3 once.
#   -DMICROCS_FS=yaffs2 for YAFFS2 (GPLv2; MICROCS_YAFFS2_DIR), -DMICROCS_FS= for a RAM disk only.
#   -DMICROCS_WS2812=OFF leaves out the LedStrip driver (and hardware_pio).
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

set(MICROCS_PORT rp2)
if(NOT DEFINED MICROCS_FS)
  set(MICROCS_FS littlefs)
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../.. microcs)

add_executable(microcs_pico main.c)
target_link_libraries(microcs_pico PRIVATE microcs)
pico_enable_stdio_usb(microcs_pico 1)
pico_enable_stdio_uart(microcs_pico 0)
pico_add_extra_outputs(microcs_pico)
