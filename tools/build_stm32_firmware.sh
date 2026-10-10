#!/bin/sh
# Build the ready-to-flash MicroCS firmware for an STM32 board (no CubeMX needed).
#   sh tools/build_stm32_firmware.sh <board> [output dir]      (default build/fw/stm32)
#   sh tools/build_stm32_firmware.sh list
# Produces microcs-<board>.elf / .bin / .hex. Needs arm-none-eabi-gcc; downloads the
# STM32Cube HAL + CMSIS sources of the family into build/sdk (or uses $STM32_SDK).
# The board setup (clock, console UART, I2C, PWM, LED, TinyFS in the internal flash)
# is in ports/stm32/firmware/board.c.
set -e
cd "$(dirname "$0")/.."
BOARDS="nucleo_f401re nucleo_f411re nucleo_f446re blackpill_f411ce nucleo_g474re nucleo_l476rg nucleo_h743zi"
board=$1
out=${2:-build/fw/stm32}
M4="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
M7="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard"
# board: family device startup cpu | flash layout: vector sector, files right after it (F4:
# the 16 KB sectors 1-3; 0 = files in the top MCS_INTFLASH_SIZE bytes), program | RAM, stack
F4="vect=16K; gap=48K; prog=448K; fs=49152"
case "$board" in
nucleo_f401re)    fam=f4; dev=STM32F401xE; start=stm32f401xe; cpu=$M4; eval "$F4"; ram=0x20000000; ramsz=96K;  stack=16K ;;
nucleo_f411re)    fam=f4; dev=STM32F411xE; start=stm32f411xe; cpu=$M4; eval "$F4"; ram=0x20000000; ramsz=128K; stack=16K ;;
blackpill_f411ce) fam=f4; dev=STM32F411xE; start=stm32f411xe; cpu=$M4; eval "$F4"; ram=0x20000000; ramsz=128K; stack=16K ;;
nucleo_f446re)    fam=f4; dev=STM32F446xx; start=stm32f446xx; cpu=$M4; eval "$F4"; ram=0x20000000; ramsz=128K; stack=16K ;;
nucleo_g474re)    fam=g4; dev=STM32G474xx; start=stm32g474xx; cpu=$M4; vect=4K; gap=0; prog=380K;  fs=131072; extra=-DMCS_STM32_FS_BLOCK=4096; ram=0x20000000; ramsz=128K; stack=16K ;;
nucleo_l476rg)    fam=l4; dev=STM32L476xx; start=stm32l476xx; cpu=$M4; vect=4K; gap=0; prog=892K;  fs=131072; extra=-DMCS_STM32_FS_BLOCK=4096; ram=0x20000000; ramsz=96K;  stack=16K ;;
nucleo_h743zi)    fam=h7; dev=STM32H743xx; start=stm32h743xx; cpu=$M7; vect=4K; gap=0; prog=1532K; fs=524288; ram=0x24000000; ramsz=512K; stack=32K ;;
list) echo $BOARDS; exit 0 ;;
*) echo "usage: $0 <board> [output dir]   boards: $BOARDS" >&2; exit 2 ;;
esac
extra=${extra:-}
macro=MCS_FW_$(echo "$board" | tr a-z A-Z)

sdk=${STM32_SDK:-build/sdk}
mkdir -p "$sdk"
[ -d "$sdk/stm32${fam}xx-hal-driver" ] || git clone -q --depth 1 https://github.com/STMicroelectronics/stm32${fam}xx-hal-driver.git "$sdk/stm32${fam}xx-hal-driver"
[ -d "$sdk/cmsis-device-${fam}" ] || git clone -q --depth 1 https://github.com/STMicroelectronics/cmsis-device-${fam}.git "$sdk/cmsis-device-${fam}"
[ -d "$sdk/cmsis-core" ] || git clone -q --depth 1 https://github.com/STMicroelectronics/cmsis-core.git "$sdk/cmsis-core"
hal=$sdk/stm32${fam}xx-hal-driver
cmsis=$sdk/cmsis-device-${fam}
core=$(ls -d "$sdk"/cmsis-core/CMSIS/Core/Include "$sdk"/cmsis-core/Include 2>/dev/null | head -n 1)

