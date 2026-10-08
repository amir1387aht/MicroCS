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

## Files on flash

The free top of the chip's own flash holds `/boot.cs`, `/main.cs`, uploads and script files
(LittleFS by default, YAFFS2 optional) — see [`example_main.c`](example_main.c):

```c
static mcs_stm32_flash_t flash;
static mcs_flashfs_t fs;
if (mcs_stm32_flash_init(&flash, 0, 0) == 0 &&                      // top quarter of the flash
    mcs_flashfs_mount(&fs, &flash.flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
    cfg.fs_ops = fs.ops; cfg.fs_ctx = fs.ctx;
}
```

* Every family: page flash (F0/F1/F3/G0/G4/L0/L1/L4/L5/U0/U5/C0/WB/WL, 2–8 KB pages), sector
  flash (F2/F4/F7: the region must lie in the uniform 128/256 KB top sectors; H5 8 KB, H7 128 KB).
  The program unit (8/16/32-byte flash words with ECC) is honoured; L0/L1 erase to 0x00 and the
  driver stores inverted bytes. On STM32WB the region stays below the wireless stack.
* Pass an address and size to pick the region yourself (e.g. reserve it in the linker script);
  regions overlapping the firmware are refused.
* The CPU stalls while its own flash bank erases (up to 1–2 s for a 128 KB F4 sector); uploads
  use the retrying Studio/shell protocol, so this is harmless.
* CMake (`MICROCS_PORT=stm32`): `-DMICROCS_FS=littlefs` or `-DMICROCS_FS=yaffs2` downloads and
  adds the sources (without it the example uses a RAM disk). CubeIDE/Makefile projects add `lfs.c lfs_util.c` and `-DMCS_ENABLE_LFS=1 -DMCS_ENABLE_FLASH=1`
  (see [FILESYSTEM.md](../../docs/FILESYSTEM.md#files-on-the-chips-own-flash-every-port)).

## Memory

Heap guidance: 48–64 KB for the REPL with a small RAM disk on F4/G4/L4 (128 KB SRAM),
160 KB+ on H7/H5/U5. Without the on-device compiler (`MCS_ENABLE_COMPILER=0`, run images
precompiled with `mcs -C`) 12–32 KB is enough for many applications; see
[LOW_RESOURCE.md](../../docs/LOW_RESOURCE.md).

### Configuration per part (auto profile)
With CMake (`MICROCS_PORT=stm32`) the `auto` profile is the default: the CMSIS device macro
of your CubeMX project (`STM32F072xB`, `STM32G474xx`, …) selects the RAM/flash class from
[`include/profiles/mcs_target_stm32.h`](../../include/profiles/mcs_target_stm32.h), and that
picks the profile — `min` below 32 KB of RAM or 128 KB of flash (F070/F072/F103/G070/L073…),
`tiny` on other 128 KB-flash parts, `lowram`, `mcu` or `embedded` as RAM grows, everything
on H5/H7/U5/F7. The HAL classes stay enabled
whenever the part has 128 KB of flash or more. In CubeIDE/Makefile projects add
`-DMCS_USER_CONFIG_FILE="profiles/mcs_profile_auto.h" -DMCS_PORT_HAL=1` to get the same.
Where one macro covers several sizes (`STM32F103xB` = F103x8 and F103xB) the smaller one is
assumed; define `MCS_TARGET_RAM_KB` / `MCS_TARGET_FLASH_KB` to override.

### Supported parts
Every STM32 with **at least 16 KB of SRAM and 64 KB of flash**. These lines have less and
stop the build with an `#error` (define `MCS_ALLOW_SMALL_TARGET=1` to try anyway):

| Family | Not supported |
|---|---|
| C0 | C011, C031, C051 |
| F0 | F030x4/x6/x8, F031, F038, F042, F048, F051, F058, F070x6 |
| F1 | F100 (except F100xE), F101x4–xB, F102, F103x4/x6 |
| F3 | F301x6, F302x6, F303x6/x8, F328, F334 |
| G0 | G030, G031, G041 (the 32 KB-flash G050x6/G051x6/G061x6 and G431x6 share a macro with their 64 KB versions and are not detected — do not use them) |
| L0 | L010x4–x8, L011, L021, L031, L041, L051, L052, L053, L062, L063 |
| L1 | L100x6/x8/xB, L151x6/x8, L152x6/x8 |
| U0 | U031 |

## Checked in CI

`tools/check_ports.sh stm32 <family> <device> <cpu>` compiles the port with `-Werror`
against the official STM32Cube HAL headers of the family, once with the default
configuration and once with the auto profile (`PORT_CFLAGS`), then with the flash driver +
LittleFS and + YAFFS2. CI covers F0, F1, F4, F7, G0,
G4, H5, H7, L0, L4, U5 and WB.
