#!/bin/sh
# Recorded completed commands, run from repository root. Optional upstreams
# are independently pinned/unchanged; no hardware or hostile images executed.
set -eu
export LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
python3 tools/test_stack_capacity.py --sanitize --build-dir build/callback29-fixed
mkdir -p build/callback29-host
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iinclude -DMCS_USER_CONFIG=0 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie src/*.c modules/*/*.c ports/unix/main.c -lm -o build/callback29-host/mcs
sh tests/run_tests.sh ./build/callback29-host/mcs
make mcs
make check
# First full-check attempt failed a UDP lifecycle assertion. Its raw log is
# retained; unchanged-source targeted retry and entire full gate then passed.
python3 tests/test_net_lifecycle.py ./mcs
python3 tools/test_tft_headless.py --tft-root /data/deps/TFT_eSPI-upstream --sanitize --gc-stress --build-dir build/callback29-tft
# LVGL exact completed configure/build/golden recipe: run-real-lvgl.sh
