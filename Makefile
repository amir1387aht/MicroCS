# MicroCS - build for the host (CLI + tests). For firmware, just add src/*.c
# and include/ to your project (see docs/PORTING.md).
CC      ?= cc
CFLAGS  ?= -O2 -g
override CFLAGS += -std=gnu99 -Wall -Wextra -Iinclude
LDLIBS  += -lm
# Optional OS for real threads (docs/THREADS.md). Default: none - single-threaded, as always.
#   make OS=posix                         pthreads (host)
#   make OS=freertos FREERTOS_PATH=<FreeRTOS-Kernel> FREERTOS_PORT=<portable/GCC/ARM_CM4F> \
#        FREERTOS_CONFIG_DIR=<folder of FreeRTOSConfig.h>     (library objects; `make clean` when switching)
OS ?= none
ifeq ($(OS),posix)
override CFLAGS += -DMCS_OS=MCS_OS_POSIX -pthread
LDLIBS += -pthread
else ifeq ($(OS),freertos)
ifeq ($(FREERTOS_PATH),)
$(error OS=freertos: tell make where FreeRTOS is: FREERTOS_PATH=<FreeRTOS-Kernel folder> FREERTOS_PORT=<port folder, relative to it or absolute> FREERTOS_CONFIG_DIR=<folder of your FreeRTOSConfig.h> (see docs/THREADS.md))
endif
ifeq ($(wildcard $(FREERTOS_PATH)/include/FreeRTOS.h),)
$(error OS=freertos: $(FREERTOS_PATH)/include/FreeRTOS.h not found - FREERTOS_PATH must be the FreeRTOS-Kernel folder (see docs/THREADS.md))
endif
ifeq ($(wildcard $(FREERTOS_CONFIG_DIR)/FreeRTOSConfig.h),)
$(error OS=freertos: FreeRTOSConfig.h not found - set FREERTOS_CONFIG_DIR=<folder of your FreeRTOSConfig.h> (see docs/THREADS.md))
endif
FREERTOS_PORT_DIR = $(if $(wildcard $(FREERTOS_PATH)/$(FREERTOS_PORT)),$(FREERTOS_PATH)/$(FREERTOS_PORT),$(FREERTOS_PORT))
override CFLAGS += -DMCS_OS=MCS_OS_FREERTOS -I$(FREERTOS_PATH)/include -I$(FREERTOS_PORT_DIR) -I$(FREERTOS_CONFIG_DIR)
else ifneq ($(OS),none)
$(error OS=$(OS): choose none (default), posix or freertos; Zephyr builds go through west (ports/zephyr))
endif
SRC      = $(wildcard src/*.c)
MOD_SRC  = $(wildcard modules/*/*.c)
OBJ      = $(patsubst src/%.c,build/%.o,$(SRC)) $(patsubst modules/%.c,build/mod/%.o,$(MOD_SRC))

all: mcs

