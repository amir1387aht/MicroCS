# MicroCS - one CMakeLists.txt for every CMake-based build:
#
#   * plain CMake / any toolchain:  add_subdirectory(MicroCS) + target_link_libraries(app PRIVATE microcs)
#   * ESP-IDF:                      clone the repository into components/MicroCS (it is an IDF component)
#   * Raspberry Pi pico-sdk:        set(MICROCS_PORT rp2) before add_subdirectory
#   * STM32CubeMX / CubeIDE CMake:  set(MICROCS_PORT stm32); links the generated `stm32cubemx` target
#   * Zephyr:                       see zephyr/ (module.yml points there)
#   * host:                         cmake -B build && cmake --build build   -> build/mcs (CLI + REPL)
#
# Configuration: put a mcs_user_config.h (template: config/mcs_user_config.h) next to the top-level
# CMakeLists.txt or in its include/, src/ or main/ folder - it is found automatically and every
# MCS_* option can be set there. The options below still work and win over the header.
#
# Options:
#   MICROCS_CONFIG    path of the project config header (default: the mcs_user_config.h found as
#                     above; relative paths start at the top-level source dir; NONE = do not use one)
#   MICROCS_PROFILE   "" | full | auto | min | tiny | lowram | mcu | embedded | linux   (include/profiles/)
#                     auto = pick from MICROCS_RAM_KB / MICROCS_FLASH_KB or the target's device macros
#                     (default for MICROCS_PORT=stm32: the STM32 device macro selects it).
#                     MCS_PROFILE in mcs_user_config.h wins over it.
#   MICROCS_RAM_KB / MICROCS_FLASH_KB   target memory sizes for the auto profile (optional)
#   MICROCS_PORT      "" | stm32 | rp2 | esp32 | template          (adds ports/<port>/mcs_port_<port>.c)
#   MICROCS_MODULES   ON: filesystem, HAL, scheduler, shell/REPL, runtime (default ON)
#   MICROCS_FS        "" | littlefs | yaffs2: compile a flash filesystem in -> mcs_flashfs_mount() on the
#                     port's flash driver. Sources are not bundled: MICROCS_LITTLEFS_DIR / MICROCS_YAFFS2_DIR,
#                     a copy found next to the project, or -DMICROCS_FS_DOWNLOAD=ON (cmake/MicroCSFS.cmake)
#   MICROCS_WS2812    ON: the built-in "ws2812" driver (C# LedStrip; RP2: links hardware_pio) (default ON)
#   MICROCS_SERVO     ON: the built-in "servo" driver (C# Servo on a PWM channel) (default ON)
cmake_minimum_required(VERSION 3.13)

set(MICROCS_DIR ${CMAKE_CURRENT_LIST_DIR})
file(GLOB MICROCS_CORE_SRCS ${MICROCS_DIR}/src/*.c)
file(GLOB MICROCS_MODULE_SRCS ${MICROCS_DIR}/modules/*/*.c)

# The project config header -> MICROCS_CONFIG_DEFS (MCS_USER_CONFIG_FILE="<abs path>").
# config: MICROCS_CONFIG ("" = search, NONE = none); root: the project folder to search
function(microcs_config_defs config root)
    set(_cfg "")
    set(_defs "")
    if(config STREQUAL "NONE")
        set(_defs "MCS_USER_CONFIG=0")
    elseif(config)
        if(IS_ABSOLUTE "${config}")
            set(_cfg "${config}")
        elseif(EXISTS "${root}/${config}")
            get_filename_component(_cfg "${root}/${config}" ABSOLUTE)
        else()
            set(_cfg "${config}")                     # found on the include path
        endif()
    else()
        foreach(d "" "include/" "src/" "main/")
            if(NOT _cfg AND EXISTS "${root}/${d}mcs_user_config.h")
                set(_cfg "${root}/${d}mcs_user_config.h")
            endif()
        endforeach()
    endif()
    if(_cfg)
        set(_defs "MCS_USER_CONFIG_FILE=\"${_cfg}\"")
        message(STATUS "MicroCS: config header ${_cfg}")
    endif()
    set(MICROCS_CONFIG_DEFS ${_defs} PARENT_SCOPE)
endfunction()

