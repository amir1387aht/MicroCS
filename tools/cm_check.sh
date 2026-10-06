#!/bin/sh
# Build the Cortex-M reference firmware for every target, run each one in the
# Unicorn emulator, and check that the script output is identical to the host
# interpreter. Then run the UART shell protocol test on the M33 shell build.
# Needs: arm-none-eabi-gcc on PATH, python3 with `unicorn`.
set -e
if ! python3 -c "import unicorn" 2>/dev/null; then echo "cm-check needs the Unicorn emulator: pip3 install unicorn"; exit 2; fi
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make -s mcs
make -s -C ports/cortex-m -j4 >/dev/null
./mcs ports/cortex-m/demo.cs > build/cm/host.out
fail=0
for t in m0-runtime m0-lowram m4-full m33-full; do
    cpu=${t%%-*}
    python3 tools/cm_emu.py build/cm/$t.elf --cpu $cpu < /dev/null > build/cm/$t.log 2> build/cm/$t.emu
    # the demo prints its output once per execution mode (image, then source)
    n=$(grep -c '^\[.*\] result=0' build/cm/$t.log || true)
    grep -v '^\[\|^MicroCS' build/cm/$t.log > build/cm/$t.out
    exp=build/cm/host.out; [ "$n" = 2 ] && { cat build/cm/host.out build/cm/host.out > build/cm/host2.out; exp=build/cm/host2.out; }
    if [ "$n" -ge 1 ] && cmp -s build/cm/$t.out $exp; then echo "PASS $t ($n run(s) match host)"; else echo "FAIL $t"; fail=1; fi
    grep '^\[' build/cm/$t.log | sed 's/^/    /'
    sed 's/^/    /' build/cm/$t.emu
done
# the small-MCU firmware example (examples/lowram) on a 48 KB-RAM Cortex-M0
python3 tools/cm_emu.py build/cm/m0-node.elf --cpu m0 < /dev/null > build/cm/m0-node.log 2> build/cm/m0-node.emu
if grep '^\[node\]' build/cm/m0-node.log | cmp -s - examples/lowram/node.expected; then echo "PASS m0-node (examples/lowram matches host)"; else echo "FAIL m0-node"; fail=1; fi
grep '^\[c\]' build/cm/m0-node.log | sed 's/^/    /'
sed 's/^/    /' build/cm/m0-node.emu
python3 tests/test_cm_shell.py build/cm/m33-shell.elf || fail=1
exit $fail