build/%.o: src/%.c src/*.h include/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/mod/%.o: modules/%.c include/*.h | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
build:
	mkdir -p build

mcs: $(OBJ) ports/unix/main.c
	$(CC) $(CFLAGS) $(OBJ) ports/unix/main.c -o $@ $(LDLIBS)

test: mcs build/test_modules build/test_flash test-config config-check tinyfs-test threads-test
	sh tests/run_tests.sh ./mcs
	./build/test_modules
	./build/test_flash
	@if command -v python3 >/dev/null 2>&1; then python3 tests/test_shell.py ./mcs; else echo "SKIP shell tests (no python3)"; fi
	@./mcs examples/tour.cs | cmp -s - examples/tour.out && echo "PASS examples/tour.cs" || { echo "FAIL examples/tour.cs"; exit 1; }
	@./mcs --sim --run-for 600 examples/blink.cs > /dev/null && echo "PASS examples/blink.cs (smoke)" || { echo "FAIL examples/blink.cs"; exit 1; }
	@./mcs --sim --ramfs 32768 --run-for 1200 examples/sensor_logger.cs > /dev/null && echo "PASS examples/sensor_logger.cs (smoke)" || { echo "FAIL examples/sensor_logger.cs"; exit 1; }
	@for f in examples/hardware/*.cs; do ./mcs --sim --sim-virtual --ramfs 16384 --run-for 1500 $$f > /dev/null 2>build/hw.err || { echo "FAIL $$f"; cat build/hw.err; exit 1; }; done; echo "PASS examples/hardware/*.cs (smoke, $$(ls examples/hardware/*.cs | wc -l) scripts)"
	@$(MAKE) -s quickstart > build/quickstart.txt 2>&1 && grep -q 'Add(40, 2) = 42' build/quickstart.txt && echo "PASS examples/quickstart_embed.c" || { echo "FAIL examples/quickstart_embed.c"; cat build/quickstart.txt; exit 1; }
	@$(MAKE) -s example-lowram > build/lowram.txt 2>&1 && grep '^\[node\]' build/lowram.txt | cmp -s - examples/lowram/node.expected && echo "PASS examples/lowram (lowram profile, XIP image)" || { echo "FAIL examples/lowram"; cat build/lowram.txt; exit 1; }

build/test_modules: tests/c/test_modules.c $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) tests/c/test_modules.c -o $@ $(LDLIBS)
build/test_flash: tests/c/test_flash.c tests/c/flash_sim.h $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) tests/c/test_flash.c -o $@ $(LDLIBS)

# Real OS threads (docs/THREADS.md) on POSIX threads: the C# Thread/Worker/Channel suite through
# the CLI and the firmware runtime (pool heap, RAM filesystem, jobs.cfg `thread` jobs, C API).
# Built separately: the default `mcs` stays OS-free.
THREADS_FLAGS = -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iinclude -DMCS_OS=MCS_OS_POSIX -pthread
threads-test: | build
	@$(CC) $(THREADS_FLAGS) $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_threads
	@./build/mcs_threads --fs tests/threads tests/threads/t_threads.cs > build/threads.txt 2>&1; \
	cmp -s build/threads.txt tests/threads/t_threads.out && echo "PASS tests/threads/t_threads.cs (POSIX threads)" || { echo "FAIL tests/threads/t_threads.cs"; diff build/threads.txt tests/threads/t_threads.out; exit 1; }
	@$(CC) $(THREADS_FLAGS) $(SRC) $(MOD_SRC) tests/c/test_threads.c -lm -o build/test_threads
	@./build/test_threads | tail -n 1
	@$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Iinclude $(SRC) $(MOD_SRC) tests/c/test_threads.c -lm -o build/test_nothreads && ./build/test_nothreads | head -n 1
# the same under ThreadSanitizer (data races between thread VMs, the console, the pool, the filesystem)
tsan-test: | build
	$(CC) $(THREADS_FLAGS) -fsanitize=thread $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_tsan
	TSAN_OPTIONS=halt_on_error=1 ./build/mcs_tsan --fs tests/threads tests/threads/t_threads.cs > build/tsan.txt && cmp build/tsan.txt tests/threads/t_threads.out
	$(CC) $(THREADS_FLAGS) -fsanitize=thread $(SRC) $(MOD_SRC) tests/c/test_threads.c -lm -o build/test_threads_tsan
	TSAN_OPTIONS=halt_on_error=1 ./build/test_threads_tsan

# FreeRTOS backend on the FreeRTOS POSIX (Linux simulator) port: downloads FreeRTOS-Kernel
# V11.1.0 (MIT) into build/third_party; tests/c/test_threads.c runs as a FreeRTOS task
FREERTOS_TEST_DIR = build/third_party/FreeRTOS-Kernel-11.1.0
FREERTOS_SIM = $(FREERTOS_TEST_DIR)/portable/ThirdParty/GCC/Posix
$(FREERTOS_TEST_DIR)/tasks.c:
	mkdir -p build/third_party && curl -sSL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.1.0.tar.gz | tar xz -C build/third_party
freertos-test: $(FREERTOS_TEST_DIR)/tasks.c
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iinclude -Itests/c/freertos -I$(FREERTOS_TEST_DIR)/include -I$(FREERTOS_SIM) \
	  -DMCS_OS=MCS_OS_FREERTOS -DMCS_THREAD_STACK=262144 -c modules/os/mcs_os.c -o build/mcs_os_freertos.o
	$(CC) -std=gnu99 -O1 -g -Iinclude -Itests/c/freertos -I$(FREERTOS_TEST_DIR)/include -I$(FREERTOS_SIM) -I$(FREERTOS_SIM)/utils \
	  -DMCS_OS=MCS_OS_FREERTOS -DMCS_THREAD_STACK=262144 $(SRC) $(MOD_SRC) tests/c/test_threads.c \
	  $(addprefix $(FREERTOS_TEST_DIR)/,tasks.c queue.c list.c portable/MemMang/heap_3.c) $(FREERTOS_SIM)/port.c \
	  $(FREERTOS_SIM)/utils/wait_for_event.c -lm -pthread -o build/test_threads_freertos
	./build/test_threads_freertos
	@$(CC) -std=gnu99 -Iinclude -DMCS_OS=MCS_OS_FREERTOS -c modules/os/mcs_os.c -o build/x.o 2>&1 | grep -q "FreeRTOS.h is not on the include path" \
	  && echo "OK missing FreeRTOS headers: build error says where to set the path" || { echo "FAIL no FreeRTOS path message"; exit 1; }

# Project config header (include/mcs_config.h + tests/c/config/mcs_user_config.h):
# found on the include path, named with MCS_USER_CONFIG_FILE, forced with
# MCS_USER_CONFIG=1 and skipped with MCS_USER_CONFIG=0; library and test are
# built with the same flags each time.
CONFIG_VARIANTS = "-Itests/c/config" "-I. -DMCS_USER_CONFIG_FILE=\"tests/c/config/mcs_user_config.h\"" \
	"-Itests/c/config -DMCS_USER_CONFIG=1" "-Itests/c/config -DMCS_USER_CONFIG=0 -DEXPECT_NO_HEADER"
test-config: | build
	@for f in $(CONFIG_VARIANTS); do \
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Iinclude $$f -DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN -DMCS_GC_GROW=3 \
	  $(SRC) $(MOD_SRC) tests/c/test_config.c -o build/test_config $(LDLIBS) && ./build/test_config || { echo "FAIL config: $$f"; exit 1; }; done

# The config template holds every option with the full profile's defaults;
# tools/gen_config.py writes it for the other profiles (each must build -Werror,
# and a header for another profile than the build asks for must warn).
GEN_PROFILES = min lowram tiny mcu embedded linux
config-check: | build
	@python3 tools/gen_config.py --check
	@for p in $(GEN_PROFILES); do mkdir -p build/gencfg/$$p; \
	python3 tools/gen_config.py --profile $$p -o build/gencfg/$$p/mcs_user_config.h || exit 1; done; \
	mkdir -p build/gencfg/auto; python3 tools/gen_config.py --profile auto --ram-kb 20 --flash-kb 128 -o build/gencfg/auto/mcs_user_config.h
	@echo 'int main(void){return 0;}' > build/cfg_warn.c; \
	$(CC) -Iinclude -Iconfig -DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN -include mcs.h -c build/cfg_warn.c -o build/cfg_warn.o 2>&1 | grep -q "another profile" \
	  && echo "OK config template: profile mismatch warns" || { echo "FAIL config template: no profile mismatch warning"; exit 1; }

# Full verification: tests, GC torture, every feature-flag combination
FLAG_SETS = "-DMCS_FLOAT_DOUBLE=0" "-DMCS_ENABLE_FLOAT=0" "-DMCS_ENABLE_COMPILER=0" \
	"-DMCS_ENABLE_DICT=0 -DMCS_ENABLE_LIST=0" "-DMCS_LAZY_REGS=0" "-DMCS_COMPUTED_GOTO=0" \
	"-DMCS_ENABLE_DISASM=0 -DMCS_ENABLE_LINES=0 -DMCS_ENABLE_BYTECODE_SAVE=0" "-DMCS_INT64=1" \
	"-DMCS_ENABLE_FS=0" "-DMCS_ENABLE_HAL=0 -DMCS_ENABLE_SCHED=0" "-DMCS_ENABLE_SHELL=0" \
	"-DMCS_ENABLE_FS=0 -DMCS_ENABLE_HAL=0 -DMCS_ENABLE_SCHED=0 -DMCS_ENABLE_COMPILER=0" \
	"-DMCS_USER_CONFIG_FILE=\"profiles/mcs_profile_tiny.h\"" "-DMCS_PROFILE=MCS_PROFILE_TINY" "-DMCS_PROFILE=MCS_PROFILE_MCU" \
	"-DMCS_PROFILE=MCS_PROFILE_EMBEDDED" "-DMCS_PROFILE=MCS_PROFILE_LINUX" \
	"-DMCS_PROFILE=MCS_PROFILE_LOWRAM" "-DMCS_COMPACT_VALUES=1" "-DMCS_ENABLE_LINQ=0" \
	"-DMCS_ENABLE_XIP=0" "-DMCS_TABLE_MIN_CAP=16 -DMCS_POOL_ALIGN=16 -DMCS_ERROR_SIZE=64" \
	"-DMCS_PROFILE=MCS_PROFILE_MIN" "-DMCS_LAZY_CLASSES=0" \
	"-DMCS_ENABLE_STRING_EXTRA=0 -DMCS_ENABLE_ARRAY_EXTRA=0 -DMCS_ENABLE_STACK_QUEUE=0" \
	"-DMCS_ENABLE_CONVERT=0 -DMCS_ENABLE_DIAGNOSTICS=0" "-DMCS_ENABLE_STDIO=0 -DMCS_ENABLE_MALLOC=0 -DMCS_TINY_PRINTF=1" \
	"-DMCS_PROFILE=MCS_PROFILE_AUTO -DMCS_TARGET_RAM_KB=16 -DMCS_TARGET_FLASH_KB=64" \
	"-DMCS_PROFILE=MCS_PROFILE_AUTO -DMCS_TARGET_RAM_KB=64 -DMCS_PORT_HAL=1" \
	"-DMCS_PROFILE=MCS_PROFILE_AUTO -DSTM32F072xB -DMCS_PORT_HAL=1" \
	"-DMCS_ENABLE_SUPEROPS=0" "-DMCS_OPTIMIZE_SOURCE=1" "-DMCS_COMPUTED_GOTO=0 -DMCS_ENABLE_SUPEROPS=0" \
	"-DMCS_ENABLE_WS2812=0" "-DMCS_ENABLE_DRIVERS=0" \
	"-Iconfig" "-DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN" "-Itests/c/config -DMCS_GC_GROW=3" \
	"-Ibuild/gencfg/min" "-Ibuild/gencfg/lowram" "-Ibuild/gencfg/tiny" "-Ibuild/gencfg/mcu" \
	"-Ibuild/gencfg/embedded" "-Ibuild/gencfg/linux" "-Ibuild/gencfg/auto" "-Iconfig -DMCS_ENABLE_TINYFS=1 -DMCS_ENABLE_SERVO=0" \
	"-DMCS_OS=MCS_OS_POSIX -pthread" "-DMCS_OS=MCS_OS_AUTO" "-DMCS_OS=MCS_OS_POSIX -pthread -DMCS_PROFILE=MCS_PROFILE_MCU"
# configurations whose whole script suite must still pass (not just build)
ALT_CONFIGS = "-DMCS_COMPACT_VALUES=1" "-DMCS_ENABLE_XIP=0" "-DMCS_TABLE_MIN_CAP=16" \
	"-DMCS_COMPUTED_GOTO=0 -DMCS_FIELD_CACHE=0" "-DMCS_GC_INITIAL=4096 -DMCS_POOL_ALIGN=16" \
	"-DMCS_LAZY_CLASSES=0" "-DMCS_LAZY_REGS=0" "-DMCS_TINY_PRINTF=1" \
	"-DMCS_ENABLE_SUPEROPS=0" "-DMCS_OPTIMIZE_SOURCE=1" "-DMCS_COMPUTED_GOTO=0 -DMCS_OPTIMIZE_SOURCE=1"
check: test
	@echo "== GC stress"; $(CC) -std=gnu99 -O1 -Iinclude -DMCS_GC_STRESS=1 $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_gc && \
	cd tests && for t in t*.cs; do o=$$(head -n 1 $$t | sed -n 's|^// args: *||p'); \
	case $$t in *gc_stress*) o="--heap 196608 --stack 256";; esac; \
	../build/mcs_gc $$o $$t > ../build/gc.txt 2>&1; cmp -s ../build/gc.txt $${t%.cs}.out && echo "PASS $$t" || { echo "FAIL $$t"; exit 1; }; done
	@echo "== feature flag builds"; for f in $(FLAG_SETS); do \
	$(CC) -std=gnu99 -Wall -Wextra -Werror -Iinclude $$f $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_flags || { echo "BUILD FAIL $$f"; exit 1; }; \
	echo "OK $$f"; done
	@for f in "" "-DMCS_ENABLE_WS2812=0" "-DMCS_ENABLE_SERVO=0" "-DMCS_ENABLE_DRIVERS=0" "-DMCS_ENABLE_HAL=0" "-DMCS_ENABLE_TINYFS=1"; do \
	$(CC) -std=gnu99 -Wall -Wextra -Werror -Iinclude $$f -c ports/template/mcs_port_template.c -o build/template.o || { echo "BUILD FAIL ports/template $$f"; exit 1; }; done; \
	echo "OK ports/template (drivers, flash filesystem skeleton)"
	@echo "== alternate configurations (full script suite)"; for f in $(ALT_CONFIGS); do \
	$(CC) -std=gnu99 -O1 -Iinclude $$f $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_alt || { echo "BUILD FAIL $$f"; exit 1; }; \
	sh tests/run_tests.sh ./build/mcs_alt > build/alt.txt 2>&1 && echo "OK $$f ($$(tail -n 1 build/alt.txt))" || { cat build/alt.txt | grep FAIL; echo "FAIL $$f"; exit 1; }; done

# Debug build with sanitizers
SAN_FLAGS = CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDLIBS="-lm -fsanitize=address,undefined"
asan: clean
	$(MAKE) $(SAN_FLAGS)
# whole test suite under ASan + UBSan (rebuilds everything; run `make clean` afterwards)
asan-test: clean
	$(MAKE) $(SAN_FLAGS) test

# Firmware-sized variants (object size report for a typical Cortex-M build
# with arm-none-eabi-gcc if available, else host compiler)
SIZE_CC ?= $(shell command -v arm-none-eabi-gcc >/dev/null 2>&1 && echo arm-none-eabi-gcc || echo $(CC))
SIZE_FLAGS = -std=gnu99 -Os -Iinclude -ffunction-sections -fdata-sections
size:
	@echo "== full (compiler + VM + stdlib)"; \
	for f in $(SRC); do $(SIZE_CC) $(SIZE_FLAGS) -c $$f -o /tmp/mcs_size_$$(basename $$f).o; done; \
	size /tmp/mcs_size_*.o | awk 'NR>1{t+=$$1;d+=$$2;b+=$$3} END{print "text="t" data="d" bss="b}'; rm -f /tmp/mcs_size_*.o
	@echo "== runtime only (-DMCS_ENABLE_COMPILER=0, runs precompiled .mcsb)"; \
	for f in $(SRC); do $(SIZE_CC) $(SIZE_FLAGS) -DMCS_ENABLE_COMPILER=0 -c $$f -o /tmp/mcs_size_$$(basename $$f).o; done; \
	size /tmp/mcs_size_*.o | awk 'NR>1{t+=$$1;d+=$$2;b+=$$3} END{print "text="t" data="d" bss="b}'; rm -f /tmp/mcs_size_*.o
	@echo "== minimal (no compiler, no float, no collections extras)"; \
	for f in $(SRC); do $(SIZE_CC) $(SIZE_FLAGS) -DMCS_ENABLE_COMPILER=0 -DMCS_ENABLE_FLOAT=0 -DMCS_ENABLE_DICT=0 -DMCS_ENABLE_STRINGBUILDER=0 -DMCS_ENABLE_DISASM=0 -c $$f -o /tmp/mcs_size_$$(basename $$f).o; done; \
	size /tmp/mcs_size_*.o | awk 'NR>1{t+=$$1;d+=$$2;b+=$$3} END{print "text="t" data="d" bss="b}'; rm -f /tmp/mcs_size_*.o

examples/app_image.h: examples/app.cs mcs
	./mcs -C examples/app.cs -n app_image -o $@

example: $(OBJ) examples/firmware_example.c examples/app_image.h
	$(CC) $(CFLAGS) -Iexamples $(OBJ) examples/firmware_example.c -o build/firmware_example $(LDLIBS)
	./build/firmware_example

# Option 1 in the README: the smallest drop-in embedding
quickstart: $(OBJ) examples/quickstart_embed.c
	$(CC) $(CFLAGS) $(OBJ) examples/quickstart_embed.c -o build/quickstart_embed $(LDLIBS)
	./build/quickstart_embed

# small-MCU firmware skeleton: whole library built with the lowram profile, image run in place
# (the profile and options come from examples/lowram/mcs_user_config.h)
LOWRAM_FLAGS =
examples/lowram/node_image.h: examples/lowram/node.cs mcs
	./mcs -O0 -C examples/lowram/node.cs -n node_image -o $@
example-lowram: examples/lowram/node_image.h examples/lowram/lowram_firmware.c | build
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Iinclude -Iexamples/lowram $(LOWRAM_FLAGS) $(SRC) examples/lowram/lowram_firmware.c -o build/lowram_firmware $(LDLIBS)
	./build/lowram_firmware

clean:
	rm -rf build mcs

.PHONY: all test threads-test tsan-test freertos-test test-config config-check check asan asan-test size clean example example-lowram quickstart cm cm-check bench mcu-bench lfs-test yaffs-test tinyfs-test fetch-lfs fetch-yaffs print-lfs-dir print-yaffs-dir

# LittleFS backend test (downloads littlefs v2.9.3, BSD-3-Clause, into build/third_party):
# RAM block device + LittleFS on the simulated SPI NOR and SPI NAND (bad blocks) chips
LFS_DIR = build/third_party/littlefs-2.9.3
$(LFS_DIR)/lfs.c:
	mkdir -p build/third_party && curl -sSL https://github.com/littlefs-project/littlefs/archive/refs/tags/v2.9.3.tar.gz | tar xz -C build/third_party
fetch-lfs: $(LFS_DIR)/lfs.c
print-lfs-dir:
	@echo $(LFS_DIR)
lfs-test: $(LFS_DIR)/lfs.c
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Iinclude -I$(LFS_DIR) -DMCS_ENABLE_LFS=1 -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
	  $(SRC) $(MOD_SRC) $(LFS_DIR)/lfs.c $(LFS_DIR)/lfs_util.c tests/c/test_lfs.c -lm -o build/test_lfs
	./build/test_lfs

# TinyFS (built-in flash filesystem) on a simulated internal flash: physics, model check, power cut at every step
tinyfs-test: | build
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iinclude -DMCS_ENABLE_TINYFS=1 $(SRC) $(MOD_SRC) tests/c/test_tinyfs.c -lm -o build/test_tinyfs
	./build/test_tinyfs

# YAFFS2 backend test (downloads a pinned yaffs2 revision, GPLv2, into build/third_party; only the
# test binary links it): YAFFS2 on the simulated SPI NAND (in-band and spare-area tags) and SPI NOR
YAFFS_REV = 474b3acb927d27b2305618aaf24456b9d33fe91b
YAFFS_DIR = build/third_party/yaffs2-$(YAFFS_REV)
YAFFS_CORE = yaffs_ecc yaffs_cache yaffs_guts yaffs_tagscompat yaffs_tagsmarshall yaffs_packedtags1 \
	yaffs_packedtags2 yaffs_nand yaffs_checkptrw yaffs_nameval yaffs_allocator yaffs_yaffs1 yaffs_yaffs2 \
	yaffs_bitmap yaffs_endian yaffs_verify yaffs_summary
YAFFS_DEFS = -DCONFIG_YAFFS_DIRECT -DCONFIG_YAFFS_YAFFS2 -DCONFIG_YAFFS_DEFINES_TYPES \
	-DCONFIG_YAFFS_PROVIDE_DEFS -DCONFIG_YAFFSFS_PROVIDE_VALUES
$(YAFFS_DIR)/direct/yaffs_guts.c:
	mkdir -p build/third_party && curl -sSL https://github.com/Aleph-One-Ltd/yaffs2/archive/$(YAFFS_REV).tar.gz | tar xz -C build/third_party
	cd $(YAFFS_DIR)/direct && for f in $(YAFFS_CORE); do for e in c h; do \
	  sed -e 's/strcat/yaffs_strcat/g' -e 's/strcpy/yaffs_strcpy/g' -e 's/strncpy/yaffs_strncpy/g' -e 's/strnlen/yaffs_strnlen/g' \
	      -e 's/strcmp/yaffs_strcmp/g' -e 's/strncmp/yaffs_strncmp/g' -e 's/loff_t/Y_LOFF_T/g' ../core/$$f.$$e > $$f.$$e; \
	  done; done; for h in yaffs_getblockinfo yaffs_trace yaffs_attribs; do \
	  sed -e 's/loff_t/Y_LOFF_T/g' ../core/$$h.h > $$h.h; done
fetch-yaffs: $(YAFFS_DIR)/direct/yaffs_guts.c
print-yaffs-dir:
	@echo $(YAFFS_DIR)/direct
yaffs-test: $(YAFFS_DIR)/direct/yaffs_guts.c
	$(CC) -std=gnu99 -O1 -w -Iinclude -I$(YAFFS_DIR)/direct $(YAFFS_DEFS) -DMCS_ENABLE_YAFFS=1 -DMCS_YAFFS_OSGLUE=1 \
	  $(SRC) $(MOD_SRC) $(addprefix $(YAFFS_DIR)/direct/,$(addsuffix .c,$(YAFFS_CORE))) \
	  $(YAFFS_DIR)/direct/yaffsfs.c $(YAFFS_DIR)/direct/yaffs_attribs.c $(YAFFS_DIR)/direct/yaffs_error.c \
	  $(YAFFS_DIR)/direct/yaffs_hweight.c tests/c/test_yaffs.c -lm -o build/test_yaffs
	./build/test_yaffs

# host benchmark: VM startup / compile / source run / image run, best of 5
bench: $(OBJ)
	$(CC) -O2 -std=gnu99 -Iinclude $(OBJ) bench/bench_host.c -o build/bench_host $(LDLIBS)
	@for f in bench/fib.cs bench/loop.cs bench/objects.cs ports/cortex-m/demo.cs; do ./build/bench_host $$f 5; done

# bench/mcu/*.cs as optimized image, -O0 image and source on emulated Cortex-M4/M0
mcu-bench: mcs
	sh tools/mcu_bench.sh

# Cortex-M reference firmware (needs arm-none-eabi-gcc) and emulator check (needs python unicorn)
cm: mcs
	$(MAKE) -C ports/cortex-m
cm-check:
	sh tools/cm_check.sh
