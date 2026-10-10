# MicroCS - Make fragment for any Makefile-based firmware (bare metal, vendor
# SDK makefiles, STM32CubeMX "Makefile" projects, NXP/TI/Microchip makefiles):
#
#     MICROCS_DIR  := path/to/MicroCS
#     MICROCS_PORT := stm32            # optional: stm32 rp2 esp32 zephyr arduino template
#     include $(MICROCS_DIR)/microcs.mk
#     C_SOURCES    += $(MICROCS_SRCS)  # CubeMX Makefile variable names
#     C_INCLUDES   += $(MICROCS_INCS)
#     C_DEFS       += $(MICROCS_DEFS)  # MICROCS_CONFIG / MICROCS_PROFILE / MICROCS_WS2812 / MICROCS_SERVO choices
#
# Configuration: every MCS_* option goes into a project header mcs_user_config.h (template:
# $(MICROCS_DIR)/config/mcs_user_config.h). It is used automatically when it is in one of the
# project's include folders (C_INCLUDES), or name it:
#     MICROCS_CONFIG  := app/mcs_user_config.h   # any name/path; NONE = no config header
#     MICROCS_PROFILE := lowram                  # full auto embedded mcu lowram tiny min linux
#                                                # (MCS_PROFILE in the header wins over it)
# MICROCS_CORE_ONLY := 1 leaves out the optional modules (fs, hal, drivers, sched, shell, runtime).
MICROCS_DIR ?= $(dir $(lastword $(MAKEFILE_LIST)))
MICROCS_SRCS := $(wildcard $(MICROCS_DIR)/src/*.c)
ifeq ($(MICROCS_CORE_ONLY),)
MICROCS_SRCS += $(wildcard $(MICROCS_DIR)/modules/*/*.c)
endif
MICROCS_INCS := -I$(MICROCS_DIR)/include
ifneq ($(MICROCS_PORT),)
MICROCS_SRCS += $(wildcard $(MICROCS_DIR)/ports/$(MICROCS_PORT)/mcs_port_$(MICROCS_PORT).c)
MICROCS_INCS += -I$(MICROCS_DIR)/ports/$(MICROCS_PORT)
endif
MICROCS_DEFS :=
ifeq ($(MICROCS_CONFIG),NONE)
MICROCS_DEFS += -DMCS_USER_CONFIG=0
else ifneq ($(MICROCS_CONFIG),)
MICROCS_DEFS += -DMCS_USER_CONFIG_FILE=\"$(abspath $(MICROCS_CONFIG))\"
endif
ifneq ($(MICROCS_PROFILE),)
_mcs_upper = $(subst z,Z,$(subst y,Y,$(subst x,X,$(subst w,W,$(subst v,V,$(subst u,U,$(subst t,T,$(subst s,S,$(subst r,R,$(subst q,Q,$(subst p,P,$(subst o,O,$(subst n,N,$(subst m,M,$(subst l,L,$(subst k,K,$(subst j,J,$(subst i,I,$(subst h,H,$(subst g,G,$(subst f,F,$(subst e,E,$(subst d,D,$(subst c,C,$(subst b,B,$(subst a,A,$(1)))))))))))))))))))))))))))
MICROCS_DEFS += -DMCS_DEFAULT_PROFILE=MCS_PROFILE_$(call _mcs_upper,$(strip $(MICROCS_PROFILE)))
endif
# MICROCS_WS2812 := 0 leaves out the built-in "ws2812" driver (C# LedStrip)
ifeq ($(MICROCS_WS2812),0)
MICROCS_DEFS += -DMCS_ENABLE_WS2812=0
endif
# MICROCS_SERVO := 0 leaves out the built-in "servo" driver (C# Servo)
ifeq ($(MICROCS_SERVO),0)
MICROCS_DEFS += -DMCS_ENABLE_SERVO=0
endif
