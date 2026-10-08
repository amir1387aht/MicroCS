# MicroCS for Raspberry Pi RP2040 / RP2350 (pico-sdk)

Raspberry Pi Pico, Pico W, Pico 2 (Arm or RISC-V cores) and any RP2040/RP2350 board.

## Quick start

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S ports/rp2/example -B build/pico -DPICO_BOARD=pico      # or pico2, pico_w
cmake --build build/pico
# copy build/pico/microcs_pico.uf2 to the board (hold BOOTSEL while plugging in)
```

Open the USB serial port (any baud rate) and type C# at the `> ` prompt. The whole firmware
is [`example/main.c`](example/main.c), ~30 lines.

## Pins

```c
mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;     // Pico defaults:
// UART0 GP0/GP1, UART1 GP4/GP5, I2C0 GP8/GP9, I2C1 GP6/GP7,
// SPI0 GP18/19/16 (SCK/MOSI/MISO), SPI1 GP10/11/12
pins.i2c[0] = (mcs_rp2_i2c_pins_t){ 4, 5 };   // change any bus
mcs_rp2_hal_init(&hal, &pins);
```

| C# | RP2 |
|---|---|
| pins | GPIO numbers; `"GP15"`, `"GPIO15"`, `"LED"` (on-board LED where defined) |
| `GPIO.OnChange` | GPIO IRQ callback |
| `UART` | interrupt-driven receive ring |
| `PWM.Set(gpio, ...)` | every GPIO can do PWM (channel = GPIO number) |
| `ADC.Read(0..3)` | GP26–GP29; `ADC.Read(4)` = internal temperature sensor |
| `Timer` | hardware alarm pool |
| `Watchdog`, `RTC`, `Hal.UniqueId` | hardware watchdog, software clock on the 64-bit µs timer (set it with `RTC.Set`), flash unique id |

Not available on this chip: DAC, CAN, QSPI for user devices; I²S needs PIO (planned).

## Files on flash

The example keeps `/boot.cs`, `/main.cs`, uploaded scripts and every file a script writes in
**LittleFS** on the top of the board's QSPI flash, so they survive resets:

```c
static mcs_rp2_flash_t flash;
static mcs_flashfs_t fs;
if (mcs_rp2_flash_init(&flash, 0, 0) == 0 &&                        // last MCS_RP2_FS_SIZE bytes
    mcs_flashfs_mount(&fs, &flash.flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
    cfg.fs_ops = fs.ops; cfg.fs_ctx = fs.ctx;
}
```

* Region: `MCS_RP2_FS_SIZE` bytes at the end of flash — 1 MB on 2 MB boards (Pico), all but
  the first 1 MB on 4 MB+ boards (Pico 2: 3 MB). Override with `-DMCS_RP2_FS_SIZE=...` or pass
  an offset/size; the driver refuses a region that overlaps the firmware.
* Erase/program go through `flash_safe_execute`, which pauses interrupts and the other core.
* **YAFFS2 instead**: `cmake ... -DMICROCS_FS=yaffs2` (downloads yaffs2; GPLv2 or commercial
  licence — linking it puts your firmware under those terms). `-DMICROCS_FS=` (empty) builds
  without flash files (RAM disk only).

## Checked in CI

The example firmware is built for `pico` (RP2040, LittleFS) and `pico2` (RP2350, LittleFS and
YAFFS2).
