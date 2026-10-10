#!/bin/sh
# Compile-check a board port against the real vendor headers.
#   sh tools/check_ports.sh stm32 <family f4|h7|g4|l4|...> <device macro> <cpu>
# Compiles the port in several configurations at once (default, without the
# ws2812/servo drivers, auto profile, internal flash + LittleFS/YAFFS2/TinyFS).
# PORT_CFLAGS adds flags to every one, e.g. the auto profile:
#   PORT_CFLAGS="-DMCS_PORT_HAL=1 -DMCS_PROFILE=MCS_PROFILE_AUTO" sh tools/check_ports.sh stm32 f0 STM32F072xB cortex-m0
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
    # Every configuration is compiled in parallel (one compiler per file and
    # variant); a failing one prints its errors and fails the run.
    # CHECK_VARIANTS=quick: only the default configuration ($PORT_CFLAGS).
    make -s fetch-lfs fetch-yaffs > /dev/null
    lfs="-DMCS_ENABLE_LFS=1 -I$(make -s print-lfs-dir)"
    yaffs="-DMCS_ENABLE_YAFFS=1 -I$(make -s print-yaffs-dir) -DCONFIG_YAFFS_DIRECT -DCONFIG_YAFFS_YAFFS2 -DCONFIG_YAFFS_DEFINES_TYPES -DCONFIG_YAFFS_PROVIDE_DEFS -DCONFIG_YAFFSFS_PROVIDE_VALUES -DY_LOFF_T=off_t"
    out=build/port_check_${fam}_${dev}
    rm -rf "$out"; mkdir -p "$out"
    n=0
    run() {   # run <label> <flags> <files...>
        label=$1; flags=$2; shift 2
        for f in "$@"; do
            n=$((n + 1))
            ( arm-none-eabi-gcc -mcpu="$cpu" -mthumb -std=gnu99 -Os -Wall -Wextra -Werror -c "$f" -o "$out/$n.o" \
                -D"$dev" -DUSE_HAL_DRIVER -I"$conf" -I"$sdk/stm32${fam}xx-hal-driver/Inc" -I"$sdk/cmsis-device-${fam}/Include" \
                -I"$core" -Iinclude -Iports/stm32 $flags $PORT_CFLAGS > "$out/$n.log" 2>&1 \
              && echo "OK   $label: $f" > "$out/$n.res" || echo "FAIL $label: $f" > "$out/$n.res" ) &
        done
    }
    port="ports/stm32/mcs_port_stm32.c ports/stm32/example_main.c"
    run "default" "" $port
    if [ "$CHECK_VARIANTS" != quick ]; then
        run "no ws2812 / servo" "-DMCS_ENABLE_WS2812=0 -DMCS_ENABLE_SERVO=0" $port
        run "no drivers" "-DMCS_ENABLE_DRIVERS=0" $port
        run "auto profile" "-DMCS_PORT_HAL=1 -DMCS_PROFILE=MCS_PROFILE_AUTO" $port
        run "internal flash + LittleFS" "$lfs" $port modules/fs/mcs_flashfs.c
        run "internal flash + YAFFS2" "$yaffs" $port modules/fs/mcs_flashfs.c
        run "internal flash + TinyFS (8 KB)" "-DMCS_ENABLE_TINYFS=1 -DMCS_INTFLASH_SIZE=8192" $port modules/fs/mcs_flashfs.c modules/fs/mcs_vfs_tinyfs.c
        run "TinyFS + auto profile" "-DMCS_ENABLE_TINYFS=1 -DMCS_PORT_HAL=1 -DMCS_PROFILE=MCS_PROFILE_AUTO" $port modules/fs/mcs_vfs_tinyfs.c
    fi
    wait
    fail=0
    for r in $(ls "$out"/*.res | sort -t/ -k3 -n); do
        cat "$r"
        case $(cat "$r") in FAIL*) fail=1; cat "${r%.res}.log";; esac
    done
    [ $fail = 0 ] || { echo "FAILED: $dev"; exit 1; }
    echo "all $n compiles OK ($dev, $cpu)"
    ;;
*)
    echo "usage: $0 stm32 <family> <device> [cpu]"; exit 2 ;;
esac
