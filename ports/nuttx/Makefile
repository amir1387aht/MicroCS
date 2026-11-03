############################################################################
# apps/interpreters/microcs/Makefile  (ports/nuttx of the MicroCS repository)
#
# SPDX-License-Identifier: MIT
#
############################################################################

include $(APPDIR)/Make.defs

# This folder is ports/nuttx of the MicroCS repository, linked or copied-with-the-repo to
# apps/interpreters/microcs; the sources are two levels up. $(realpath) follows the link.
MICROCS_DIR := $(realpath $(CURDIR)/../..)
ifeq ($(wildcard $(MICROCS_DIR)/src/mcs_vm.c),)
$(error MicroCS: $(CURDIR) must be a link to ports/nuttx of the MicroCS repository: ln -s <MicroCS>/ports/nuttx $(APPDIR)/interpreters/microcs)
endif

# language core + modules (fs, hal, sched, shell, runtime, threads, os...); a module that
# cannot be built for the configuration compiles to an empty unit
CSRCS += $(wildcard $(MICROCS_DIR)/src/*.c)
CSRCS += $(wildcard $(MICROCS_DIR)/modules/*/*.c)

CFLAGS += ${INCDIR_PREFIX}$(MICROCS_DIR)/include
CFLAGS += -Wno-undef -Wno-unused-parameter -Wno-sign-compare -Wno-shadow

ifneq ($(CONFIG_INTERPRETERS_MICROCS_THREADS),)
CFLAGS += -DMCS_OS=MCS_OS_NUTTX
endif
ifneq ($(CONFIG_INTERPRETERS_MICROCS_PROFILE_EMBEDDED),)
CFLAGS += -DMCS_DEFAULT_PROFILE=MCS_PROFILE_EMBEDDED
else ifneq ($(CONFIG_INTERPRETERS_MICROCS_PROFILE_MCU),)
CFLAGS += -DMCS_DEFAULT_PROFILE=MCS_PROFILE_MCU
else ifneq ($(CONFIG_INTERPRETERS_MICROCS_PROFILE_LOWRAM),)
CFLAGS += -DMCS_DEFAULT_PROFILE=MCS_PROFILE_LOWRAM
else ifneq ($(CONFIG_INTERPRETERS_MICROCS_PROFILE_MIN),)
CFLAGS += -DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN
endif
ifneq ($(CONFIG_INTERPRETERS_MICROCS_USER_CONFIG_FILE),)
CFLAGS += -DMCS_USER_CONFIG_FILE=\"$(CONFIG_INTERPRETERS_MICROCS_USER_CONFIG_FILE)\"
endif

# the `mcs` command: the same command line tool as the host's (ports/unix/main.c)
MAINSRC = $(MICROCS_DIR)/ports/unix/main.c

PROGNAME  = $(CONFIG_INTERPRETERS_MICROCS_PROGNAME)
PRIORITY  = $(CONFIG_INTERPRETERS_MICROCS_PRIORITY)
STACKSIZE = $(CONFIG_INTERPRETERS_MICROCS_STACKSIZE)
MODULE    = $(CONFIG_INTERPRETERS_MICROCS)

include $(APPDIR)/Application.mk
