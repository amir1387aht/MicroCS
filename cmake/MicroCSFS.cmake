# MicroCS - flash filesystems for CMake builds (pico-sdk, STM32CubeMX CMake, ESP-IDF, Zephyr, plain CMake).
#
#   set(MICROCS_FS littlefs)      # or yaffs2, or "" (none: RAM disk only)
#   add_subdirectory(MicroCS)     # the root CMakeLists.txt includes this file
#
# Neither filesystem is bundled: the sources are downloaded once into the build
# tree (or taken from MICROCS_LITTLEFS_DIR / MICROCS_YAFFS2_DIR for offline
# builds) and compiled into the microcs library with MCS_ENABLE_LFS=1 or
# MCS_ENABLE_YAFFS=1, so mcs_flashfs_mount() (mcs_vfs.h) is available.
#
#   LittleFS  BSD-3-Clause, v2.9.3                         - the default for the port examples
#   YAFFS2    GPLv2 or commercial (Aleph One), pinned rev  - linking it puts the firmware under
#             those terms; better on big erase blocks (STM32F4/F7/H7 sectors)
set(MICROCS_LITTLEFS_VERSION 2.9.3)
set(MICROCS_YAFFS2_REV 474b3acb927d27b2305618aaf24456b9d33fe91b)

function(_microcs_fetch url dir marker)
    if(EXISTS ${dir}/${marker})
        return()
    endif()
    set(tgz ${CMAKE_BINARY_DIR}/_microcs_deps/dl.tar.gz)
    message(STATUS "MicroCS: downloading ${url}")
    file(DOWNLOAD ${url} ${tgz} STATUS st TLS_VERIFY ON)
    list(GET st 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "MicroCS: download failed (${st}); set MICROCS_LITTLEFS_DIR / MICROCS_YAFFS2_DIR to local sources")
    endif()
    get_filename_component(parent ${dir} DIRECTORY)
    file(MAKE_DIRECTORY ${parent})
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${tgz} WORKING_DIRECTORY ${parent} RESULT_VARIABLE rc)
    file(REMOVE ${tgz})
    if(NOT rc EQUAL 0 OR NOT EXISTS ${dir}/${marker})
        message(FATAL_ERROR "MicroCS: could not unpack ${url}")
    endif()
endfunction()

# littlefs: sets MICROCS_LFS_SRCS / MICROCS_LFS_INC in the caller
macro(microcs_littlefs_sources)
    if(MICROCS_LITTLEFS_DIR)
        set(_lfs ${MICROCS_LITTLEFS_DIR})
    else()
        set(_lfs ${CMAKE_BINARY_DIR}/_microcs_deps/littlefs-${MICROCS_LITTLEFS_VERSION})
        _microcs_fetch(https://github.com/littlefs-project/littlefs/archive/refs/tags/v${MICROCS_LITTLEFS_VERSION}.tar.gz ${_lfs} lfs.c)
    endif()
    set(MICROCS_LFS_SRCS ${_lfs}/lfs.c ${_lfs}/lfs_util.c)
    set(MICROCS_LFS_INC ${_lfs})
endmacro()

# yaffs2 "direct": the core files are copied next to the direct ones with the
# usual renames (the same as the Makefile's yaffs-test target); sets
# MICROCS_YAFFS_SRCS / MICROCS_YAFFS_INC / MICROCS_YAFFS_DEFS
macro(microcs_yaffs2_sources)
    if(MICROCS_YAFFS2_DIR)
        set(_y ${MICROCS_YAFFS2_DIR})
    else()
        set(_y ${CMAKE_BINARY_DIR}/_microcs_deps/yaffs2-${MICROCS_YAFFS2_REV})
        _microcs_fetch(https://github.com/Aleph-One-Ltd/yaffs2/archive/${MICROCS_YAFFS2_REV}.tar.gz ${_y} direct/yaffsfs.c)
    endif()
    set(_core yaffs_ecc yaffs_cache yaffs_guts yaffs_tagscompat yaffs_tagsmarshall yaffs_packedtags1
        yaffs_packedtags2 yaffs_nand yaffs_checkptrw yaffs_nameval yaffs_allocator yaffs_yaffs1 yaffs_yaffs2
        yaffs_bitmap yaffs_endian yaffs_verify yaffs_summary)
    set(_gen ${CMAKE_BINARY_DIR}/_microcs_deps/yaffs2-direct)
    set(MICROCS_YAFFS_SRCS ${_y}/direct/yaffsfs.c ${_y}/direct/yaffs_attribs.c ${_y}/direct/yaffs_error.c
        ${_y}/direct/yaffs_hweight.c)
    foreach(f ${_core} yaffs_getblockinfo yaffs_trace yaffs_attribs)
        foreach(e c h)
            if(EXISTS ${_y}/core/${f}.${e} AND NOT EXISTS ${_gen}/${f}.${e})
                file(READ ${_y}/core/${f}.${e} txt)
                if(NOT f MATCHES "^yaffs_(getblockinfo|trace|attribs)$")
                    foreach(fn strcat strcpy strncpy strnlen strcmp strncmp)
                        string(REPLACE "${fn}" "yaffs_${fn}" txt "${txt}")
                    endforeach()
                endif()
                string(REPLACE "loff_t" "Y_LOFF_T" txt "${txt}")
                file(WRITE ${_gen}/${f}.${e} "${txt}")
            endif()
        endforeach()
    endforeach()
    foreach(f ${_core})
        list(APPEND MICROCS_YAFFS_SRCS ${_gen}/${f}.c)
    endforeach()
    set(MICROCS_YAFFS_INC ${_gen} ${_y}/direct)
    set(MICROCS_YAFFS_DEFS CONFIG_YAFFS_DIRECT CONFIG_YAFFS_YAFFS2 CONFIG_YAFFS_DEFINES_TYPES
        CONFIG_YAFFS_PROVIDE_DEFS CONFIG_YAFFSFS_PROVIDE_VALUES Y_LOFF_T=off_t)
endmacro()

# add the selected filesystem to a target (the microcs library)
function(microcs_add_fs target fs)
    if(fs STREQUAL "littlefs")
        microcs_littlefs_sources()
        target_sources(${target} PRIVATE ${MICROCS_LFS_SRCS})
        target_include_directories(${target} PUBLIC ${MICROCS_LFS_INC})
        target_compile_definitions(${target} PUBLIC MCS_ENABLE_LFS=1 LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
        set_source_files_properties(${MICROCS_LFS_SRCS} PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
    elseif(fs STREQUAL "yaffs2")
        microcs_yaffs2_sources()
        target_sources(${target} PRIVATE ${MICROCS_YAFFS_SRCS})
        target_include_directories(${target} PUBLIC ${MICROCS_YAFFS_INC})
        target_compile_definitions(${target} PUBLIC MCS_ENABLE_YAFFS=1 MCS_YAFFS_OSGLUE=1 ${MICROCS_YAFFS_DEFS})
        # -w: third-party code; sys/types.h first: mode_t/off_t on newlib
        set_source_files_properties(${MICROCS_YAFFS_SRCS} PROPERTIES COMPILE_OPTIONS "-w;-include;sys/types.h")
    elseif(fs)
        message(FATAL_ERROR "MICROCS_FS must be littlefs, yaffs2 or empty (got '${fs}')")
    endif()
endfunction()
