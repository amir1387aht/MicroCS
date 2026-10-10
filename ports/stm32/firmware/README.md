# Ready-to-flash MicroCS firmware for STM32 boards

Every [release](https://github.com/amir1387aht/MicroCS/releases/latest) has a complete
firmware for these boards: flash it, open the board's serial port at **115200 baud** (or
[MicroCS Studio](https://amir1387aht.github.io/MicroCS/)) and you get the C# REPL, the shell and
the Studio upload protocol, with `/boot.cs`, `/main.cs` and your files kept in the chip's own
flash. No CubeMX project, no toolchain.

| Board | File | Console | Files (TinyFS) | VM heap |
|---|---|---|---|---|
| Nucleo-F401RE | `microcs-<v>-nucleo_f401re.bin` | ST-LINK USB (USART2) | 28 KB | 67 KB |
| Nucleo-F411RE | `microcs-<v>-nucleo_f411re.bin` | ST-LINK USB (USART2) | 28 KB | 99 KB |
| Nucleo-F446RE | `microcs-<v>-nucleo_f446re.bin` | ST-LINK USB (USART2) | 28 KB | 99 KB |
| Black Pill F411CE (WeAct) | `microcs-<v>-blackpill_f411ce.bin` | USART1: PA9 = TX, PA10 = RX (USB-serial adapter, 3.3 V) | 28 KB | 99 KB |
| Nucleo-G474RE | `microcs-<v>-nucleo_g474re.bin` | ST-LINK USB (LPUART1) | 105 KB | 99 KB |
| Nucleo-L476RG | `microcs-<v>-nucleo_l476rg.bin` | ST-LINK USB (USART2) | 105 KB | 67 KB |
| Nucleo-H743ZI / H743ZI2 | `microcs-<v>-nucleo_h743zi.bin` | ST-LINK USB (USART3) | 307 KB | 467 KB |

The F4/G4/L4 builds use the `embedded` profile (on-device compiler + REPL), the H743 build
everything (`auto` profile from the chip's memory).

## Flashing

* **Nucleo boards:** plug in the ST-LINK USB, copy the `.bin` to the `NOD_xxx` drive that
  appears; the board restarts with MicroCS. Or use STM32CubeProgrammer
  (`STM32_Programmer_CLI -c port=SWD -w microcs-<v>-<board>.hex -v -rst`) — the `.hex` holds
  only the program, so your files survive a firmware update; the `.bin` covers the file area
  too and starts with an empty filesystem.
* **Black Pill:** hold BOOT0, tap NRST, release BOOT0 (USB DFU mode), then
  `dfu-util -a 0 -s 0x08000000:leave -D microcs-<v>-blackpill_f411ce.bin` — or an ST-LINK on the
  SWD pins. Connect a USB-serial adapter to PA9/PA10 for the console.
* `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program microcs-<v>-<board>.hex verify reset exit"`
  works for every board (use the matching `target/stm32xxx.cfg`).

## What is wired up

| C# | Nucleo-64 (F401/F411/F446/G474/L476) | Nucleo-H743ZI | Black Pill F411CE |
|---|---|---|---|
| `new Pin("LED")` | PA5 (LD2) | PB0 (LD1) | PC13 (lights when low) |
| any `new Pin("PB3")` … | every GPIO, with interrupts | every GPIO | every GPIO |
| `I2C.Open(1, …)` | PB8 = SCL (D15), PB9 = SDA (D14) | PB8 / PB9 (D15 / D14) | PB6 = SCL, PB7 = SDA |
| `PWM.Set(0..3, …)`, `Servo(0..3)` | TIM3: PA6 (D12), PA7 (D11), PB0 (A3), PB1 | TIM3: PA6, PB5, PC8, PC9 | TIM3: PA6, PA7, PB0, PB1 |
| `UART.Open(n)` / console | 2 (G474: 1 = LPUART1) | 3 | 1 |
| `Watchdog`, `Hal.Board`, `Hal.Millis`, files, scheduler, ws2812 (bit-banged on any pin) | ✓ | ✓ | ✓ |

The four PWM pins start as timer outputs (low); `new Pin(...)` on one of them turns it back
into a GPIO. SPI, ADC, DAC, CAN and the other peripherals need their pins and clocks chosen
for your hardware — use the [CubeMX route](../README.md) (`example_main.c`) for those.

Clocks run from the internal HSI oscillator + PLL (no crystal needed): F401 84 MHz, F411 100 MHz,
F446 168 MHz, G474 170 MHz, L476 80 MHz, H743 400 MHz.

## Flash layout

| Family | Program | Files |
|---|---|---|
| F4 (512 KB) | sector 0 (vector table) + sectors 4–7 | sectors 1–3 (3 × 16 KB, next to the vector table — the big 128 KB sectors are needed for the program) |
| G474 / L476 | from 0x08000000 | top 128 KB (32 blocks of 4 KB = 2 pages, in bank 2 of these dual-bank parts: no CPU stall while erasing) |
| H743 | from 0x08000000 | top 512 KB (4 × 128 KB sectors, bank 2) |

A write that erases a sector stalls the CPU on single-bank F4 parts (≈ 0.3 s for a 16 KB
sector); uploads from Studio and the shell retry, so this is harmless.

## Building it yourself

```sh
sh tools/build_stm32_firmware.sh list                 # the boards
sh tools/build_stm32_firmware.sh nucleo_f446re        # -> build/fw/stm32/microcs-nucleo_f446re.{elf,bin,hex}
```

Needs `arm-none-eabi-gcc` (Arm GNU Toolchain or `apt install gcc-arm-none-eabi libnewlib-arm-none-eabi`);
the script downloads the STM32Cube HAL + CMSIS of the family once (into `build/sdk`, or set
`STM32_SDK`). Board setup: [`board.c`](board.c); memory map: [`stm32.ld`](stm32.ld).
To add a board, add a line to the table in the script and a block in `board.c` (console
UART + pins, LED, clock).

CI builds all boards on every push and boots the F4 and H743 images in the
[Renode](https://renode.io) emulator (`tools/renode_check.py`): REPL prompt, an expression,
`Hal.Board`, a file written to and read back from the emulated internal flash, the LED pin.
The G474 and L476 images are built and linked but Renode has no model for those chips.
