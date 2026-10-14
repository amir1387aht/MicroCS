#!/usr/bin/env bash
set -euo pipefail
cd /data/v2-mission/lazy-recovery
export LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
L=/data/v2-mission/logs/lazy-recovery/pending-flag-01
for mode in default gc eager-regs eager-classes both-eager; do
 flags=(); case "$mode" in gc) flags+=(-DMCS_GC_STRESS=1);; eager-regs) flags+=(-DMCS_LAZY_REGS=0);; eager-classes) flags+=(-DMCS_LAZY_CLASSES=0);; both-eager) flags+=(-DMCS_LAZY_CLASSES=0 -DMCS_LAZY_REGS=0);; esac
 for test in test_lazy_recovery test_lazy_module_recovery; do
  cc -std=gnu99 -O1 -g -Wall -Wextra -Iinclude -Isrc -DMCS_USER_CONFIG=0 -DMCS_ENABLE_THREADS=0 -fsanitize=address,undefined -fno-omit-frame-pointer "${flags[@]}" src/*.c modules/*/*.c "tests/c/$test.c" -lm -o "build/$test-$mode" > "$L/$test-$mode-build.log" 2>&1
  "build/$test-$mode" > "$L/$test-$mode-test.log" 2>&1
  tail -2 "$L/$test-$mode-test.log"
 done
done
