# ##############################################################################
# apps/interpreters/microcs/CMakeLists.txt  (ports/nuttx of the MicroCS repository)
#
# SPDX-License-Identifier: MIT
#
# ##############################################################################

if(CONFIG_INTERPRETERS_MICROCS)
  # ports/nuttx, linked to apps/interpreters/microcs: the sources are two levels up
  # (resolve the link first, then go up)
  get_filename_component(_microcs_here "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
  get_filename_component(MICROCS_DIR "${_microcs_here}/../.." ABSOLUTE)
  if(NOT EXISTS "${MICROCS_DIR}/src/mcs_vm.c")
    message(
      FATAL_ERROR
        "MicroCS: ${CMAKE_CURRENT_LIST_DIR} must be a link to ports/nuttx of the MicroCS repository: "
        "ln -s <MicroCS>/ports/nuttx <apps>/interpreters/microcs")
  endif()

  file(GLOB MICROCS_CORE_SRCS ${MICROCS_DIR}/src/*.c)
  file(GLOB MICROCS_MODULE_SRCS ${MICROCS_DIR}/modules/*/*.c)

  set(MICROCS_DEFS "")
  set(MICROCS_FLAGS -Wno-undef -Wno-unused-parameter -Wno-sign-compare
                    -Wno-shadow)
  if(CONFIG_INTERPRETERS_MICROCS_THREADS)
    list(APPEND MICROCS_DEFS MCS_OS=MCS_OS_NUTTX)
  endif()
  if(CONFIG_INTERPRETERS_MICROCS_PROFILE_EMBEDDED)
    list(APPEND MICROCS_DEFS MCS_DEFAULT_PROFILE=MCS_PROFILE_EMBEDDED)
  elseif(CONFIG_INTERPRETERS_MICROCS_PROFILE_MCU)
    list(APPEND MICROCS_DEFS MCS_DEFAULT_PROFILE=MCS_PROFILE_MCU)
  elseif(CONFIG_INTERPRETERS_MICROCS_PROFILE_LOWRAM)
    list(APPEND MICROCS_DEFS MCS_DEFAULT_PROFILE=MCS_PROFILE_LOWRAM)
  elseif(CONFIG_INTERPRETERS_MICROCS_PROFILE_MIN)
    list(APPEND MICROCS_DEFS MCS_DEFAULT_PROFILE=MCS_PROFILE_MIN)
  endif()
  if(CONFIG_INTERPRETERS_MICROCS_USER_CONFIG_FILE)
    list(APPEND MICROCS_DEFS
         MCS_USER_CONFIG_FILE="${CONFIG_INTERPRETERS_MICROCS_USER_CONFIG_FILE}")
  endif()

  # the first source is the one whose main() becomes <name>_main: the same
  # command line tool as the host's
  nuttx_add_application(
    NAME
    ${CONFIG_INTERPRETERS_MICROCS_PROGNAME}
    STACKSIZE
    ${CONFIG_INTERPRETERS_MICROCS_STACKSIZE}
    PRIORITY
    ${CONFIG_INTERPRETERS_MICROCS_PRIORITY}
    MODULE
    ${CONFIG_INTERPRETERS_MICROCS}
    INCLUDE_DIRECTORIES
    ${MICROCS_DIR}/include
    DEFINITIONS
    ${MICROCS_DEFS}
    COMPILE_FLAGS
    ${MICROCS_FLAGS}
    SRCS
    ${MICROCS_DIR}/ports/unix/main.c
    ${MICROCS_DIR}/ports/nuttx/mcs_port_nuttx.c
    ${MICROCS_CORE_SRCS}
    ${MICROCS_MODULE_SRCS})
endif()