obj=build/fw_obj/$board
rm -rf "$obj"; mkdir -p "$obj" "$out"
# every HAL module enabled (the linker drops what is unused); HSE unused (HSI + PLL)
cp "$hal/Inc/stm32${fam}xx_hal_conf_template.h" "$obj/stm32${fam}xx_hal_conf.h"

CFLAGS="$cpu -std=gnu99 -Os -g -ffunction-sections -fdata-sections -D$dev -DUSE_HAL_DRIVER -D$macro \
 -DMCS_PORT_HAL=1 -DMCS_PROFILE=MCS_PROFILE_AUTO -DMCS_ENABLE_TINYFS=1 -DMCS_INTFLASH_SIZE=$fs -DMCS_STM32_EXTI_HANDLERS=1 $extra \
 -I$obj -I$hal/Inc -I$cmsis/Include -I$core -Iinclude -Iports/stm32"
# MicroCS (warnings are errors) and the vendor code (as shipped) compiled in parallel
{
    for f in src/*.c modules/*/*.c ports/stm32/mcs_port_stm32.c ports/stm32/firmware/board.c; do
        case $f in *mcs_vfs_posix.c) continue ;; esac
        echo "W $f"
    done
    for f in "$hal"/Src/*.c "$cmsis/Source/Templates/system_stm32${fam}xx.c"; do
        case $f in *template*) continue ;; esac
        echo "V $f"
    done
    echo "V $cmsis/Source/Templates/gcc/startup_$start.s"
} > "$obj/sources"
n=0
while read -r kind f; do
    n=$((n + 1))
    b=$(basename "$f"); echo "$kind $f $obj/$n-${b%.*}.o"
done < "$obj/sources" > "$obj/jobs"
export CFLAGS
xargs -P "$(nproc 2>/dev/null || echo 4)" -L 1 sh -c '
    w=""; [ "$0" = W ] && w="-Wall -Wextra -Werror"
    arm-none-eabi-gcc $CFLAGS $w -c "$1" -o "$2" 2> "$2.log" || { echo "FAIL $1"; cat "$2.log"; exit 255; }' < "$obj/jobs"

elf=$out/microcs-$board.elf
arm-none-eabi-gcc $cpu -T ports/stm32/firmware/stm32.ld \
    -Wl,--defsym=VECT_SIZE=$vect -Wl,--defsym=FS_SIZE=$gap -Wl,--defsym=FLASH_SIZE=$prog -Wl,--defsym=RAM_ORIGIN=$ram -Wl,--defsym=RAM_SIZE=$ramsz -Wl,--defsym=STACK_SIZE=$stack \
    -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map="$obj/microcs.map" --specs=nano.specs --specs=nosys.specs \
    "$obj"/*.o -lm -o "$elf"
# .bin: one image from 0x08000000 (gaps = erased 0xFF); .hex: only the program (keeps the files on update)
arm-none-eabi-objcopy -O binary --gap-fill 0xFF "$elf" "$out/microcs-$board.bin"
arm-none-eabi-objcopy -O ihex "$elf" "$out/microcs-$board.hex"
# program = flash used after the vector sector / file gap; static RAM = .data + .bss (the rest: heap + stack)
skip=$(( (${vect%K} + ${gap%K}) * 1024 ))
arm-none-eabi-nm "$elf" | awk -v b="$board" -v p="$prog" -v fs="$fs" -v ram="$ram" -v skip="$skip" '
    { a[$3] = strtonum("0x" $1) }
    END { printf "%-17s program %6.1f KB of %s, files %d KB, static RAM %5.1f KB, heap %5.1f KB\n", b,
          (a["_sidata"] + a["_edata"] - a["_sdata"] - 0x08000000 - skip) / 1024,
          p, fs / 1024, (a["_ebss"] - strtonum(ram)) / 1024, (a["_heap_end"] - a["_heap_start"]) / 1024 }'
