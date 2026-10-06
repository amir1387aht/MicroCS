# MicroCS for STM32 (STM32Cube HAL)

Works with **every STM32 family that has a Cube HAL**: C0, F0, F1, F2, F3, F4, F7, G0, G4,
H5, H7, L0, L1, L4, L5, U5, WB, WL. The port uses the handles CubeMX generates (`huart2`,
`hi2c1`, `hspi1`, `hadc1`, `htim3`…) and the official `HAL_xxx()` functions, so your clock
tree, pin muxing and DMA settings stay exactly as configured in CubeMX.

## Quick start (CubeIDE / CubeMX project)

1. In CubeMX enable what you want to use from C#: a USART with its global interrupt (the
   console), and any of I²C, SPI, ADC, TIM (PWM channels / an update interrupt for `Timer`),
   DAC, I²S, QUADSPI/OCTOSPI, CAN/FDCAN, IWDG, RTC. Enable the EXTI interrupts for pins you
   want `GPIO.OnChange` on.
2. Add MicroCS to the project: source folders `src`, `modules`, `ports/stm32`; include paths
   `include`, `ports/stm32`. (CMake projects: `set(MICROCS_PORT stm32)` +
   `add_subdirectory(MicroCS)` + `target_link_libraries(${CMAKE_PROJECT_NAME} microcs)`.
   Makefile projects: `include MicroCS/microcs.mk`.)
3. Copy [`example_main.c`](example_main.c) into `Core/Src/`, adjust the handle names and call
   `microcs_main()` in `USER CODE BEGIN 2`.
4. Flash, open the ST-LINK virtual COM port at 115200 and type C#.

## Binding peripherals

```c
static mcs_stm32_board_t board;
static mcs_hal_t hal;

board.name    = "NUCLEO-F446RE";
board.uart[2] = &huart2;                                       // UART.Open(2, ...) + console
board.i2c[1]  = &hi2c1;                                        // I2C bus 1
board.spi[1]  = &hspi1;  board.spi_clock_hz[1] = HAL_RCC_GetPCLK2Freq();   // lets SPI.Open pick the prescaler
board.adc[0]  = (mcs_stm32_adc_t){ &hadc1, ADC_CHANNEL_1 };    // ADC.Read(0)
board.pwm[0]  = (mcs_stm32_pwm_t){ &htim3, TIM_CHANNEL_1, 0 }; // PWM.Set(0, ...)
board.timer[0] = (mcs_stm32_timer_t){ &htim6, 0 };             // Timer.Start(0, ...)
board.dac     = &hdac;      board.i2s[0] = &hi2s2;     board.qspi[0] = &hqspi;
board.can[0]  = &hcan1;     /* or */ board.fdcan[0] = &hfdcan1;
board.rtc     = &hrtc;      board.iwdg = &hiwdg;
mcs_stm32_hal_init(&hal, &board);
```

Only peripherals whose `HAL_xxx_MODULE_ENABLED` is set in `stm32xxxx_hal_conf.h` are compiled.

| C# | STM32 |
|---|---|
| pin numbers | `port * 16 + pin`; names `"PA5"`, `"PC13"` work everywhere |
| `GPIO.OnChange` | EXTI line of the pin (one pin per line, as in hardware) |
| `UART.Read` / `OnReceive` | interrupt-driven ring buffer (`MCS_STM32_UART_RXBUF`) |
| `I2C.Scan` | `HAL_I2C_IsDeviceReady` |
| `SPI.Open(bus, hz, mode)` | re-programs prescaler / CPOL / CPHA when `spi_clock_hz` is given |
| `PWM.Set` | timer ARR/CCR computed from `clock_hz` (0 = from the APB clock) |
| `Timer.Start` | timer update interrupt |
| `QSPI` | `HAL_QSPI_Command` / `HAL_OSPI_Command` |
| `CAN` | bxCAN or FDCAN (classic frames), accept-all filter |
| `Hal.Micros` | DWT cycle counter (Cortex-M3+) or SysTick |
| `Hal.UniqueId` | 96-bit UID |

## Interrupt callbacks

By default (`MCS_STM32_DEFINE_CALLBACKS=1`) the port defines `HAL_GPIO_EXTI_Callback`,
`HAL_UART_RxCpltCallback` and `HAL_UART_ErrorCallback`. If your code already defines them,
set it to 0 and call `mcs_stm32_exti(pin)`, `mcs_stm32_uart_rx_cplt(huart)` and
`mcs_stm32_uart_error(huart)` from yours. `HAL_TIM_PeriodElapsedCallback` usually exists
already (HAL tick on a TIM) — call `mcs_stm32_tim_elapsed(htim)` from it, or set
`MCS_STM32_DEFINE_TIM_CALLBACK=1`. `MCS_STM32_EXTI_HANDLERS=1` defines the `EXTIx_IRQHandler`s
when CubeMX does not.

## Memory

Heap guidance: 48–64 KB for the REPL with a small RAM disk on F4/G4/L4 (128 KB SRAM),
160 KB+ on H7/H5/U5. Without the on-device compiler (`MCS_ENABLE_COMPILER=0`, run images
precompiled with `mcs -C`) 32 KB is enough for many applications; see
[LOW_RESOURCE.md](../../docs/LOW_RESOURCE.md).

## Checked in CI

`tools/check_ports.sh stm32 <family> <device> <cpu>` compiles the port with `-Werror`
against the official STM32Cube HAL headers of the family. CI covers F0, F1, F4, F7, G0, G4,
H5, H7, L0, L4, U5 and WB.
