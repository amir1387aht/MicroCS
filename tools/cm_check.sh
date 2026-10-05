#!/bin/sh
# Build the Cortex-M reference firmware for every target, run each one in the
# Unicorn emulator, and check that the script output is identical to the host
# interpreter. Then run the UART shell protocol test on the M33 shell build.
# Needs: arm-none-eabi-gcc on PATH, python3 with `unicorn`.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make -s mcs
make -s -C ports/cortex-m -j4 >/dev/null
./mcs ports/cortex-m/demo.cs > build/cm/host.out
fail=0
for t in m0-runtime m4-full m33-full; do
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
python3 tests/test_cm_shell.py build/cm/m33-shell.elf || fail=1
exit $fail
