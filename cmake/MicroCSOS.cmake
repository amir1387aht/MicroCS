# MicroCS - optional OS for real C# threads (docs/THREADS.md).
#
#   MICROCS_OS = none (default: single-threaded, exactly like builds without this option)
#              | freertos | posix | auto
#   (Zephyr: CONFIG_MICROCS_THREADS=y through the Zephyr module, ESP-IDF: menuconfig > MicroCS.)
#
# FreeRTOS headers are taken from, in this order:
#   1. a FreeRTOS CMake target that already exists: freertos_kernel (FreeRTOS-Kernel's own
#      CMake), FreeRTOS-Kernel (pico-sdk FreeRTOS_Kernel_import.cmake), freertos, or the
#      stm32cubemx target when CubeMX generated FreeRTOS into Middlewares/
#   2. MICROCS_FREERTOS_PATH (the FreeRTOS-Kernel folder, also the FREERTOS_KERNEL_PATH /
#      FREERTOS_PATH environment variables) + MICROCS_FREERTOS_PORT (port folder relative to it
#      or absolute, e.g. portable/GCC/ARM_CM4F) + MICROCS_FREERTOS_CONFIG_DIR (folder of
#      FreeRTOSConfig.h; default: searched in the project)
#   3. common places in the project: FreeRTOS-Kernel/, third_party/FreeRTOS-Kernel/, lib/...,
#      Middlewares/Third_Party/FreeRTOS/Source/ (CubeMX)
# and the configure step stops with a message saying which variable to set when none fits.

