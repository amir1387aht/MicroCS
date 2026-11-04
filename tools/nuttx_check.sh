#!/bin/sh
# Build Apache NuttX's `sim:nsh` simulator with the MicroCS app (ports/nuttx, real threads on
# pthreads) and run the C# thread suite (tests/threads) on it: `make nuttx-test`.
#
#   NUTTX_REF=nuttx-12.11.0   NuttX / apps release tag to download into build/nuttx
#   NUTTX_SMP=1               also build an SMP simulator (4 CPUs) and check Thread.RunOn pinning
#
# Needs: git, cmake, ninja, gcc (x86-64 Linux, 32-bit multilib not needed), python3 with
# `kconfiglib` (pip install kconfiglib), genromfs and xxd (apt: genromfs xxd).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF=${NUTTX_REF:-nuttx-12.11.0}
W=$ROOT/build/nuttx
mkdir -p "$W"

for t in git cmake ninja genromfs xxd; do
    command -v "$t" >/dev/null 2>&1 || { echo "nuttx-test: '$t' not found" >&2; exit 2; }
done
python3 -c 'import kconfiglib' 2>/dev/null || { echo "nuttx-test: pip install kconfiglib" >&2; exit 2; }

fetch() { # <repo> <dir>
    [ -d "$W/$2/.git" ] || git clone --quiet --depth 1 --branch "$REF" "https://github.com/apache/$1.git" "$W/$2"
}
fetch nuttx nuttx
fetch nuttx-apps apps

mkdir -p "$W/apps/interpreters"
rm -f "$W/apps/interpreters/microcs"
ln -s "$ROOT/ports/nuttx" "$W/apps/interpreters/microcs"

SIM=$W/nuttx/boards/sim/sim/sim/configs

# <name> <base config> <extra lines...>
mkconfig() {
    name=$1; base=$2; shift 2
    mkdir -p "$SIM/$name"
    cp "$SIM/$base/defconfig" "$SIM/$name/defconfig"
    {
        echo "CONFIG_INTERPRETERS_MICROCS=y"
        echo "CONFIG_INTERPRETERS_MICROCS_THREADS=y"
        echo "CONFIG_FS_HOSTFS=y"
        for l in "$@"; do echo "$l"; done
    } >> "$SIM/$name/defconfig"
}

build() { # <config name>
    echo "== NuttX sim:$1"
    ( cd "$W/nuttx"
      rm -rf "build_$1"
      cmake -S . -B "build_$1" -GNinja -DBOARD_CONFIG="sim:$1" -DNUTTX_APPS_DIR="$W/apps" >"$W/cfg_$1.log" 2>&1 \
          || { tail -n 30 "$W/cfg_$1.log"; exit 1; }
      cmake --build "build_$1" >"$W/build_$1.log" 2>&1 || { tail -n 40 "$W/build_$1.log"; exit 1; } )
}

# run the simulator: feed NSH commands one at a time and wait for each marker (the simulator does
# not exit on EOF). <config name> <script file with nuttx shell lines> -> output on stdout
run_sim() {
    cfg=$1; shift
    fifo=$W/stdin.$$; out=$W/out_$cfg.txt
    rm -f "$fifo"; mkfifo "$fifo"; : > "$out"
    ( cd "$W/nuttx/build_$cfg" && exec ./nuttx < "$fifo" > "$out" 2>&1 ) &
    pid=$!
    exec 3> "$fifo"
    wait_for() { # <text> <seconds>
        i=0
        while ! tr -d '\r' < "$out" | grep -q "$1"; do
            i=$((i + 1)); [ "$i" -gt $(($2 * 10)) ] && { kill "$pid" 2>/dev/null || true; echo "nuttx-test: timeout waiting for '$1'" >&2; tail -n 20 "$out" >&2; exit 1; }
            sleep 0.1
        done
    }
    wait_for "nsh>" 30
    for cmd in "$@"; do
        sleep 0.3
        printf '%s\n' "$cmd" >&3
        sleep 0.3
    done
    printf 'echo DONE_MARK\n' >&3
    wait_for "^DONE_MARK" 120
    exec 3>&-
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    rm -f "$fifo"
    tr -d '\r' < "$out" | sed 's/\x1b\[K//g'
}

check_suite() { # <config name>
    got=$W/threads_$1.txt
    run_sim "$1" "mount -t hostfs -o fs=$ROOT/tests /host" \
        "mcs --fs /host/threads /host/threads/t_threads.cs" > "$W/raw_$1.txt"
    # The test deliberately triggers a thread exception; stderr writes can interleave on
    # SMP, even within a line. Compare the stable script output around that diagnostic block.
    stable() { awk '
        /^sum 550 state done count 0$/ { print; diagnostic=1; next }
        diagnostic && /^caught: True$/ { diagnostic=0 }
        !diagnostic && NF { print }
    '; }
    sed -n '/^os True/,/^formats /p' "$W/raw_$1.txt" | stable > "$got"
    stable < "$ROOT/tests/threads/t_threads.out" > "$W/expected.txt"
    if cmp -s "$got" "$W/expected.txt"; then
        echo "PASS tests/threads/t_threads.cs (NuttX $REF sim:$1)"
    else
        echo "FAIL tests/threads/t_threads.cs (NuttX sim:$1)"; diff "$got" "$W/expected.txt" || true; exit 1
    fi
}

mkconfig mcs nsh "CONFIG_INTERPRETERS_MICROCS_GPIO=y"
build mcs
run_sim mcs 'mcs --version' 'mcs -e "Console.WriteLine(Thread.Os);"' | grep -q '^NuttX' \
    || { echo "FAIL: Thread.Os is not NuttX"; exit 1; }
check_suite mcs
run_sim mcs "mount -t hostfs -o fs=$ROOT/tests /host" \
    "mcs /host/nuttx/t_gpio.cs" > "$W/raw_gpio.txt"
sed -n '/^native NuttX /,/^missing pin$/p' "$W/raw_gpio.txt" > "$W/gpio.txt"
cmp "$W/gpio.txt" "$ROOT/tests/nuttx/t_gpio.out" || { echo "FAIL NuttX GPIO"; exit 1; }
echo "PASS NuttX GPIO character devices (sim:mcs)"
run_sim mcs "mount -t hostfs -o fs=$ROOT/tests /host" "mount -t hostfs -o fs=$W /stage" \
    "mcs /host/nuttx/t_stack.cs" "mcs -c /host/nuttx/t_stack.cs -o /stage/stack.mcsb" \
    "mcs /stage/stack.mcsb" "mcs --xip /stage/stack.mcsb" > "$W/raw_stack.txt"
[ "$(grep -c '^stack caught$' "$W/raw_stack.txt")" -eq 3 ] && \
    [ "$(grep -c '^stack survived$' "$W/raw_stack.txt")" -eq 3 ] || \
    { echo "FAIL NuttX native stack guard"; tail -n 40 "$W/raw_stack.txt"; exit 1; }
echo "PASS NuttX native stack guard (source, image, XIP)"

if [ "${NUTTX_SMP:-0}" = 1 ]; then
    mkconfig mcssmp smp
    build mcssmp
    run_sim mcssmp 'mcs -e "Console.WriteLine(Thread.Cores);"' > "$W/raw_smp_cores.txt"
    grep -q '^[2-9]$' "$W/raw_smp_cores.txt" || { echo "FAIL: SMP simulator reports one CPU"; exit 1; }
    check_suite mcssmp
fi
echo "NuttX port OK"