# MICROCS_PROFILE name -> MICROCS_PROFILE_DEFS (MCS_DEFAULT_PROFILE=MCS_PROFILE_<NAME>)
function(microcs_profile_defs name)
    set(_defs "")
    if(name)
        string(TOUPPER "${name}" _p)
        if(NOT _p MATCHES "^(FULL|AUTO|EMBEDDED|MCU|LOWRAM|TINY|MIN|LINUX)$")
            message(FATAL_ERROR "MicroCS: unknown MICROCS_PROFILE '${name}' (full, auto, embedded, mcu, lowram, tiny, min, linux)")
        endif()
        set(_defs "MCS_DEFAULT_PROFILE=MCS_PROFILE_${_p}")
    endif()
    set(MICROCS_PROFILE_DEFS ${_defs} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------ ESP-IDF component
if(ESP_PLATFORM)
    set(_mcs_fs_srcs "")
    set(_mcs_fs_incs "")
    if(CONFIG_MICROCS_FS_YAFFS2)        # menuconfig > MicroCS > Filesystem
        include(${MICROCS_DIR}/cmake/MicroCSFS.cmake)
        microcs_yaffs2_sources()
        set(_mcs_fs_srcs ${MICROCS_YAFFS_SRCS})
        set(_mcs_fs_incs ${MICROCS_YAFFS_INC})
    endif()
    idf_component_register(
        SRCS ${MICROCS_CORE_SRCS} ${MICROCS_MODULE_SRCS} ports/esp32/mcs_port_esp32.c ${_mcs_fs_srcs}
        INCLUDE_DIRS include ports/esp32 ${_mcs_fs_incs}
        REQUIRES driver esp_adc esp_timer esp_partition joltwallet__littlefs
        PRIV_REQUIRES esp_system)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)
    if(NOT CONFIG_MICROCS_WS2812)       # menuconfig > MicroCS > WS2812 / NeoPixel driver
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_WS2812=0)
    endif()
    if(NOT CONFIG_MICROCS_SERVO)        # menuconfig > MicroCS > Servo driver
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_SERVO=0)
    endif()
    if(CONFIG_MICROCS_FS_TINYFS)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_TINYFS=1)
    endif()
    if(CONFIG_MICROCS_FS_YAFFS2)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_YAFFS=1 MCS_YAFFS_OSGLUE=1 ${MICROCS_YAFFS_DEFS})
        set_source_files_properties(${_mcs_fs_srcs} PROPERTIES COMPILE_OPTIONS "-w;-include;sys/types.h")
    endif()
    # menuconfig > Component config > MicroCS (Kconfig); the ESP32-C2 defaults to "embedded".
    # MCS_PROFILE in mcs_user_config.h wins over it.
    foreach(p EMBEDDED MCU LOWRAM MIN)
        if(CONFIG_MICROCS_PROFILE_${p})
            target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_DEFAULT_PROFILE=MCS_PROFILE_${p})
        endif()
    endforeach()
    # project config header: MICROCS_CONFIG (CMake), menuconfig > MicroCS > Project config header,
    # or mcs_user_config.h in the project folder, its main/ or include/
    idf_build_get_property(_mcs_project_dir PROJECT_DIR)
    if(NOT DEFINED MICROCS_CONFIG)
        set(MICROCS_CONFIG "${CONFIG_MICROCS_USER_CONFIG_FILE}")
    endif()
    microcs_config_defs("${MICROCS_CONFIG}" "${_mcs_project_dir}")
    if(MICROCS_CONFIG_DEFS)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC ${MICROCS_CONFIG_DEFS})
    endif()
    # the port is the HAL: keep it in every profile (all ESP32 parts have >= 1 MB of flash)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_HAL=1 MCS_PORT_HAL=1)
    return()
endif()

# (a plain set() before add_subdirectory() wins over these cache defaults)
if(NOT DEFINED MICROCS_PROFILE)
    if(MICROCS_PORT STREQUAL "stm32")
        set(MICROCS_PROFILE "auto" CACHE STRING "MicroCS configuration profile (full, auto, min, tiny, lowram, mcu, embedded, linux)")
    else()
        set(MICROCS_PROFILE "" CACHE STRING "MicroCS configuration profile (full, auto, min, tiny, lowram, mcu, embedded, linux)")
    endif()
endif()
if(NOT DEFINED MICROCS_CONFIG)
    set(MICROCS_CONFIG "" CACHE STRING "Project config header (empty: find mcs_user_config.h in the project, NONE: none)")
endif()
if(NOT DEFINED MICROCS_PORT)
    set(MICROCS_PORT "" CACHE STRING "Board port to compile in (stm32, rp2, esp32, template)")
endif()
if(NOT DEFINED MICROCS_FS)
    set(MICROCS_FS "" CACHE STRING "Flash filesystem to compile in: tinyfs (built in), littlefs, yaffs2 (GPLv2) or empty")
endif()
if(NOT DEFINED MICROCS_FS_SIZE)
    set(MICROCS_FS_SIZE "" CACHE STRING "Bytes of the MCU's internal flash used for files (STM32/RP2 ports; empty = port default)")
endif()
if(NOT DEFINED MICROCS_MODULES)
    option(MICROCS_MODULES "Build the optional modules (fs, hal, drivers, sched, shell, runtime)" ON)
endif()
if(NOT DEFINED MICROCS_WS2812)
    option(MICROCS_WS2812 "Built-in ws2812 driver: C# LedStrip (WS2812 / SK6812 strips)" ON)
endif()
if(NOT DEFINED MICROCS_SERVO)
    option(MICROCS_SERVO "Built-in servo driver: C# Servo (hobby servos on a PWM channel)" ON)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MicroCS VERSION 1.9.1 LANGUAGES C)
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

microcs_config_defs("${MICROCS_CONFIG}" "${CMAKE_SOURCE_DIR}")
microcs_profile_defs("${MICROCS_PROFILE}")
if(MICROCS_CONFIG_DEFS OR MICROCS_PROFILE_DEFS)
    target_compile_definitions(microcs PUBLIC ${MICROCS_CONFIG_DEFS} ${MICROCS_PROFILE_DEFS})
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
elseif(MICROCS_FS)
    include(${MICROCS_DIR}/cmake/MicroCSFS.cmake)
    microcs_add_fs(microcs "${MICROCS_FS}")
endif()
if(MICROCS_FS_SIZE)
    target_compile_definitions(microcs PUBLIC MCS_INTFLASH_SIZE=${MICROCS_FS_SIZE})
endif()
if(NOT MICROCS_WS2812)
    target_compile_definitions(microcs PUBLIC MCS_ENABLE_WS2812=0)
endif()
if(NOT MICROCS_SERVO)
    target_compile_definitions(microcs PUBLIC MCS_ENABLE_SERVO=0)
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
                          hardware_adc hardware_pwm hardware_irq hardware_watchdog hardware_clocks
                          hardware_flash hardware_sync)
    if(MICROCS_WS2812)
        target_link_libraries(microcs PUBLIC hardware_pio)   # the ws2812 driver's PIO backend
    endif()
    if(TARGET pico_flash)
        target_link_libraries(microcs PUBLIC pico_flash)   # flash_safe_execute (pico-sdk >= 1.5.1)
    endif()
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
