# STM32

Every STM32 family with a Cube HAL (C0, F0–F7, G0, G4, H5, H7, L0–L5, U5, WB, WL). Path A
flashes a ready firmware on popular boards; path B puts MicroCS into your own STM32CubeMX /
CubeIDE project so every peripheral you configured there is usable from C#.

## Path A — ready firmware (Nucleo, Black Pill)

1. Download the file for your board from the
   [latest release](https://github.com/amir1387aht/MicroCS/releases/latest):
   `nucleo_f401re`, `nucleo_f411re`, `nucleo_f446re`, `nucleo_g474re`, `nucleo_l476rg`,
   `nucleo_h743zi` or `blackpill_f411ce` (`.bin`).
2. **Nucleo:** plug in the ST-LINK USB and copy the `.bin` onto the `NOD_…` drive. The board
   restarts with MicroCS.
   **Black Pill:** hold BOOT0, tap NRST, release BOOT0, then
   `dfu-util -a 0 -s 0x08000000:leave -D microcs-<version>-blackpill_f411ce.bin`; wire a 3.3 V
   USB-serial adapter to PA9 (TX) / PA10 (RX) for the console.
3. Open the ST-LINK virtual COM port (or the adapter) at **115200** baud, or
   [MicroCS Studio](https://amir1387aht.github.io/MicroCS/) → **Connect**, and press Enter:

   ```text
   > var led = new Pin("LED", GPIO.Output);
   > led.Toggle();
   > Console.WriteLine(string.Join(",", I2C.Scan(1)));   // Nucleo: SCL = D15 (PB8), SDA = D14 (PB9)
   ```

Which pins and peripherals the ready firmware wires up: [firmware/README.md](../../ports/stm32/firmware/README.md).

## Path B — MicroCS in your CubeMX project

### 1. Configure the chip in CubeMX / CubeIDE

* a **USART** for the console (115200 8N1) **with its global interrupt** — on Nucleo-64
  boards USART2 (PA2/PA3) is the ST-LINK virtual COM port;
* whatever C# should use: I²C, SPI, ADC, TIM channels in PWM mode, a basic TIM with its
  update interrupt (for `Timer`), DAC, I²S, QUADSPI/OCTOSPI, CAN/FDCAN, IWDG, RTC;
* EXTI interrupts for the pins you want `GPIO.OnChange` on;
* **heap/stack**: MicroCS gets its heap as a static array (64 KB in the example); give the
  main stack at least 8 KB (*Project Manager → Linker settings → Minimum stack size*).

Generate the code.

### 2. Add MicroCS

Copy (or `git submodule add`) MicroCS into the project, e.g. `Middlewares/MicroCS`, then

* **CubeIDE (Eclipse):** right-click the project → *Properties → C/C++ General → Paths and
  Symbols*: add the source folders `src`, `modules`, `ports/stm32` and the include paths
  `include`, `ports/stm32`. Add the symbol `MCS_ENABLE_TINYFS=1` to keep files in the flash.
* **CMake (CubeMX 6.11+ / VS Code):** in `CMakeLists.txt`
  ```cmake
  set(MICROCS_PORT stm32)
  set(MICROCS_FS tinyfs)                 # files in the chip's flash, nothing to download
  add_subdirectory(Middlewares/MicroCS)
  target_link_libraries(${CMAKE_PROJECT_NAME} microcs)
  ```
* **Makefile projects:** `include Middlewares/MicroCS/microcs.mk`.

### 3. Start it

Copy [`example_main.c`](../../ports/stm32/example_main.c) to `Core/Src/microcs_main.c`, change
the handle names (`huart2`, `hi2c1`, `hspi1`, `hadc1`, `htim3`, …) and `board.name`, and call
it from `main.c`:

```c
/* USER CODE BEGIN 2 */
extern void microcs_main(void);
microcs_main();          /* never returns */
/* USER CODE END 2 */
```

The bus numbers in C# are the peripheral numbers: `board.i2c[1] = &hi2c1` → `I2C.Open(1)`,
`board.uart[2] = &huart2` → `UART.Open(2, …)`, `board.pwm[0] = { &htim3, TIM_CHANNEL_1 }` →
`PWM.Set(0, …)`. Full list: [ports/stm32/README.md](../../ports/stm32/README.md#binding-peripherals).

### 4. Flash and talk

Build, flash (Run/Debug in CubeIDE, or copy the `.bin` to the ST-LINK drive), open the
virtual COM port at 115200 and press Enter.

## Your program on the board

```sh
python3 tools/mcs_remote.py --port COM7 put blink.cs /main.cs + run /main.cs
```

or MicroCS Studio → **Save & Run**. `/main.cs` runs at every start.

## Troubleshooting

| Symptom | Fix |
|---|---|
| no prompt | the console USART needs its **global interrupt** enabled in CubeMX (NVIC) |
| `multiple definition of HAL_GPIO_EXTI_Callback` | your code defines it too: set `MCS_STM32_DEFINE_CALLBACKS=0` and call `mcs_stm32_exti(pin)` from yours ([README](../../ports/stm32/README.md#interrupt-callbacks)) |
| `region RAM overflowed` | make `MICROCS_HEAP` smaller, or use a smaller profile (`-DMCS_DEFAULT_PROFILE=MCS_PROFILE_EMBEDDED`) |
| HardFault at start | main stack too small — 8 KB or more |
| `SPI.Open` ignores the frequency | set `board.spi_clock_hz[n]` to the bus clock (APB1/APB2) |

More: [ports/stm32/README.md](../../ports/stm32/README.md) (every peripheral, interrupts, flash files, memory per part).
