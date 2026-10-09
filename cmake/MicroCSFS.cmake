# MicroCS - flash filesystems for CMake builds (pico-sdk, STM32CubeMX CMake, ESP-IDF, Zephyr, plain CMake).
#
#   set(MICROCS_FS littlefs)      # or yaffs2, or "" (none: RAM disk only)
#   add_subdirectory(MicroCS)     # the root CMakeLists.txt includes this file
#
# Neither filesystem is bundled. The sources are looked up, in this order:
#   1. MICROCS_LITTLEFS_DIR / MICROCS_YAFFS2_DIR (CMake variable or environment variable)
#      - the folder with lfs.c + lfs.h, or the yaffs2 checkout with direct/ and core/
#   2. a copy next to your project or MicroCS: <project>/{littlefs,lib/littlefs,libs/littlefs,
#      third_party/littlefs,external/littlefs,components/littlefs,Middlewares/Third_Party/littlefs},
#      <project>/.. and <MicroCS>/.. siblings, MicroCS/third_party/, the west workspace
#      (Zephyr's modules/fs/littlefs), MicroCS/build/third_party (make fetch-lfs / fetch-yaffs)
#      - the same names with yaffs2 for YAFFS2
#   3. an earlier download in <build>/_microcs_deps
#   4. only with -DMICROCS_FS_DOWNLOAD=ON (or the environment variable MICROCS_FS_DOWNLOAD=1):
#      download the pinned release once into <build>/_microcs_deps
# Otherwise configuring stops with a message that says what to set - nothing is fetched
# from the network behind your back. The filesystem is compiled into the microcs library
# with MCS_ENABLE_LFS=1 or MCS_ENABLE_YAFFS=1, so mcs_flashfs_mount() (mcs_vfs.h) is available.
#
#   LittleFS  BSD-3-Clause, v2.x (download: v2.9.3)            - the default for the port examples
#   YAFFS2    GPLv2 or commercial (Aleph One), pinned rev      - linking it puts the firmware under
#             those terms; better on big erase blocks (STM32F4/F7/H7 sectors)
set(MICROCS_LITTLEFS_VERSION 2.9.3)
set(MICROCS_YAFFS2_REV 474b3acb927d27b2305618aaf24456b9d33fe91b)
if(NOT DEFINED MICROCS_FS_DOWNLOAD)
    if("$ENV{MICROCS_FS_DOWNLOAD}" MATCHES "^(1|ON|on|YES|yes|TRUE|true)$")
        set(MICROCS_FS_DOWNLOAD ON)
    else()
        set(MICROCS_FS_DOWNLOAD OFF)
    endif()
