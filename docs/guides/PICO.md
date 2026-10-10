# Raspberry Pi Pico

Raspberry Pi Pico, Pico W, Pico 2 (RP2350, Arm or RISC-V), Waveshare RP2040-Zero and every
other RP2040 / RP2350 board.

## Path A — drag and drop the release firmware

1. Download from the [latest release](https://github.com/amir1387aht/MicroCS/releases/latest):
   `microcs-<version>-pico.uf2` (Pico, Pico W, most RP2040 boards),
   `microcs-<version>-pico2.uf2` (Pico 2) or `microcs-<version>-waveshare_rp2040_zero.uf2`.
2. Hold **BOOTSEL**, plug in the USB cable, release: a drive called `RPI-RP2` (`RP2350`)
   appears. Copy the `.uf2` onto it; the board restarts by itself.
3. Open the new USB serial port (any baud rate; Windows: `COMx` in the Device Manager, Linux
   `/dev/ttyACM0`, macOS `/dev/cu.usbmodem…`) with a terminal or
   [MicroCS Studio](https://amir1387aht.github.io/MicroCS/) (Chrome/Edge → **Connect**), press Enter:

   ```text
   > var led = new Pin("LED", GPIO.Output);    // on-board LED (GP25 on the Pico)
   > led.Toggle();
   ```

The release firmware runs C# threads on both cores (FreeRTOS SMP) and keeps your files in
LittleFS on the flash (1 MB on the Pico, 3 MB on the Pico 2).

## Path B — build it yourself (pico-sdk)

### 1. Tools

* the [pico-sdk](https://github.com/raspberrypi/pico-sdk) 1.5 or 2.x
  (`git clone -b 2.1.1 https://github.com/raspberrypi/pico-sdk && cd pico-sdk && git submodule update --init`),
* the Arm GCC toolchain (`arm-none-eabi-gcc`), CMake 3.13+, Python 3, Ninja or make
  (Windows: the *Raspberry Pi Pico Windows installer* / VS Code *Raspberry Pi Pico* extension
  installs all of them).

### 2. Build

```sh
git clone https://github.com/amir1387aht/MicroCS && cd MicroCS
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S ports/rp2/example -B build/pico -DPICO_BOARD=pico -DMICROCS_FS_DOWNLOAD=ON
cmake --build build/pico -j
```

`PICO_BOARD=pico2` for the Pico 2, `pico_w` for the Pico W. `MICROCS_FS_DOWNLOAD=ON` fetches
LittleFS v2.9.3 once; `-DMICROCS_FS=tinyfs` uses the built-in TinyFS instead (nothing to
fetch). Copy `build/pico/microcs_pico.uf2` to the board as in path A.

Useful options on the same `cmake` line:

| Option | |
|---|---|
| `-DMICROCS_OS=freertos -DFREERTOS_KERNEL_PATH=...` | threads on both cores ([ports/rp2](../../ports/rp2/README.md#both-cores-freertos-smp)) |
| `-DMICROCS_U8G2=ON -DMICROCS_U8G2_DOWNLOAD=ON` | U8g2 displays ([U8G2.md](../U8G2.md)) |
| `-DMICROCS_WS2812=OFF` | no NeoPixel driver |
| `-DMICROCS_PROFILE=embedded` | smaller build profile ([CONFIGURATION.md](../CONFIGURATION.md)) |

### 3. Pins

The buses use the Pico pin-out by default — UART0 GP0/GP1, UART1 GP4/GP5, **I2C0 GP8/GP9
(SDA/SCL)**, I2C1 GP6/GP7, SPI0 GP18/GP19/GP16 (SCK/MOSI/MISO), SPI1 GP10/GP11/GP12. Other
pins: edit `ports/rp2/example/main.c` before `mcs_rp2_hal_init`:

```c
mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;
pins.i2c[0] = (mcs_rp2_i2c_pins_t){ 4, 5 };     // SDA, SCL
```

Every GPIO can do PWM (`PWM.Set(15, 1000, 0.5)` = GP15), `ADC.Read(0..3)` reads GP26–GP29
and `ADC.Read(4)` the temperature sensor.

## Your program on the board

```sh
python3 tools/mcs_remote.py --port /dev/ttyACM0 put blink.cs /main.cs + run /main.cs
```

`/main.cs` then runs at every start. With Studio: **Connect** → write → **Save & Run**.

## Troubleshooting

| Symptom | Fix |
|---|---|
| no `RPI-RP2` drive | hold BOOTSEL *while* plugging in; try another (data) cable |
| no serial port after copying | wait 2 s; the port appears only once the firmware runs |
| CMake: `LittleFS sources not found` | add `-DMICROCS_FS_DOWNLOAD=ON` or `-DMICROCS_FS=tinyfs` |
| `PICO_SDK_PATH` errors | export it, or pass `-DPICO_SDK_PATH=...` |

More: [ports/rp2/README.md](../../ports/rp2/README.md) (pins, files, both cores).
