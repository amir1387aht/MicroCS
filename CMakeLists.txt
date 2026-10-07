# MicroCS - one CMakeLists.txt for every CMake-based build:
#
#   * plain CMake / any toolchain:  add_subdirectory(MicroCS) + target_link_libraries(app PRIVATE microcs)
#   * ESP-IDF:                      the repository is an IDF component (EXTRA_COMPONENT_DIRS or idf_component.yml)
#   * Raspberry Pi pico-sdk:        set(MICROCS_PORT rp2) before add_subdirectory
#   * STM32CubeMX / CubeIDE CMake:  set(MICROCS_PORT stm32); links the generated `stm32cubemx` target
#   * Zephyr:                       see zephyr/ (module.yml points there)
#   * host:                         cmake -B build && cmake --build build   -> build/mcs (CLI + REPL)
#
# Options:
#   MICROCS_PROFILE   "" | auto | min | tiny | lowram | mcu | embedded | linux   (include/profiles/mcs_profile_<name>.h)
#                     auto = pick from MICROCS_RAM_KB / MICROCS_FLASH_KB or the target's device macros
#                     (default for MICROCS_PORT=stm32: the STM32 device macro selects it)
#   MICROCS_RAM_KB / MICROCS_FLASH_KB   target memory sizes for the auto profile (optional)
#   MICROCS_CONFIG    path of your own config header (overrides MICROCS_PROFILE)
#   MICROCS_PORT      "" | stm32 | rp2 | esp32 | template          (adds ports/<port>/mcs_port_<port>.c)
#   MICROCS_MODULES   ON: filesystem, HAL, scheduler, shell/REPL, runtime (default ON)
cmake_minimum_required(VERSION 3.13)

set(MICROCS_DIR ${CMAKE_CURRENT_LIST_DIR})
file(GLOB MICROCS_CORE_SRCS ${MICROCS_DIR}/src/*.c)
file(GLOB MICROCS_MODULE_SRCS ${MICROCS_DIR}/modules/*/*.c)

# ------------------------------------------------------------------ ESP-IDF component
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${MICROCS_CORE_SRCS} ${MICROCS_MODULE_SRCS} ports/esp32/mcs_port_esp32.c
        INCLUDE_DIRS include ports/esp32
        REQUIRES driver esp_adc esp_timer
        PRIV_REQUIRES esp_system)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)
    # menuconfig > Component config > MicroCS (Kconfig); the ESP32-C2 defaults to "embedded"
    foreach(p EMBEDDED MCU LOWRAM MIN)
        if(CONFIG_MICROCS_PROFILE_${p})
            string(TOLOWER ${p} pl)
            target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_USER_CONFIG_FILE="profiles/mcs_profile_${pl}.h")
        endif()
    endforeach()
    # the port is the HAL: keep it in every profile (all ESP32 parts have >= 1 MB of flash)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_HAL=1 MCS_PORT_HAL=1)
    return()
endif()

# (a plain set() before add_subdirectory() wins over these cache defaults)
if(NOT DEFINED MICROCS_PROFILE)
    if(MICROCS_PORT STREQUAL "stm32")
        set(MICROCS_PROFILE "auto" CACHE STRING "MicroCS configuration profile (auto, min, tiny, lowram, mcu, embedded, linux)")
    else()
        set(MICROCS_PROFILE "" CACHE STRING "MicroCS configuration profile (auto, min, tiny, lowram, mcu, embedded, linux)")
    endif()
endif()
if(NOT DEFINED MICROCS_CONFIG)
    set(MICROCS_CONFIG "" CACHE STRING "Path of a user config header (MCS_USER_CONFIG_FILE)")
endif()
if(NOT DEFINED MICROCS_PORT)
    set(MICROCS_PORT "" CACHE STRING "Board port to compile in (stm32, rp2, esp32, template)")
endif()
if(NOT DEFINED MICROCS_MODULES)
    option(MICROCS_MODULES "Build the optional modules (fs, hal, sched, shell, runtime)" ON)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MicroCS VERSION 1.6.0 LANGUAGES C)
endif()

set(MICROCS_SRCS ${MICROCS_CORE_SRCS})
if(MICROCS_MODULES)
    list(APPEND MICROCS_SRCS ${MICROCS_MODULE_SRCS})
endif()
if(MICROCS_PORT)
    list(APPEND MICROCS_SRCS ${MICROCS_DIR}/ports/${MICROCS_PORT}/mcs_port_${MICROCS_PORT}.c)
endif()

add_library(microcs STATIC ${MICROCS_SRCS})
add_library(MicroCS::microcs ALIAS microcs)
target_include_directories(microcs PUBLIC ${MICROCS_DIR}/include)
if(MICROCS_PORT)
    target_include_directories(microcs PUBLIC ${MICROCS_DIR}/ports/${MICROCS_PORT})
endif()
set_target_properties(microcs PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)   # gnu99 is enough for MicroCS; vendor SDK headers need C11

if(MICROCS_CONFIG)
    target_compile_definitions(microcs PUBLIC MCS_USER_CONFIG_FILE="${MICROCS_CONFIG}")
elseif(MICROCS_PROFILE)
    target_compile_definitions(microcs PUBLIC MCS_USER_CONFIG_FILE="profiles/mcs_profile_${MICROCS_PROFILE}.h")
endif()
if(MICROCS_PORT)
    target_compile_definitions(microcs PUBLIC MCS_PORT_HAL=1)
endif()
if(MICROCS_RAM_KB)
    target_compile_definitions(microcs PUBLIC MCS_TARGET_RAM_KB=${MICROCS_RAM_KB})
endif()
if(MICROCS_FLASH_KB)
    target_compile_definitions(microcs PUBLIC MCS_TARGET_FLASH_KB=${MICROCS_FLASH_KB})
endif()
if(NOT MICROCS_MODULES)
    target_compile_definitions(microcs PUBLIC MCS_ENABLE_FS=0 MCS_ENABLE_HAL=0 MCS_ENABLE_SCHED=0 MCS_ENABLE_SHELL=0)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(microcs PRIVATE -Wall -Wextra)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    find_library(MICROCS_LIBM m)
    if(MICROCS_LIBM)
        target_link_libraries(microcs PUBLIC ${MICROCS_LIBM})
    endif()
endif()

# ------------------------------------------------------------------ vendor SDKs
if(MICROCS_PORT STREQUAL "rp2")
    target_link_libraries(microcs PUBLIC pico_stdlib pico_unique_id hardware_i2c hardware_spi
                          hardware_adc hardware_pwm hardware_irq hardware_watchdog hardware_clocks)
elseif(MICROCS_PORT STREQUAL "stm32")
    # STM32CubeMX >= 6.11 generates an INTERFACE target with the HAL includes + defines
    if(TARGET stm32cubemx)
        target_link_libraries(microcs PUBLIC stm32cubemx)
    elseif(DEFINED MICROCS_STM32_TARGET)
        target_link_libraries(microcs PUBLIC ${MICROCS_STM32_TARGET})
    endif()
endif()

# ------------------------------------------------------------------ host CLI
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_CROSSCOMPILING AND NOT MICROCS_PORT)
    add_executable(mcs ${MICROCS_DIR}/ports/unix/main.c)
    target_link_libraries(mcs PRIVATE microcs)
endif()
