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
#   MICROCS_OS        none (default: single-threaded) | freertos | posix | auto: real C# threads and
#                     second-core jobs on the OS's tasks (docs/THREADS.md, cmake/MicroCSOS.cmake).
#                     FreeRTOS: a freertos_kernel / FreeRTOS-Kernel target, or MICROCS_FREERTOS_PATH
#                     + MICROCS_FREERTOS_PORT + MICROCS_FREERTOS_CONFIG_DIR
#   MICROCS_WIFI      ON (RP2 only): Wi-Fi + lwIP sockets on a Pico W / Pico 2 W (PICO_BOARD=pico_w / pico2_w,
#                     MICROCS_OS=freertos; ports/rp2/mcs_port_rp2_net.c, lwipopts.h from MICROCS_LWIPOPTS_DIR)
#   MICROCS_BLE       ON: Bluetooth LE (C# Ble, BleService, BleConnection, BleUart; docs/BLE.md; default OFF,
#                     never in the release builds). RP2: BTstack on a Pico W / Pico 2 W (PICO_BOARD=pico_w /
#                     pico2_w; btstack_config.h from MICROCS_BTSTACK_CONFIG_DIR); host: the simulated radio
#                     (mcs --sim). ESP-IDF: CONFIG_MICROCS_BLE; Zephyr: CONFIG_MICROCS_BLE
#   MICROCS_U8G2      ON: C# U8g2 / U8x8 on olikraus' u8g2 library (docs/U8G2.md; default OFF, or ON when
#                     mcs_user_config.h has #define MCS_ENABLE_U8G2 1). Sources: MICROCS_U8G2_DIR, a copy
#                     found next to the project or -DMICROCS_U8G2_DOWNLOAD=ON; compiled in only the
#                     MICROCS_U8G2_DISPLAYS / _FONTS / MICROCS_U8X8_FONTS lists (cmake/MicroCSU8g2.cmake)
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
    set(MICROCS_CONFIG_FILE "${_cfg}" PARENT_SCOPE)
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
    set(_mcs_u8g2_srcs "")
    set(_mcs_u8g2_incs "")
    if(CONFIG_MICROCS_U8G2)             # menuconfig > MicroCS > U8g2 displays
        include(${MICROCS_DIR}/cmake/MicroCSU8g2.cmake)
        microcs_u8g2_from_kconfig()
        microcs_u8g2_sources()
        set(_mcs_u8g2_srcs ${MICROCS_U8G2_SRCS})
        set(_mcs_u8g2_incs ${MICROCS_U8G2_INC})
    endif()
    # menuconfig > MicroCS > Wi-Fi: C# Wifi, EspNow, TcpClient, Http (HTTPS), Ntp, Ping, Mdns
    # (requirements cannot depend on Kconfig: the components are always listed, the sources not)
    set(_mcs_net_srcs "")
    if(CONFIG_MICROCS_WIFI)
        set(_mcs_net_srcs ports/esp32/mcs_port_esp32_net.c)
    endif()
    set(_mcs_net_reqs esp_wifi esp_netif esp_event nvs_flash lwip esp-tls mbedtls wpa_supplicant)
    # menuconfig > MicroCS > Bluetooth LE (NimBLE host): C# Ble, BleService, BleConnection, BleUart
    if(CONFIG_MICROCS_BLE)
        list(APPEND _mcs_net_srcs ports/esp32/mcs_port_esp32_ble.c)
    endif()
    list(APPEND _mcs_net_reqs bt)
    # mDNS (C# Mdns) when the project has the espressif/mdns component:
    # idf.py add-dependency espressif/mdns   (or a copy in <project>/components/mdns)
    idf_build_get_property(_mcs_proj PROJECT_DIR)
    set(_mcs_mdns "")
    if(EXISTS "${_mcs_proj}/managed_components/espressif__mdns")
        set(_mcs_mdns espressif__mdns)
    elseif(EXISTS "${_mcs_proj}/components/mdns")
        set(_mcs_mdns mdns)
    endif()
    list(APPEND _mcs_net_reqs ${_mcs_mdns})
    idf_component_register(
        SRCS ${MICROCS_CORE_SRCS} ${MICROCS_MODULE_SRCS} ports/esp32/mcs_port_esp32.c ${_mcs_net_srcs} ${_mcs_fs_srcs} ${_mcs_u8g2_srcs}
        INCLUDE_DIRS include ports/esp32 ${_mcs_fs_incs} ${_mcs_u8g2_incs}
        REQUIRES driver esp_adc esp_timer esp_partition joltwallet__littlefs ${_mcs_net_reqs}
        PRIV_REQUIRES esp_system)
    if(CONFIG_MICROCS_BLE)
        if(NOT CONFIG_SOC_BLE_SUPPORTED OR NOT CONFIG_BT_ENABLED OR NOT CONFIG_BT_NIMBLE_ENABLED OR NOT CONFIG_BT_CONTROLLER_ENABLED OR CONFIG_BT_NIMBLE_EXT_ADV)
            message(FATAL_ERROR "MicroCS BLE needs an onboard BLE radio, Bluetooth enabled, NimBLE host, controller enabled and extended advertising disabled (idf.py menuconfig > Component config > Bluetooth)")
        endif()
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_BLE=1)
        # Keep the front-end connection table aligned with NimBLE (C2 permits only two).
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_BLE_MAX_CONN=${CONFIG_BT_NIMBLE_MAX_CONNECTIONS})
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_BLE_NAME=\"${CONFIG_MICROCS_BLE_NAME}\")
        foreach(_opt QUEUE MAX_SERVICES MAX_ATTRS VALUE_POOL MAX_VALUE SCAN_MAX MAX_DISC MAX_SUBS TIMEOUT CONNECT_TIMEOUT UART_RX)
            target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_BLE_${_opt}=${CONFIG_MICROCS_BLE_${_opt}})
        endforeach()
    endif()
    if(NOT CONFIG_MICROCS_WIFI)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ENABLE_NET=0)
    elseif(_mcs_mdns)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_ESP32_MDNS=1)
    endif()
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
    if(CONFIG_MICROCS_THREADS)          # menuconfig > MicroCS > Real threads (FreeRTOS is part of ESP-IDF)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC MCS_OS=MCS_OS_FREERTOS)
    endif()
    if(CONFIG_MICROCS_U8G2)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC ${MICROCS_U8G2_DEFS})
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
if(NOT DEFINED MICROCS_WIFI)
    option(MICROCS_WIFI "RP2: Wi-Fi + sockets on a Pico W / Pico 2 W (needs MICROCS_OS=freertos)" OFF)
