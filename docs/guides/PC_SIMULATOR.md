# Try MicroCS on the PC

No board needed: the `mcs` program on the PC is the same compiler and VM that runs on the
chips, plus a **simulated board** with GPIO, UART, I²C sensors, SPI, ADC, PWM, timers, QSPI
flash, CAN and an optional OLED. Everything you try here runs unchanged on the hardware.

## 1. Get `mcs`

**Linux x64:** download `mcs-<version>-linux-x64.tar.gz` from the
[latest release](https://github.com/amir1387aht/MicroCS/releases/latest) and unpack it.

**Any OS with a C compiler** (Linux, macOS, Windows with MSYS2 / WSL):

```sh
git clone https://github.com/amir1387aht/MicroCS
cd MicroCS
make            # -> ./mcs      (or: cmake -S . -B build && cmake --build build)
```

## 2. Run C#

```sh
./mcs --repl               # interactive C# prompt, like on a board; .help lists commands
./mcs examples/tour.cs     # run a file
./mcs -e 'Console.WriteLine(Math.Sqrt(2));'
```

## 3. Talk to (simulated) hardware

`--sim` attaches the simulated board:

```sh
./mcs --sim examples/hardware/01_blink.cs
./mcs --sim examples/hardware/05_i2c_temperature.cs     # a TMP102-like sensor at 0x48
./mcs --sim -e 'Console.WriteLine(string.Join(",", I2C.Scan(0)));'   # 72,80,104
```

What is on the simulated board: [Hardware API → simulator](../HAL.md#simulator-board).

## 4. A display

```sh
make fetch-u8g2 mcs-u8g2                        # the CLI with the optional U8g2 driver
./build/mcs_u8g2 --oled examples/u8g2/hello.cs  # the OLED is drawn in the terminal
```

## 5. Files and precompiled images

```sh
./mcs --fs myfiles app.cs          # File.* works inside ./myfiles (default: the current folder)
./mcs -c app.cs -o app.mcsb        # compile once; devices can run app.mcsb without the compiler
./mcs -C app.cs -n app -o app.h    # the same image as a C array to build into firmware
./mcs app.mcsb
```

## 6. Next

* Put it on a board: [guides](README.md).
* Learn the hardware classes: [HAL.md](../HAL.md).
* The browser IDE also works with the PC build: [MicroCS Studio](../../tools/studio/README.md).

## Troubleshooting

* `make: cc: not found` — install a C compiler (`sudo apt install build-essential`,
  Xcode command line tools, or MSYS2's `mingw-w64-x86_64-gcc`).
* `I2C` / `GPIO` "does not exist" — add `--sim`; without it the PC has no hardware.