endif()
set(MICROCS_FS_DOWNLOAD ${MICROCS_FS_DOWNLOAD} CACHE BOOL "Download littlefs / yaffs2 when no local copy is found")
get_filename_component(_microcs_root ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(MICROCS_FS_ROOT ${_microcs_root} CACHE INTERNAL "MicroCS checkout (filesystem source lookup)")

function(_microcs_fetch url dir marker)
    if(EXISTS ${dir}/${marker})
        return()
    endif()
    set(tgz ${CMAKE_BINARY_DIR}/_microcs_deps/dl.tar.gz)
    message(STATUS "MicroCS: downloading ${url} (MICROCS_FS_DOWNLOAD=ON)")
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

# Find a source tree: out = the first of `var` (CMake or environment variable), the
# candidate folders below and `cached` that holds every file in `markers`; "" if none.
function(_microcs_find_src out var name cached markers)
    set(user "${${var}}")
    if(NOT user AND DEFINED ENV{${var}})
        set(user "$ENV{${var}}")
    endif()
    if(user)
        get_filename_component(user "${user}" ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
        foreach(m ${markers})
            if(NOT EXISTS "${user}/${m}")
                message(FATAL_ERROR "MicroCS: ${var}=${user} has no ${m} - point it at the ${name} sources")
            endif()
        endforeach()
        set(${out} "${user}" PARENT_SCOPE)
        return()
    endif()
    set(cands)
    foreach(base ${CMAKE_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
        foreach(sub ${name} lib/${name} libs/${name} third_party/${name} thirdparty/${name} external/${name}
                    extern/${name} vendor/${name} deps/${name} components/${name} Middlewares/Third_Party/${name}
                    ../${name})
            list(APPEND cands ${base}/${sub})
        endforeach()
    endforeach()
    list(APPEND cands ${MICROCS_FS_ROOT}/third_party/${name} ${MICROCS_FS_ROOT}/../${name}
        ${MICROCS_FS_ROOT}/build/third_party/${name}-${MICROCS_LITTLEFS_VERSION}       # make fetch-lfs
        ${MICROCS_FS_ROOT}/build/third_party/${name}-${MICROCS_YAFFS2_REV})              # make fetch-yaffs
    if(DEFINED ZEPHYR_BASE)                      # west workspace: <ws>/modules/fs/<name>
        list(APPEND cands ${ZEPHYR_BASE}/../modules/fs/${name} ${ZEPHYR_BASE}/../modules/lib/${name})
    endif()
    list(APPEND cands ${cached})
    foreach(c ${cands})
        set(ok TRUE)
        foreach(m ${markers})
            if(NOT EXISTS "${c}/${m}")
                set(ok FALSE)
                break()
            endif()
        endforeach()
        if(ok)
            get_filename_component(c "${c}" ABSOLUTE)
            set(${out} "${c}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} "" PARENT_SCOPE)
endfunction()

function(_microcs_not_found name var opt)
    message(FATAL_ERROR "MicroCS: MICROCS_FS needs the ${name} sources, which are not bundled and were not found.\n"
        "  Either point MicroCS at a copy:      -D${var}=/path/to/${opt}\n"
        "  or put it next to your project:      <project>/third_party/${opt} (or lib/, external/, ...)\n"
        "  or let CMake download it once:       -DMICROCS_FS_DOWNLOAD=ON   (environment: MICROCS_FS_DOWNLOAD=1)\n"
        "  or build without a flash filesystem: -DMICROCS_FS=")
endfunction()

# littlefs: sets MICROCS_LFS_SRCS / MICROCS_LFS_INC in the caller
macro(microcs_littlefs_sources)
    set(_lfs_dl ${CMAKE_BINARY_DIR}/_microcs_deps/littlefs-${MICROCS_LITTLEFS_VERSION})
    _microcs_find_src(_lfs MICROCS_LITTLEFS_DIR littlefs ${_lfs_dl} "lfs.c;lfs.h;lfs_util.c;lfs_util.h")
    if(NOT _lfs AND MICROCS_FS_DOWNLOAD)
        _microcs_fetch(https://github.com/littlefs-project/littlefs/archive/refs/tags/v${MICROCS_LITTLEFS_VERSION}.tar.gz ${_lfs_dl} lfs.c)
        set(_lfs ${_lfs_dl})
    endif()
    if(NOT _lfs)
        _microcs_not_found(LittleFS MICROCS_LITTLEFS_DIR littlefs)
    endif()
    file(STRINGS ${_lfs}/lfs.h _lfs_ver REGEX "^#define LFS_VERSION 0x[0-9a-fA-F]+")
    if(_lfs_ver MATCHES "0x0002[0-9a-fA-F]*")
        message(STATUS "MicroCS: LittleFS from ${_lfs}")
    else()
        message(FATAL_ERROR "MicroCS: ${_lfs}/lfs.h is not LittleFS v2.x (${_lfs_ver})")
    endif()
    set(MICROCS_LFS_SRCS ${_lfs}/lfs.c ${_lfs}/lfs_util.c)
    set(MICROCS_LFS_INC ${_lfs})
endmacro()

# yaffs2 "direct": the core files are copied next to the direct ones with the
# usual renames (the same as the Makefile's yaffs-test target); sets
# MICROCS_YAFFS_SRCS / MICROCS_YAFFS_INC / MICROCS_YAFFS_DEFS
macro(microcs_yaffs2_sources)
    set(_y_dl ${CMAKE_BINARY_DIR}/_microcs_deps/yaffs2-${MICROCS_YAFFS2_REV})
    _microcs_find_src(_y MICROCS_YAFFS2_DIR yaffs2 ${_y_dl} "direct/yaffsfs.c;direct/yaffsfs.h;core/yaffs_guts.c;core/yaffs_guts.h")
    if(NOT _y AND MICROCS_FS_DOWNLOAD)
        _microcs_fetch(https://github.com/Aleph-One-Ltd/yaffs2/archive/${MICROCS_YAFFS2_REV}.tar.gz ${_y_dl} direct/yaffsfs.c)
        set(_y ${_y_dl})
    endif()
    if(NOT _y)
        _microcs_not_found(YAFFS2 MICROCS_YAFFS2_DIR yaffs2)
    endif()
    message(STATUS "MicroCS: YAFFS2 from ${_y}")
    set(_core yaffs_ecc yaffs_cache yaffs_guts yaffs_tagscompat yaffs_tagsmarshall yaffs_packedtags1
        yaffs_packedtags2 yaffs_nand yaffs_checkptrw yaffs_nameval yaffs_allocator yaffs_yaffs1 yaffs_yaffs2
        yaffs_bitmap yaffs_endian yaffs_verify yaffs_summary)
    string(MD5 _yh "${_y}")
    string(SUBSTRING ${_yh} 0 8 _yh)
    set(_gen ${CMAKE_BINARY_DIR}/_microcs_deps/yaffs2-direct-${_yh})   # renamed copies, one per source tree
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
