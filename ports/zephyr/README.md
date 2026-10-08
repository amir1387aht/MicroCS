# MicroCS for Zephyr RTOS

One port for every board Zephyr supports (nRF52/53/54, NXP i.MX RT / Kinetis / LPC, STM32,
Atmel SAM, RP2040, ESP32, Renesas, Silicon Labs…), using only Zephyr's portable driver APIs.
The example turns any board into a complete MicroCS device: C# REPL on the console (UART or
USB), files on LittleFS, `/boot.cs` · `/jobs.cfg` · `/main.cs` at start-up, every peripheral
from the devicetree, and the machine protocol [MicroCS Studio](../../tools/studio) and
`tools/mcs_remote.py` use.

Tested with Zephyr **4.1** (needs 3.6 or newer). CI builds the example for `native_sim`,
`nrf52840dk/nrf52840` (UART and USB console), `rpi_pico` and `nucleo_f429zi`, and runs the
`native_sim` firmware through [`tests/zephyr/smoke.py`](../../tests/zephyr/smoke.py)
(REPL, upload/run/ls/mv/rm, jobs, files surviving a restart).

## Quick start

A fresh workspace with MicroCS as the manifest (fetches Zephyr 4.1 and the Nordic, RP2040,
STM32 HALs and LittleFS):

```sh
west init -m https://github.com/amir1387aht/MicroCS --mf ports/zephyr/west.yml microcs-ws
cd microcs-ws && west update
west build -b nrf52840dk/nrf52840 microcs/ports/zephyr/example
west flash
```

In an existing workspace add MicroCS as a module (`west.yml` project, or
`-DZEPHYR_EXTRA_MODULES=/path/to/MicroCS`) and build `ports/zephyr/example`, or set
`CONFIG_MICROCS=y` in your own app.

Open the board's serial port at 115200 baud: you get the `>` C# prompt; MicroCS Studio connects
to the same port.

### USB console

Boards with a USB device controller (nRF52840 dongle / Feather, RP2040, STM32F4…) can put the
console on USB CDC ACM instead of a UART:

```sh
west build -b nrf52840dk/nrf52840 microcs/ports/zephyr/example -- \
    -DEXTRA_CONF_FILE=overlay-usb.conf -DEXTRA_DTC_OVERLAY_FILE=usb.overlay
```

### Try it without hardware

```sh
west build -b native_sim/native/64 microcs/ports/zephyr/example
./build/zephyr/zephyr.exe            # console on this terminal, files in flash.bin
```

## Files

`mcs_zephyr_fs_mount()` mounts LittleFS on the board's `storage_partition` at `/lfs` (formats
it the first time, or reuses an `fstab` automount) and C# sees it as `/`. Boards without a
storage partition fall back to a RAM filesystem — add one in `boards/<board>.overlay` (see
[`boards/rpi_pico.overlay`](example/boards/rpi_pico.overlay)). **YAFFS2** on the same partition: build with
`-DEXTRA_CONF_FILE=overlay-yaffs2.conf` (`CONFIG_MICROCS_YAFFS2=y`, downloads yaffs2 —
GPLv2 or commercial licence); `mcs_zephyr_flash_area_init()` wraps any flash area as an
`mcs_flash_t` and `mcs_flashfs_mount()` formats/mounts it (YAFFS2 needs ≥6 erase blocks:
`boards/native_sim_yaffs2.overlay` gives `native_sim` a 136 KB storage partition). Any other mounted Zephyr
filesystem works too, e.g. FAT on an SD card:

```c
static mcs_zephyr_fs_t sd;
mcs_zephyr_fs_init(&sd, "/SD:", "fat");             // after fs_mount() of the FAT volume
cfg.fs_ops = &mcs_zephyr_fs_ops; cfg.fs_ctx = &sd;  // mcs_runtime_cfg_t
```

## Devices come from the devicetree

| C# | Devicetree |
|---|---|
| `GPIO` | every `gpio0`, `gpio1`… / `gpioa`, `gpiob`… node; pin = port * 32 + pin, names `"P0.13"`, `"PA5"` |
| `UART.Open(n)` | alias `mcs-uartN` (UART 0 defaults to `zephyr,console`); interrupt-driven, polled where the driver has no IRQ API |
| `I2C` / `SPI` | aliases `mcs-i2cN` / `mcs-spiN` (fallback: `i2c0`, `spi1`…) |
| `ADC.Read(n)` | n-th entry of `io-channels` in `/zephyr,user` |
| `PWM.Set(n, ...)` | n-th entry of `pwms` in `/zephyr,user` |
| `LedStrip` | `led_strip` device at alias `led-strip` (`CONFIG_LED_STRIP=y`, `chain-length`); the pin argument is ignored |
| `DAC` | alias `mcs-dac` |
| `I2S.Open(n)` | alias `mcs-i2sN` (fallback: `i2s0`) — memory-slab streaming, `CONFIG_I2S=y` |
| `CAN.Open(n)` | alias `mcs-canN` (bus 0 falls back to chosen `zephyr,canbus`) |
| `Watchdog` | alias `watchdog0` (fallback: `wdt0`, `wdt`, `iwdg`) |
| `RTC` | alias `rtc` with `CONFIG_RTC=y`, else a software clock |
| `Hal.UniqueId` / `Hal.Reset` | `hwinfo` / `sys_reboot` |
| `Timer` | `k_timer` (4 timers) |

Ready-made overlays: [`nrf52840dk_nrf52840`](example/boards/nrf52840dk_nrf52840.overlay)
(ADC, PWM on LED1, I2S), [`rpi_pico`](example/boards/rpi_pico.overlay) (ADC incl. temperature,
PWM on the LED, 512 KB LittleFS, hardware RTC). QSPI is not exposed (Zephyr has no portable
QSPI API; use the flash API from C).

## Kconfig

| Option | Default | |
|---|---|---|
| `CONFIG_MICROCS` | n | the runtime (compiler, VM, stdlib, shell, scheduler) |
| `CONFIG_MICROCS_PORT` | y | this board port |
| `CONFIG_MICROCS_PROFILE_*` | AUTO | configuration from `CONFIG_SRAM_SIZE` / `CONFIG_FLASH_SIZE` |
| `CONFIG_MICROCS_HEAP_SIZE` | 96 / 64 / 40 KB | C# heap of the example (by RAM size) |
| `CONFIG_MICROCS_FS` | y with LittleFS | LittleFS on `storage_partition` |
| `CONFIG_MICROCS_YAFFS2` | n | YAFFS2 on `storage_partition` instead (`overlay-yaffs2.conf`) |
| `CONFIG_MICROCS_RAMFS_SIZE` | 16 KB | RAM filesystem when there is no partition |
| `CONFIG_MICROCS_CONSOLE_ECHO` | y | echo typed characters (raw terminals) |
