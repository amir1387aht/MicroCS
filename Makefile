# MicroCS - build for the host (CLI + tests). For firmware, just add src/*.c
# and include/ to your project (see docs/PORTING.md).
CC      ?= cc
CFLAGS  ?= -O2 -g
override CFLAGS += -std=gnu99 -Wall -Wextra -Iinclude
LDLIBS  += -lm
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

test: mcs build/test_modules
	sh tests/run_tests.sh ./mcs
	./build/test_modules
	@if command -v python3 >/dev/null 2>&1; then python3 tests/test_shell.py ./mcs; else echo "SKIP shell tests (no python3)"; fi

build/test_modules: tests/c/test_modules.c $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) tests/c/test_modules.c -o $@ $(LDLIBS)

# Full verification: tests, GC torture, every feature-flag combination
FLAG_SETS = "-DMCS_FLOAT_DOUBLE=0" "-DMCS_ENABLE_FLOAT=0" "-DMCS_ENABLE_COMPILER=0" \
	"-DMCS_ENABLE_DICT=0 -DMCS_ENABLE_LIST=0" "-DMCS_LAZY_REGS=0" "-DMCS_COMPUTED_GOTO=0" \
	"-DMCS_ENABLE_DISASM=0 -DMCS_ENABLE_LINES=0 -DMCS_ENABLE_BYTECODE_SAVE=0" "-DMCS_INT64=1" \
	"-DMCS_ENABLE_FS=0" "-DMCS_ENABLE_HAL=0 -DMCS_ENABLE_SCHED=0" "-DMCS_ENABLE_SHELL=0" \
	"-DMCS_ENABLE_FS=0 -DMCS_ENABLE_HAL=0 -DMCS_ENABLE_SCHED=0 -DMCS_ENABLE_COMPILER=0" \
	"-DMCS_USER_CONFIG_FILE=\"profiles/mcs_profile_tiny.h\"" "-DMCS_USER_CONFIG_FILE=\"profiles/mcs_profile_mcu.h\"" \
	"-DMCS_USER_CONFIG_FILE=\"profiles/mcs_profile_embedded.h\"" "-DMCS_USER_CONFIG_FILE=\"profiles/mcs_profile_linux.h\""
check: test
	@echo "== GC stress"; $(CC) -std=gnu99 -O1 -Iinclude -DMCS_GC_STRESS=1 $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_gc && \
	cd tests && for t in t0*.cs; do o=$$(head -n 1 $$t | sed -n 's|^// args: *||p'); \
	case $$t in *gc_stress*) o="--heap 196608 --stack 256";; esac; \
	../build/mcs_gc $$o $$t > ../build/gc.txt 2>&1; cmp -s ../build/gc.txt $${t%.cs}.out && echo "PASS $$t" || { echo "FAIL $$t"; exit 1; }; done
	@echo "== feature flag builds"; for f in $(FLAG_SETS); do \
	$(CC) -std=gnu99 -Wall -Wextra -Werror -Iinclude $$f $(SRC) $(MOD_SRC) ports/unix/main.c -lm -o build/mcs_flags || { echo "BUILD FAIL $$f"; exit 1; }; \
	echo "OK $$f"; done

# Debug build with sanitizers
asan: clean
	$(MAKE) CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDLIBS="-lm -fsanitize=address,undefined"

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

clean:
	rm -rf build mcs

.PHONY: all test check asan size clean example cm cm-check bench lfs-test

# LittleFS backend test (downloads littlefs v2.9.3, BSD-3-Clause, into build/third_party)
LFS_DIR = build/third_party/littlefs-2.9.3
$(LFS_DIR)/lfs.c:
	mkdir -p build/third_party && curl -sSL https://github.com/littlefs-project/littlefs/archive/refs/tags/v2.9.3.tar.gz | tar xz -C build/third_party
lfs-test: $(LFS_DIR)/lfs.c
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Iinclude -I$(LFS_DIR) -DMCS_ENABLE_LFS=1 -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
	  $(SRC) $(MOD_SRC) $(LFS_DIR)/lfs.c $(LFS_DIR)/lfs_util.c tests/c/test_lfs.c -lm -o build/test_lfs
	./build/test_lfs

# host benchmark: VM startup / compile / source run / image run, best of 5
bench: $(OBJ)
	$(CC) -O2 -std=gnu99 -Iinclude $(OBJ) bench/bench_host.c -o build/bench_host $(LDLIBS)
	@for f in bench/fib.cs bench/loop.cs bench/objects.cs ports/cortex-m/demo.cs; do ./build/bench_host $$f 5; done

# Cortex-M reference firmware (needs arm-none-eabi-gcc) and emulator check (needs python unicorn)
cm: mcs
	$(MAKE) -C ports/cortex-m
cm-check:
	sh tools/cm_check.sh
