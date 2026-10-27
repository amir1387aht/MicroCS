#!/bin/sh
# Compare identical compiler/config on an explicit baseline checkout. No network.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
baseline=${1:?Pass a baseline MicroCS checkout (e.g. the PR base)}
baseline=$(CDPATH= cd -- "$baseline" && pwd)
out="$root/build/lvgl-disabled-proof"
mkdir -p "$out"
CC=${CC:-cc}
for name in baseline current; do
    tree=$root; [ "$name" != baseline ] || tree=$baseline
    (
        cd "$tree"
        $CC -std=gnu99 -O2 -g -Iinclude -Isrc src/*.c modules/*/*.c "$root/tests/lvgl/disabled_probe.c" -lm -o "$out/$name"
        $CC -std=gnu99 -O2 -Iinclude -c src/mcs_vm.c -o "$out/$name-vm.o"
        $CC -std=gnu99 -O2 -Iinclude -c modules/hal/mcs_hal_lib.c -o "$out/$name-caps.o"
    )
    "$out/$name" > "$out/$name.memory"
    objcopy -O binary --only-section=.text "$out/$name-vm.o" "$out/$name-vm.text"
    objcopy -O binary --only-section=.text "$out/$name-caps.o" "$out/$name-caps.text"
done
cmp "$out/baseline.memory" "$out/current.memory"
cmp "$out/baseline-vm.text" "$out/current-vm.text"
cmp "$out/baseline-caps.text" "$out/current-caps.text"
if nm "$out/current" | grep -E ' [TtBbDd] (mcs_lvgl_|lv_)';then echo 'FAIL: LVGL runtime symbols in disabled binary';exit 1;fi
cat "$out/current.memory"
echo 'PASS: disabled VM/capability machine code and memory match baseline; no LVGL runtime symbols'
