#!/bin/sh
# Bare-metal C side of the MicroCS comparison (see ../README.md): cbench.c on the
# same emulated Cortex-M4F / M0 boards, toolchain flags (-Os) and linker script
# as ports/cortex-m/bench.c, plus the host build (-O2).
#   sh bench/compare/c/build.sh       (needs arm-none-eabi-gcc, python3 + unicorn)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
P=$ROOT/ports/cortex-m
OUT=$ROOT/build/compare-c
mkdir -p "$OUT"
CF="-std=gnu99 -Os -ffunction-sections -fdata-sections -I$P -Wall"
LD="-Wl,--gc-sections -T $P/cm.ld --specs=nano.specs --specs=nosys.specs -u _printf_float -Wl,--no-warn-rwx-segments -lm"
SRC="$HERE/cbench.c $P/board.c $P/startup.c"
arm-none-eabi-gcc $CF -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DCM_TARGET='"m4-cbench"' $SRC -o "$OUT/m4-cbench.elf" $LD \
  -Wl,--defsym=FLASH_SIZE=512K -Wl,--defsym=RAM_SIZE=192K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=32K
arm-none-eabi-gcc $CF -mcpu=cortex-m0 -mthumb -mfloat-abi=soft -DCM_TARGET='"m0-cbench"' $SRC -o "$OUT/m0-cbench.elf" $LD \
  -Wl,--defsym=FLASH_SIZE=256K -Wl,--defsym=RAM_SIZE=128K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=32K
for t in m4 m0; do
    python3 "$ROOT/tools/cm_emu.py" "$OUT/$t-cbench.elf" --cpu $t < /dev/null > "$OUT/$t-cbench.log" 2>&1
    echo "== $t-cbench"; grep '^\[bench\]' "$OUT/$t-cbench.log"
done
cc -O2 -DHOST "$HERE/cbench.c" -o "$OUT/cbench_host"
echo "== host (-O2)"; "$OUT/cbench_host" | grep ' ms$'
