# Zephyr

One port for every board Zephyr supports — nRF52/53/54, NXP, STM32, RP2040, ESP32, SAM,
Silicon Labs… Devices come from the devicetree, so a new board usually needs only a small
overlay.

## 1. Workspace

Install the [Zephyr SDK and west](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
(Python venv + `pip install west`), then create a workspace with MicroCS as the manifest. It
fetches Zephyr 4.1 and the HALs for Nordic, RP2040 and STM32 plus LittleFS:

```sh
west init -m https://github.com/amir1387aht/MicroCS --mf ports/zephyr/west.yml microcs-ws
cd microcs-ws && west update
```

Already have a workspace? Add MicroCS as a project in your `west.yml`, or pass
`-DZEPHYR_EXTRA_MODULES=/path/to/MicroCS` to `west build`.

## 2. Build and flash

```sh
west build -b nrf52840dk/nrf52840 microcs/ports/zephyr/example
west flash
```

Other boards: `rpi_pico`, `nucleo_f429zi`, `native_sim/native/64` (runs on the PC:
`./build/zephyr/zephyr.exe`). Console on USB instead of a UART:

```sh
west build -b nrf52840dk/nrf52840 microcs/ports/zephyr/example -- \
    -DEXTRA_CONF_FILE=overlay-usb.conf -DEXTRA_DTC_OVERLAY_FILE=usb.overlay
```

## 3. Talk to it

Open the board's serial port at 115200 baud (nRF52840 DK: the J-Link VCOM) or
[MicroCS Studio](https://amir1387aht.github.io/MicroCS/), press Enter:

```text
> var led = new Pin("P0.14", GPIO.Output);    // LED2 on the nRF52840 DK (LED1 is the PWM demo)
> led.Toggle();
```

## 4. Give C# your devices

Buses are devicetree aliases. In `microcs/ports/zephyr/example/boards/<board>.overlay`
(or your app's overlay):

```dts
/ {
    aliases {
        mcs-i2c0 = &i2c0;          /* I2C.Open(0) */
        mcs-spi0 = &spi1;          /* SPI bus 0 */
        mcs-uart1 = &uart1;        /* UART.Open(1, ...) */
    };
    zephyr,user {
        io-channels = <&adc 0>;    /* ADC.Read(0) */
        pwms = <&pwm0 0 PWM_MSEC(20) PWM_POLARITY_NORMAL>;   /* PWM.Set(0, ...) */
    };
};
```

Pins, pull-ups and speeds of the buses are set in the devicetree too (`pinctrl`,
`clock-frequency`). The full table: [ports/zephyr/README.md](../../ports/zephyr/README.md#devices-come-from-the-devicetree).

## 5. Options

`prj.conf` or `-DEXTRA_CONF_FILE=...`: `CONFIG_MICROCS_HEAP_SIZE`, `CONFIG_MICROCS_THREADS=y`
(`overlay-threads.conf`), TinyFS (`overlay-tinyfs.conf`), YAFFS2, U8g2 displays
(`CONFIG_MICROCS_U8G2=y`, [U8G2.md](../U8G2.md)) — see the [Kconfig table](../../ports/zephyr/README.md#kconfig).

## Your program on the board

```sh
python3 microcs/tools/mcs_remote.py --port /dev/ttyACM0 put blink.cs /main.cs + run /main.cs
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `I2C.Scan: not supported` (or `I2C.Write: …`) | no `mcs-i2c0` alias and no `i2c0` node enabled — add the alias / `status = "okay"` |
| files are lost on reset | the board has no `storage_partition`: add one in the overlay (see `boards/rpi_pico.overlay`) |
| `west: unknown command "build"` | run it inside the workspace (`microcs-ws`) with the venv active |

More: [ports/zephyr/README.md](../../ports/zephyr/README.md).