endif()
if(NOT DEFINED MICROCS_BLE)
    option(MICROCS_BLE "Bluetooth LE: RP2 Pico W / Pico 2 W (BTstack), host (simulated radio)" OFF)
endif()
if(NOT DEFINED MICROCS_OS)
    set(MICROCS_OS "none" CACHE STRING "OS for real C# threads: none (default, single-threaded), freertos, posix, auto")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MicroCS VERSION 1.14.0 LANGUAGES C)
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
if(MICROCS_BLE AND NOT MICROCS_PORT AND NOT CMAKE_CROSSCOMPILING)
    target_compile_definitions(microcs PUBLIC MCS_ENABLE_BLE=1)   # host: the simulated radio (mcs --sim)
endif()
if(MICROCS_OS AND NOT MICROCS_OS STREQUAL "none")
    include(${MICROCS_DIR}/cmake/MicroCSOS.cmake)
    microcs_add_os(microcs "${MICROCS_OS}")
endif()
include(${MICROCS_DIR}/cmake/MicroCSU8g2.cmake)
microcs_u8g2_wanted(_mcs_u8g2 "${MICROCS_CONFIG_FILE}")
if(_mcs_u8g2 AND MICROCS_MODULES)
    microcs_add_u8g2(microcs)
elseif(DEFINED MICROCS_U8G2 AND NOT MICROCS_U8G2)
    target_compile_definitions(microcs PUBLIC MCS_ENABLE_U8G2=0)
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
string(TOLOWER "${MICROCS_OS}" _mcs_os_lc)
if(MICROCS_PORT STREQUAL "rp2")
    target_link_libraries(microcs PUBLIC pico_stdlib pico_unique_id hardware_i2c hardware_spi
                          hardware_adc hardware_pwm hardware_irq hardware_watchdog hardware_clocks
                          hardware_flash hardware_sync hardware_dma)
    if(MICROCS_WS2812)
        target_link_libraries(microcs PUBLIC hardware_pio)   # the ws2812 driver's PIO backend
    endif()
    if(TARGET pico_flash)
        target_link_libraries(microcs PUBLIC pico_flash)   # flash_safe_execute (pico-sdk >= 1.5.1)
    endif()
    if(MICROCS_WIFI)                                 # Pico W / Pico 2 W: Wi-Fi + lwIP sockets (docs/NETWORK.md)
        if(NOT TARGET pico_cyw43_arch_lwip_sys_freertos OR NOT _mcs_os_lc STREQUAL "freertos")
            message(FATAL_ERROR "MICROCS_WIFI=ON needs a board with the CYW43 radio (-DPICO_BOARD=pico_w or pico2_w) "
                "and -DMICROCS_OS=freertos (lwIP sockets run in FreeRTOS threads)")
        endif()
        target_sources(microcs PRIVATE ${MICROCS_DIR}/ports/rp2/mcs_port_rp2_net.c)
        if(NOT MICROCS_LWIPOPTS_DIR)
            set(MICROCS_LWIPOPTS_DIR ${MICROCS_DIR}/ports/rp2/wifi)   # lwipopts.h (your own: -DMICROCS_LWIPOPTS_DIR=...)
        endif()
        target_include_directories(microcs PUBLIC ${MICROCS_LWIPOPTS_DIR})
        target_link_libraries(microcs PUBLIC pico_cyw43_arch_lwip_sys_freertos pico_lwip_sntp pico_lwip_mdns)
        target_compile_definitions(microcs PUBLIC MCS_RP2_WIFI=1 MCS_ENABLE_NET=1 MCS_NET_LWIP=1 MCS_NET_SIM=0)
    endif()
    if(MICROCS_BLE)                                  # Pico W / Pico 2 W: Bluetooth LE on BTstack (docs/BLE.md)
        if(NOT TARGET pico_btstack_cyw43 OR NOT TARGET pico_btstack_ble)
            message(FATAL_ERROR "MICROCS_BLE=ON needs a board with the CYW43 radio (-DPICO_BOARD=pico_w or pico2_w) "
                "and pico-sdk's lib/btstack + lib/cyw43-driver submodules")
        endif()
        target_sources(microcs PRIVATE ${MICROCS_DIR}/ports/rp2/mcs_port_rp2_ble.c)
        if(NOT MICROCS_BTSTACK_CONFIG_DIR)
            set(MICROCS_BTSTACK_CONFIG_DIR ${MICROCS_DIR}/ports/rp2/ble)   # btstack_config.h
        endif()
        target_include_directories(microcs PUBLIC ${MICROCS_BTSTACK_CONFIG_DIR})
        target_link_libraries(microcs PUBLIC pico_btstack_ble pico_btstack_cyw43)
        if(NOT MICROCS_WIFI)                         # the cyw43 driver without lwIP
            if(_mcs_os_lc STREQUAL "freertos" AND TARGET pico_cyw43_arch_sys_freertos)
                target_link_libraries(microcs PUBLIC pico_cyw43_arch_sys_freertos)
                target_compile_definitions(microcs PUBLIC CYW43_LWIP=0)
            elseif(_mcs_os_lc STREQUAL "freertos")
                message(FATAL_ERROR "MICROCS_BLE=ON with MICROCS_OS=freertos needs pico-sdk's lib/lwip submodule "
                    "(pico_cyw43_arch_sys_freertos is defined next to the lwIP targets)")
            else()
                target_link_libraries(microcs PUBLIC pico_cyw43_arch_none)   # threadsafe background, no lwIP
            endif()
        endif()
        target_compile_definitions(microcs PUBLIC MCS_RP2_BLE=1 MCS_ENABLE_BLE=1)
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
string(TOLOWER "${MICROCS_OS}" _mcs_os_lc)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_CROSSCOMPILING AND NOT MICROCS_PORT
   AND NOT _mcs_os_lc STREQUAL "freertos")      # a FreeRTOS host build is a library for your own app
    add_executable(mcs ${MICROCS_DIR}/ports/unix/main.c)
    target_link_libraries(mcs PRIVATE microcs)
    if(_mcs_u8g2 AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
        target_link_options(mcs PRIVATE -Wl,--gc-sections)   # only the u8g2 fonts / displays in use
    elseif(_mcs_u8g2 AND APPLE)
        target_link_options(mcs PRIVATE -Wl,-dead_strip)
    endif()
endif()
