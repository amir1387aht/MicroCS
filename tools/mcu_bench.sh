#!/bin/sh
# Instruction counts of bench/mcu/*.cs on emulated Cortex-M4 (full profile:
# optimized image, -O0 image, source) and Cortex-M0 (runtime only: images).
# Needs arm-none-eabi-gcc on PATH and python3 with `unicorn`.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make -s mcs
make -s -C ports/cortex-m bench >/dev/null
for t in m4-bench m0-bench; do
    python3 tools/cm_emu.py build/cm/$t.elf --cpu ${t%%-*} < /dev/null > build/cm/$t.log 2> build/cm/$t.emu
    echo "== $t"; grep '^\[bench\]' build/cm/$t.log
done
# every run must print the same as the host interpreter
for f in bench/mcu/*.cs; do ./mcs "$f"; done > build/cm/bench.host
for t in m4-bench m0-bench; do
    n=3; [ $t = m0-bench ] && n=2
    grep -v '^\[\|^MicroCS' build/cm/$t.log > build/cm/$t.out
    python3 - "$n" build/cm/bench.host build/cm/$t.out <<'PY' || { echo "FAIL $t output"; exit 1; }
import sys
n = int(sys.argv[1]); host = open(sys.argv[2]).read().splitlines(); got = open(sys.argv[3]).read().splitlines()
exp = [l for l in host for _ in range(n)]
sys.exit(0 if exp == got else 1)
PY
    echo "PASS $t output matches host"
done
