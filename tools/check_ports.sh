#!/bin/sh
# Compile-check a board port against the real vendor headers.
#   sh tools/check_ports.sh stm32 <family f4|h7|g4|l4|...> <device macro> <cpu>
# Downloads the STM32Cube HAL + CMSIS headers for the family into build/sdk
# (or uses $STM32_SDK if set) and compiles ports/stm32 with -Werror.
set -e
cd "$(dirname "$0")/.."
case "$1" in
stm32)
    fam=$2; dev=$3; cpu=${4:-cortex-m4}
    sdk=${STM32_SDK:-build/sdk}
    mkdir -p "$sdk"
    [ -d "$sdk/stm32${fam}xx-hal-driver" ] || git clone -q --depth 1 https://github.com/STMicroelectronics/stm32${fam}xx-hal-driver.git "$sdk/stm32${fam}xx-hal-driver"
    [ -d "$sdk/cmsis-device-${fam}" ] || git clone -q --depth 1 https://github.com/STMicroelectronics/cmsis-device-${fam}.git "$sdk/cmsis-device-${fam}"
    [ -d "$sdk/cmsis-core" ] || git clone -q --depth 1 https://github.com/STMicroelectronics/cmsis-core.git "$sdk/cmsis-core"
    conf=build/stm32conf_${fam}
    mkdir -p "$conf"
    cp "$sdk/stm32${fam}xx-hal-driver/Inc/stm32${fam}xx_hal_conf_template.h" "$conf/stm32${fam}xx_hal_conf.h" 2>/dev/null || \
        cp "$sdk"/stm32${fam}xx-hal-driver/stm32${fam}xx_hal_conf_template.h "$conf/stm32${fam}xx_hal_conf.h"
    core=$(ls -d "$sdk"/cmsis-core/CMSIS/Core/Include "$sdk"/cmsis-core/Include 2>/dev/null | head -n 1)
    for f in ports/stm32/mcs_port_stm32.c ports/stm32/example_main.c; do
        arm-none-eabi-gcc -mcpu="$cpu" -mthumb -std=gnu99 -Os -Wall -Wextra -Werror -c "$f" -o build/port_check.o \
            -D"$dev" -DUSE_HAL_DRIVER -I"$conf" -I"$sdk/stm32${fam}xx-hal-driver/Inc" -I"$sdk/cmsis-device-${fam}/Include" \
            -I"$core" -Iinclude -Iports/stm32
        echo "OK $f ($dev)"
    done
    ;;
*)
    echo "usage: $0 stm32 <family> <device> [cpu]"; exit 2 ;;
esac
