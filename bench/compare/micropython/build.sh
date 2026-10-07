#!/bin/sh
# MicroPython side of the MicroCS comparison (see ../README.md).
# Builds MicroPython (ports/embed) + mpbench.c for the same emulated Cortex-M4F
# and Cortex-M0 boards, toolchain flags and linker script as
# ports/cortex-m/bench.c, then runs both in tools/cm_emu.py.
#   MPY=/path/to/micropython sh bench/compare/micropython/build.sh
# Needs: MicroPython checkout (tested v1.26.0) with mpy-cross built,
# arm-none-eabi-gcc, python3 + unicorn.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=$ROOT/build/compare-mpy
: "${MPY:?set MPY to a MicroPython checkout}"
mkdir -p "$OUT/py"
cp "$HERE"/mpconfigport.h "$HERE"/mpbench.c "$OUT"/
cp "$ROOT"/ports/cortex-m/board.c "$ROOT"/ports/cortex-m/board.h "$ROOT"/ports/cortex-m/startup.c "$ROOT"/ports/cortex-m/cm.ld "$OUT"/
for b in fib loop objects sensor strings; do
    cp "$HERE/$b.py" "$OUT/py/"
    (cd "$OUT/py" && "$MPY/mpy-cross/build/mpy-cross" $b.py)
done
python3 "$HERE/gen.py" "$OUT/py" "$OUT/bench_py.h"
cd "$OUT"
printf 'MICROPYTHON_TOP = %s\ninclude $(MICROPYTHON_TOP)/ports/embed/embed.mk\n' "$MPY" > micropython_embed.mk
rm -rf build-embed micropython_embed
make -s -f micropython_embed.mk >/dev/null
# report a fatal (uncaught) error as an emulator exit code instead of spinning
sed -i 's/void nlr_jump_fail(void \*val) {/#include "board.h"\nvoid nlr_jump_fail(void *val) { board_exit(3);/' micropython_embed/port/embed_util.c
SRC="$(find micropython_embed -name '*.c' ! -name mphalport.c | tr '\n' ' ') board.c startup.c mpbench.c"
CF="-std=gnu99 -Os -ffunction-sections -fdata-sections -I. -Imicropython_embed -DNDEBUG -w"
LD="-Wl,--gc-sections -T cm.ld --specs=nano.specs --specs=nosys.specs -u _printf_float -Wl,--no-warn-rwx-segments -lm"
M0="-mcpu=cortex-m0 -mthumb -mfloat-abi=soft"
M4="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
arm-none-eabi-gcc $CF $M4 -DCM_TARGET='"m4-mpbench"' -DHEAP_SIZE=98304 $SRC -o m4-mpbench.elf $LD \
  -Wl,--defsym=FLASH_SIZE=512K -Wl,--defsym=RAM_SIZE=192K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K
arm-none-eabi-gcc $CF $M0 -DCM_TARGET='"m0-mpbench"' -DHEAP_SIZE=65536 $SRC -o m0-mpbench.elf $LD \
  -Wl,--defsym=FLASH_SIZE=256K -Wl,--defsym=RAM_SIZE=128K -Wl,--defsym=STACK_SIZE=8K -Wl,--defsym=SBRK_SIZE=2K
for t in m4 m0; do
    python3 "$ROOT/tools/cm_emu.py" $t-mpbench.elf --cpu $t < /dev/null > $t-mpbench.log 2> $t-mpbench.emu
    echo "== $t-mpbench"; grep '^\[bench\]' $t-mpbench.log
done