function(_microcs_find_freertos_config out)
    set(_c "")
    if(MICROCS_FREERTOS_CONFIG_DIR)
        get_filename_component(_d "${MICROCS_FREERTOS_CONFIG_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT EXISTS "${_d}/FreeRTOSConfig.h")
            message(FATAL_ERROR "MicroCS: MICROCS_FREERTOS_CONFIG_DIR=${MICROCS_FREERTOS_CONFIG_DIR} has no FreeRTOSConfig.h")
        endif()
        set(_c "${_d}")
    else()
        foreach(d "" include inc config freertos src main Core/Inc Inc app/include)
            if(NOT _c AND EXISTS "${CMAKE_SOURCE_DIR}/${d}/FreeRTOSConfig.h")
                set(_c "${CMAKE_SOURCE_DIR}/${d}")
            endif()
        endforeach()
    endif()
    set(${out} "${_c}" PARENT_SCOPE)
endfunction()

function(microcs_use_freertos tgt)
    _microcs_find_freertos_config(_cfg)
    # 1. existing targets
    foreach(t freertos_kernel FreeRTOS-Kernel freertos FreeRTOS)
        if(TARGET ${t})
            message(STATUS "MicroCS: threads on FreeRTOS (target ${t})")
            target_link_libraries(${tgt} PUBLIC ${t})
            if(_cfg)
                target_include_directories(${tgt} PUBLIC "${_cfg}")
            endif()
            target_compile_definitions(${tgt} PUBLIC MCS_OS=MCS_OS_FREERTOS)
            return()
        endif()
    endforeach()
    if(TARGET stm32cubemx)
        get_target_property(_incs stm32cubemx INTERFACE_INCLUDE_DIRECTORIES)
        foreach(i IN LISTS _incs)
            if(EXISTS "${i}/FreeRTOS.h")
                message(STATUS "MicroCS: threads on FreeRTOS (from the CubeMX project: ${i})")
                target_compile_definitions(${tgt} PUBLIC MCS_OS=MCS_OS_FREERTOS)
                return()
            endif()
        endforeach()
    endif()
    # 2./3. a FreeRTOS-Kernel folder
    set(_root "${MICROCS_FREERTOS_PATH}")
    if(NOT _root AND DEFINED ENV{FREERTOS_KERNEL_PATH})
        set(_root "$ENV{FREERTOS_KERNEL_PATH}")
    endif()
    if(NOT _root AND DEFINED ENV{FREERTOS_PATH})
        set(_root "$ENV{FREERTOS_PATH}")
    endif()
    if(_root)
        get_filename_component(_root "${_root}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT EXISTS "${_root}/include/FreeRTOS.h")
            message(FATAL_ERROR "MicroCS: ${_root}/include/FreeRTOS.h not found. MICROCS_FREERTOS_PATH (or "
                "FREERTOS_KERNEL_PATH) must be the FreeRTOS-Kernel folder - the one with include/ and portable/.")
        endif()
    else()
        foreach(d FreeRTOS-Kernel third_party/FreeRTOS-Kernel lib/FreeRTOS-Kernel libs/FreeRTOS-Kernel
                  external/FreeRTOS-Kernel Middlewares/Third_Party/FreeRTOS/Source FreeRTOS/Source)
            if(NOT _root AND EXISTS "${CMAKE_SOURCE_DIR}/${d}/include/FreeRTOS.h")
                set(_root "${CMAKE_SOURCE_DIR}/${d}")
            endif()
        endforeach()
    endif()
    if(NOT _root)
        message(FATAL_ERROR "MicroCS: MICROCS_OS=freertos but no FreeRTOS was found (no freertos_kernel / FreeRTOS-Kernel "
            "target, no FreeRTOS-Kernel folder in the project). Tell the build where it is:\n"
            "  -DMICROCS_FREERTOS_PATH=<FreeRTOS-Kernel folder>  (or the FREERTOS_KERNEL_PATH environment variable)\n"
            "  -DMICROCS_FREERTOS_PORT=<port folder, e.g. portable/GCC/ARM_CM4F>\n"
            "  -DMICROCS_FREERTOS_CONFIG_DIR=<folder of your FreeRTOSConfig.h>\n"
            "or build without an OS: -DMICROCS_OS=none (the default). See docs/THREADS.md")
    endif()
    set(_port "")
    if(MICROCS_FREERTOS_PORT)
        if(IS_ABSOLUTE "${MICROCS_FREERTOS_PORT}")
            set(_port "${MICROCS_FREERTOS_PORT}")
        else()
            set(_port "${_root}/${MICROCS_FREERTOS_PORT}")
        endif()
    elseif(NOT CMAKE_CROSSCOMPILING AND CMAKE_HOST_UNIX AND EXISTS "${_root}/portable/ThirdParty/GCC/Posix/portmacro.h")
        set(_port "${_root}/portable/ThirdParty/GCC/Posix")          # host: the Linux simulator port
    endif()
    if(NOT _port OR NOT EXISTS "${_port}/portmacro.h")
        message(FATAL_ERROR "MicroCS: FreeRTOS found at ${_root}, but not its port for this CPU (portmacro.h). "
            "Set -DMICROCS_FREERTOS_PORT=<folder with portmacro.h, relative to ${_root} or absolute>, "
            "e.g. portable/GCC/ARM_CM4F, portable/GCC/ARM_CM0, portable/GCC/ARM_CM33_NTZ/non_secure, "
            "portable/ThirdParty/GCC/RP2040. See docs/THREADS.md")
    endif()
    if(NOT _cfg)
        message(FATAL_ERROR "MicroCS: FreeRTOS found at ${_root}, but no FreeRTOSConfig.h in the project. "
            "Set -DMICROCS_FREERTOS_CONFIG_DIR=<folder of your FreeRTOSConfig.h>. See docs/THREADS.md")
    endif()
    message(STATUS "MicroCS: threads on FreeRTOS (${_root}, port ${_port}, config ${_cfg})")
    target_include_directories(${tgt} PUBLIC "${_root}/include" "${_port}" "${_cfg}")
    target_compile_definitions(${tgt} PUBLIC MCS_OS=MCS_OS_FREERTOS)
endfunction()

# MICROCS_OS value -> definitions and libraries on `tgt`
function(microcs_add_os tgt os)
    string(TOLOWER "${os}" _os)
    if(_os STREQUAL "" OR _os STREQUAL "none" OR _os STREQUAL "off")
        return()                                    # single-threaded: nothing changes
    elseif(_os STREQUAL "posix")
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(${tgt} PUBLIC Threads::Threads)
        target_compile_definitions(${tgt} PUBLIC MCS_OS=MCS_OS_POSIX)
        message(STATUS "MicroCS: threads on POSIX threads")
    elseif(_os STREQUAL "freertos")
        microcs_use_freertos(${tgt})
    elseif(_os STREQUAL "auto")
        # the compiler decides (include/mcs_config.h): Zephyr, ESP-IDF or FreeRTOS headers on the path
        foreach(t freertos_kernel FreeRTOS-Kernel)
            if(TARGET ${t})
                target_link_libraries(${tgt} PUBLIC ${t})
                break()
            endif()
        endforeach()
        target_compile_definitions(${tgt} PUBLIC MCS_OS=MCS_OS_AUTO)
    elseif(_os STREQUAL "zephyr")
        message(FATAL_ERROR "MicroCS: Zephyr threads come with the Zephyr module: CONFIG_MICROCS_THREADS=y in prj.conf "
            "(see ports/zephyr/README.md), not MICROCS_OS")
    else()
        message(FATAL_ERROR "MicroCS: unknown MICROCS_OS '${os}' (none, freertos, posix, auto)")
    endif()
endfunction()
