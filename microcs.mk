# MicroCS - Make fragment for any Makefile-based firmware (bare metal, vendor
# SDK makefiles, STM32CubeMX "Makefile" projects, NXP/TI/Microchip makefiles):
#
#     MICROCS_DIR  := path/to/MicroCS
#     MICROCS_PORT := stm32            # optional: stm32 rp2 esp32 zephyr arduino template
#     include $(MICROCS_DIR)/microcs.mk
#     C_SOURCES    += $(MICROCS_SRCS)  # CubeMX Makefile variable names
#     C_INCLUDES   += $(MICROCS_INCS)
#     C_DEFS       += $(MICROCS_DEFS)  # MICROCS_PROFILE / MICROCS_WS2812 choices
#
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
ifneq ($(MICROCS_PROFILE),)
MICROCS_DEFS := -DMCS_USER_CONFIG_FILE=\"profiles/mcs_profile_$(MICROCS_PROFILE).h\"
endif
# MICROCS_WS2812 := 0 leaves out the built-in "ws2812" driver (C# LedStrip)
ifeq ($(MICROCS_WS2812),0)
MICROCS_DEFS += -DMCS_ENABLE_WS2812=0
endif
