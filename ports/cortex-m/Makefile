# MicroCS Cortex-M reference port (bare metal, runs on real silicon or tools/cm_emu.py)
#   make -C ports/cortex-m            build all targets into build/cm/
#   make -C ports/cortex-m run        build + run each target in the Unicorn emulator
# Requires arm-none-eabi-gcc on PATH (tested: xPack GCC 13.2.1).
ROOT    := ../..
OUT     := $(ROOT)/build/cm
CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
MCS     := $(ROOT)/mcs
SRC     := $(wildcard $(ROOT)/src/*.c) $(wildcard $(ROOT)/modules/*/*.c) startup.c board.c main.c
# modules that only make sense on a host
SRC     := $(filter-out %mcs_vfs_posix.c,$(SRC))
CFLAGS  := -std=gnu99 -Os -g -Wall -Wextra -ffunction-sections -fdata-sections -I$(ROOT)/include -I$(OUT) -I.
LDFLAGS := -Wl,--gc-sections -T cm.ld --specs=nano.specs --specs=nosys.specs -u _printf_float -Wl,--no-warn-rwx-segments -lm

M0  := -mcpu=cortex-m0 -mthumb -mfloat-abi=soft
M4  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
M33 := -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard

# target: cpu flags, profile flags, flash, ram
RUNTIME := -DMCS_ENABLE_COMPILER=0 -DMCS_ENABLE_DISASM=0 -DMCS_ENABLE_SHELL=0 -DMCS_ENABLE_SCHED=0
TARGETS := m0-runtime m0-lowram m4-full m33-full m33-shell
m0-runtime_F := $(M0)  $(RUNTIME) -DCM_HEAP_SIZE=102400 -DCM_RAMFS_SIZE=2048
m0-runtime_M := FLASH_SIZE=256K RAM_SIZE=128K
# m0-lowram: profiles/mcs_profile_lowram.h on a 64 KB-RAM part, 40 KB pool (VM heap + RAM filesystem)
m0-lowram_F  := $(M0)  -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_lowram.h"' -DCM_HEAP_SIZE=40960 -DCM_RAMFS_SIZE=1024 -DCM_STACK_SLOTS=192 -DCM_FRAMES=32
m0-lowram_M  := FLASH_SIZE=256K RAM_SIZE=64K
m4-full_F    := $(M4)  -DCM_HEAP_SIZE=163840
m4-full_M    := FLASH_SIZE=512K RAM_SIZE=192K
# m33: sized like SiFli SF32LB525 (Cortex-M33, 512 KB SRAM); heap kept at 256 KB
m33-full_F   := $(M33) -DCM_HEAP_SIZE=262144
m33-full_M   := FLASH_SIZE=1024K RAM_SIZE=512K
m33-shell_F  := $(M33) -DCM_HEAP_SIZE=262144 -DCM_SHELL=1 -DCM_RAMFS_SIZE=32768
m33-shell_M  := FLASH_SIZE=1024K RAM_SIZE=512K

all: $(addprefix $(OUT)/,$(addsuffix .elf,$(TARGETS)))

$(OUT)/demo_image.h: demo.cs $(MCS)
	@mkdir -p $(OUT)
	$(MCS) -C demo.cs -n demo_image -o $@
$(OUT)/demo_src.h: demo.cs
	@mkdir -p $(OUT)
	python3 -c "import sys,json; s=open('demo.cs').read(); print('static const char demo_src[] = ' + json.dumps(s) + ';')" > $@
$(MCS):
	$(MAKE) -C $(ROOT) mcs

# per-target object directories so the map file attributes size per source file
vpath %.c $(ROOT)/src $(sort $(dir $(wildcard $(ROOT)/modules/*/*.c))) .
objs = $(addprefix $(OUT)/$(1)/,$(notdir $(SRC:.c=.o)))
define target_rule
$(OUT)/$(1)/%.o: %.c cm.ld Makefile $(OUT)/demo_image.h $(OUT)/demo_src.h
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $($(1)_F) -DCM_TARGET='"$(1)"' -c $$< -o $$@
$(OUT)/$(1).elf: $(call objs,$(1))
	$(CC) $($(1)_F) $$^ -o $$@ $(LDFLAGS) \
	  -Wl,--defsym=$(word 1,$($(1)_M)) -Wl,--defsym=$(word 2,$($(1)_M)) -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K \
	  -Wl,-Map=$(OUT)/$(1).map
	@$(SIZE) $$@
endef
$(foreach t,$(TARGETS),$(eval $(call target_rule,$(t))))

# examples/lowram/lowram_firmware.c on a Cortex-M0 with 48 KB RAM (32 KB pool), lowram profile
NODE_F := $(M0) -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_lowram.h"' -DMCS_ENABLE_FS=0 -DMCS_ENABLE_HAL=0 -DMCS_ENABLE_SCHED=0
NODE_SRC := $(wildcard $(ROOT)/src/*.c) startup.c board.c $(ROOT)/examples/lowram/lowram_firmware.c
all: $(OUT)/m0-node.elf
$(ROOT)/examples/lowram/node_image.h: $(ROOT)/examples/lowram/node.cs $(MCS)
	$(MCS) -C $< -n node_image -o $@
$(OUT)/m0-node.elf: $(NODE_SRC) $(ROOT)/examples/lowram/node_image.h cm.ld Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(NODE_F) -I$(ROOT)/examples/lowram $(NODE_SRC) -o $@ $(LDFLAGS) \
	  -Wl,--defsym=FLASH_SIZE=256K -Wl,--defsym=RAM_SIZE=48K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K -Wl,-Map=$(OUT)/m0-node.map
	@$(SIZE) $@

run: all
	@for t in $(TARGETS); do echo "== $$t"; python3 $(ROOT)/tools/cm_emu.py $(OUT)/$$t.elf --cpu $$(echo $$t | cut -d- -f1) < /dev/null || exit 1; done

clean:
	rm -rf $(OUT)
.PHONY: all run clean
