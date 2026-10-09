/*
 * MicroCS - umbrella header: everything an application normally needs.
 * Arduino / PlatformIO:  #include <MicroCS.h>
 */
#ifndef MICROCS_H
#define MICROCS_H
#include "mcs.h"
#include "mcs_bind.h"
#include "mcs_hal.h"
#include "mcs_driver.h"
#include "mcs_vfs.h"
#include "mcs_sched.h"
#include "mcs_shell.h"
#include "mcs_runtime.h"
#if defined(ARDUINO) && defined(__cplusplus)
#  if defined(__has_include)
#    if __has_include("mcs_port_arduino.h")
#      include "mcs_port_arduino.h"           /* flattened Arduino library */
#    else
#      include "../ports/arduino/mcs_port_arduino.h"
#    endif
#  else
#    include "../ports/arduino/mcs_port_arduino.h"
#  endif
#endif
#endif
