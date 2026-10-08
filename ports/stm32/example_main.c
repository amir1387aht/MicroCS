/*
 * MicroCS on an STM32 Nucleo board (CubeMX project) - the C# REPL on the
 * ST-LINK virtual COM port plus every peripheral CubeMX configured.
 *
 * In CubeMX: enable USART2 (115200 8N1) with its global interrupt, plus any
 * of I2C1, SPI1, ADC1, TIM3 (PWM CH1), TIM6 (update IRQ), DAC, IWDG, RTC.
 * Then call microcs_main() from main() after the MX_xxx_Init() calls
 * (USER CODE BEGIN 2). That is all.
 *
 * Files: with LittleFS or YAFFS2 compiled in (CubeMX CMake project:
 * set(MICROCS_FS littlefs) before add_subdirectory(MicroCS); other IDEs: add
 * lfs.c + lfs_util.c and -DMCS_ENABLE_LFS=1) scripts live in the top part of
 * the chip's own flash (mcs_stm32_flash_init) and survive resets; without
 * one, a RAM disk.
 */
#include "mcs_runtime.h"
#include "mcs_port_stm32.h"

extern UART_HandleTypeDef huart2;
#ifdef HAL_I2C_MODULE_ENABLED
extern I2C_HandleTypeDef hi2c1;
#endif
#ifdef HAL_SPI_MODULE_ENABLED
extern SPI_HandleTypeDef hspi1;
#endif
#ifdef HAL_ADC_MODULE_ENABLED
extern ADC_HandleTypeDef hadc1;
#endif
#ifdef HAL_TIM_MODULE_ENABLED
extern TIM_HandleTypeDef htim3, htim6;
#endif

#ifndef MICROCS_HEAP
#define MICROCS_HEAP (64 * 1024)      /* VM heap + RAM disk; F446 has 128 KB SRAM */
#endif
static uint8_t heap[MICROCS_HEAP] __attribute__((aligned(8)));
static mcs_stm32_board_t board;
static mcs_hal_t hal;
static mcs_runtime_t rt;
#if MCS_ENABLE_FLASH && (MCS_ENABLE_LFS || MCS_ENABLE_YAFFS)
static mcs_stm32_flash_t flash;
static mcs_flashfs_t flashfs;
#endif

void microcs_main(void) {
    board.name = "NUCLEO-F446RE";
    board.led = "PA5";                                           /* LD2: new Pin("LED") in C# */
    board.uart[2] = &huart2;                                     /* UART.Open(2, ...) and the console */
#ifdef HAL_I2C_MODULE_ENABLED
    board.i2c[1] = &hi2c1;                                       /* I2C bus 1 */
#endif
#ifdef HAL_SPI_MODULE_ENABLED
    board.spi[1] = &hspi1;
#ifdef RCC_CFGR_PPRE2
    board.spi_clock_hz[1] = HAL_RCC_GetPCLK2Freq();              /* SPI1 sits on APB2: lets SPI.Open pick the prescaler */
#else
    board.spi_clock_hz[1] = HAL_RCC_GetPCLK1Freq();
#endif
#endif
#ifdef HAL_ADC_MODULE_ENABLED
    board.adc[0] = (mcs_stm32_adc_t){ &hadc1, ADC_CHANNEL_1 };   /* ADC.Read(0) = ADC1_IN1 (PA1 on F4) */
    board.adc[1] = (mcs_stm32_adc_t){ &hadc1, ADC_CHANNEL_2 };   /* ADC.Read(1) = ADC1_IN2 */
#endif
#ifdef HAL_TIM_MODULE_ENABLED
    board.pwm[0] = (mcs_stm32_pwm_t){ &htim3, TIM_CHANNEL_1, 0 }; /* PWM.Set(0, ...) = PA6 */
    board.timer[0] = (mcs_stm32_timer_t){ &htim6, 0 };           /* Timer.Start(0, ...) */
#endif
    mcs_stm32_hal_init(&hal, &board);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = sizeof heap;
#if MCS_ENABLE_FLASH && (MCS_ENABLE_LFS || MCS_ENABLE_YAFFS)
    if (mcs_stm32_flash_init(&flash, 0, 0) == 0 &&
        mcs_flashfs_mount(&flashfs, &flash.flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
        cfg.fs_ops = flashfs.ops;                                /* files in the chip's flash */
        cfg.fs_ctx = flashfs.ctx;
    } else
#endif
    {
        cfg.ramfs_size = 8 * 1024;                               /* RAM disk, lost on reset */
    }
    cfg.console = mcs_stm32_console(&board, 2);
    cfg.ticks = mcs_stm32_ticks;
    cfg.delay = mcs_stm32_delay;
    cfg.hal = &hal;
    for (;;) mcs_runtime_run(&rt, &cfg);
}
