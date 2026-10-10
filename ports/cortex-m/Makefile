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
# m0-lowram: the lowram profile on a 64 KB-RAM part, 40 KB pool (VM heap + RAM filesystem)
m0-lowram_F  := $(M0)  -DMCS_PROFILE=MCS_PROFILE_LOWRAM -DCM_HEAP_SIZE=40960 -DCM_RAMFS_SIZE=1024 -DCM_STACK_SLOTS=192 -DCM_FRAMES=32
m0-lowram_M  := FLASH_SIZE=256K RAM_SIZE=64K
m4-full_F    := $(M4)  -DCM_HEAP_SIZE=163840
m4-full_M    := FLASH_SIZE=512K RAM_SIZE=192K
# m33: sized like a Cortex-M33 part with 512 KB SRAM (STM32H5/U5, RP2350 class); heap kept at 256 KB
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
# (profile and options: examples/lowram/mcs_user_config.h, found via -I$(ROOT)/examples/lowram)
NODE_F := $(M0) -DNODE_STACK_HIGH_WATER=board_stack_high_water -DNODE_PUTS=board_puts
NODE_SRC := $(wildcard $(ROOT)/src/*.c) startup.c board.c $(ROOT)/examples/lowram/lowram_firmware.c
all: $(OUT)/m0-node.elf
$(ROOT)/examples/lowram/node_image.h: $(ROOT)/examples/lowram/node.cs $(MCS)
	$(MCS) -O0 -C $< -n node_image -o $@
$(OUT)/m0-node.elf: $(NODE_SRC) $(ROOT)/examples/lowram/node_image.h cm.ld Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(NODE_F) -I$(ROOT)/examples/lowram $(NODE_SRC) -o $@ $(LDFLAGS) \
	  -Wl,--defsym=FLASH_SIZE=256K -Wl,--defsym=RAM_SIZE=48K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K -Wl,-Map=$(OUT)/m0-node.map
	@$(SIZE) $@
# the same firmware on a 16 KB-RAM Cortex-M0 (the minimum the lowram profile
# targets): 12 KB VM pool + 2 KB C stack + 512 B libc heap + statics
all: $(OUT)/m0-16k.elf
$(OUT)/m0-16k.elf: $(NODE_SRC) $(ROOT)/examples/lowram/node_image.h cm.ld Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(NODE_F) -DNODE_HEAP_SIZE=12288 \
	  -I$(ROOT)/examples/lowram $(NODE_SRC) -o $@ $(LDFLAGS) \
	  -Wl,--defsym=FLASH_SIZE=256K -Wl,--defsym=RAM_SIZE=16K -Wl,--defsym=STACK_SIZE=2K -Wl,--defsym=SBRK_SIZE=512 -Wl,-Map=$(OUT)/m0-16k.map
	@$(SIZE) $@
# ...and with the min profile (overrides the example header's lowram) on a 64 KB-flash / 16 KB-RAM part
# (the linker fails if the firmware does not fit in 64 KB). No -u _printf_float.
MIN_F := $(M0) -DMCS_PROFILE=MCS_PROFILE_MIN -DNODE_STACK_HIGH_WATER=board_stack_high_water -DNODE_PUTS=board_puts
all: $(OUT)/m0-64k.elf
$(OUT)/m0-64k.elf: $(NODE_SRC) $(ROOT)/examples/lowram/node_image.h cm.ld Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(MIN_F) -DNODE_HEAP_SIZE=12288 -I$(ROOT)/examples/lowram $(NODE_SRC) -o $@ \
	  -Wl,--gc-sections -T cm.ld --specs=nano.specs --specs=nosys.specs -Wl,--no-warn-rwx-segments \
	  -Wl,--defsym=FLASH_SIZE=64K -Wl,--defsym=RAM_SIZE=16K -Wl,--defsym=STACK_SIZE=2K -Wl,--defsym=SBRK_SIZE=512 -Wl,-Map=$(OUT)/m0-64k.map
	@$(SIZE) $@

run: all
	@for t in $(TARGETS); do echo "== $$t"; python3 $(ROOT)/tools/cm_emu.py $(OUT)/$$t.elf --cpu $$(echo $$t | cut -d- -f1) < /dev/null || exit 1; done

clean:
	rm -rf $(OUT)
.PHONY: all run clean

# benchmark firmware (tools/mcu_bench.sh): bench/mcu/*.cs as optimized image,
# -O0 image and source; M4 full profile and M0 runtime-only (images only)
BENCH_SRC := $(filter-out %main.c,$(SRC)) bench.c
$(OUT)/bench_scripts.h: $(wildcard $(ROOT)/bench/mcu/*.cs) $(ROOT)/tools/gen_bench_h.py $(MCS)
	@mkdir -p $(OUT)
	python3 $(ROOT)/tools/gen_bench_h.py $@
bench: $(OUT)/m4-bench.elf $(OUT)/m0-bench.elf
$(OUT)/m4-bench.elf: $(BENCH_SRC) $(OUT)/bench_scripts.h cm.ld Makefile
	$(CC) $(CFLAGS) $(M4) $(BENCH_CFLAGS) -DCM_TARGET='"m4-bench"' -DCM_HEAP_SIZE=98304 $(BENCH_SRC) -o $@ $(LDFLAGS) \
	  -Wl,--defsym=FLASH_SIZE=512K -Wl,--defsym=RAM_SIZE=192K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K
	@$(SIZE) $@
$(OUT)/m0-bench.elf: $(BENCH_SRC) $(OUT)/bench_scripts.h cm.ld Makefile
	$(CC) $(CFLAGS) $(M0) $(RUNTIME) $(BENCH_CFLAGS) -DCM_TARGET='"m0-bench"' -DCM_HEAP_SIZE=65536 $(BENCH_SRC) -o $@ $(LDFLAGS) \
	  -Wl,--defsym=FLASH_SIZE=256K -Wl,--defsym=RAM_SIZE=128K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K
	@$(SIZE) $@
.PHONY: bench
