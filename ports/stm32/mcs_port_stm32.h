/*
 * MicroCS port for STM32 (any family with an STM32Cube HAL).
 *
 * Works inside a normal STM32CubeIDE / CubeMX / Keil / IAR / CMake / Make
 * project: CubeMX generates the peripheral handles (huart2, hi2c1, hspi1,
 * hadc1, htim3 ...) exactly as usual, you hand them to MicroCS once, and the
 * C# classes GPIO, UART, I2C, SPI, ADC, DAC, PWM, Timer, I2S, QSPI, CAN,
 * Watchdog and RTC drive them through the official HAL_xxx() functions.
 * Only peripherals whose HAL module is enabled in stm32xxxx_hal_conf.h
 * (HAL_xxx_MODULE_ENABLED) are compiled in.
 *
 *     #include "mcs_port_stm32.h"
 *     static mcs_stm32_board_t board;
 *     static mcs_hal_t hal;
 *     board.uart[2] = &huart2;              // UART.Open(2, ...) in C#
 *     board.i2c[1]  = &hi2c1;               // I2C bus 1
 *     board.spi[1]  = &hspi1;
 *     board.adc[0]  = (mcs_stm32_adc_t){ &hadc1, ADC_CHANNEL_0 };   // ADC.Read(0)
 *     board.pwm[0]  = (mcs_stm32_pwm_t){ &htim3, TIM_CHANNEL_1, 0 }; // PWM.Set(0, ...)
 *     mcs_stm32_hal_init(&hal, &board);
 *     mcs_hal_open_lib(vm, &hal);           // or cfg.hal = &hal for mcs_runtime
 *
 * Pins: C# pin numbers are port * 16 + pin, and the names work too:
 * GPIO.Pin("PA5") == 5, "PC13" == 45. Interrupts: GPIO.OnChange() uses EXTI
 * (enable the EXTI IRQs in CubeMX NVIC, or set MCS_STM32_EXTI_HANDLERS 1).
 * Family header: detected automatically (stm32f4xx_hal.h, stm32h7xx_hal.h,
 * ...) or forced with -DMCS_STM32_HAL_HEADER="\"stm32g4xx_hal.h\"".
 */
#ifndef MCS_PORT_STM32_H
#define MCS_PORT_STM32_H

#if defined(MCS_STM32_HAL_HEADER)
#include MCS_STM32_HAL_HEADER
#elif defined(__has_include)
#  if   __has_include("stm32f4xx_hal.h")
#    include "stm32f4xx_hal.h"
#  elif __has_include("stm32h7xx_hal.h")
#    include "stm32h7xx_hal.h"
#  elif __has_include("stm32f1xx_hal.h")
#    include "stm32f1xx_hal.h"
#  elif __has_include("stm32f0xx_hal.h")
#    include "stm32f0xx_hal.h"
#  elif __has_include("stm32f2xx_hal.h")
#    include "stm32f2xx_hal.h"
#  elif __has_include("stm32f3xx_hal.h")
#    include "stm32f3xx_hal.h"
#  elif __has_include("stm32f7xx_hal.h")
#    include "stm32f7xx_hal.h"
#  elif __has_include("stm32g0xx_hal.h")
#    include "stm32g0xx_hal.h"
#  elif __has_include("stm32g4xx_hal.h")
#    include "stm32g4xx_hal.h"
#  elif __has_include("stm32c0xx_hal.h")
#    include "stm32c0xx_hal.h"
#  elif __has_include("stm32l0xx_hal.h")
#    include "stm32l0xx_hal.h"
#  elif __has_include("stm32l1xx_hal.h")
#    include "stm32l1xx_hal.h"
#  elif __has_include("stm32l4xx_hal.h")
#    include "stm32l4xx_hal.h"
#  elif __has_include("stm32l5xx_hal.h")
#    include "stm32l5xx_hal.h"
#  elif __has_include("stm32u5xx_hal.h")
#    include "stm32u5xx_hal.h"
#  elif __has_include("stm32h5xx_hal.h")
#    include "stm32h5xx_hal.h"
#  elif __has_include("stm32wbxx_hal.h")
#    include "stm32wbxx_hal.h"
#  elif __has_include("stm32wlxx_hal.h")
#    include "stm32wlxx_hal.h"
#  elif __has_include("stm32u0xx_hal.h")
#    include "stm32u0xx_hal.h"
#  elif __has_include("main.h")
#    include "main.h"            /* CubeMX main.h includes the family HAL */
#  else
#    error "MicroCS STM32 port: STM32Cube HAL header not found; define MCS_STM32_HAL_HEADER"
#  endif
#else
#  include "main.h"
#endif

#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* a HAL module is used only when it is enabled AND the chip has the
 * peripheral (the HAL header then defines its constants) */
#if defined(HAL_QSPI_MODULE_ENABLED) && defined(QSPI_INSTRUCTION_1_LINE)
#define MCS_STM32_HAS_QSPI 1
#endif
#if defined(HAL_OSPI_MODULE_ENABLED) && defined(HAL_OSPI_INSTRUCTION_1_LINE) && !defined(MCS_STM32_HAS_QSPI)
#define MCS_STM32_HAS_OSPI 1
#endif
#if defined(HAL_CAN_MODULE_ENABLED) && defined(CAN_ID_STD)
#define MCS_STM32_HAS_CAN 1
#endif
#if defined(HAL_FDCAN_MODULE_ENABLED) && defined(FDCAN_STANDARD_ID) && !defined(MCS_STM32_HAS_CAN)
#define MCS_STM32_HAS_FDCAN 1
#endif
#if defined(HAL_DAC_MODULE_ENABLED) && defined(DAC_ALIGN_12B_R)
#define MCS_STM32_HAS_DAC 1
#endif
#if defined(HAL_I2S_MODULE_ENABLED) && defined(I2S_MODE_MASTER_TX)
#define MCS_STM32_HAS_I2S 1
#endif

#ifndef MCS_STM32_UARTS
#define MCS_STM32_UARTS 9          /* UART.Open(n): index = USARTn / UARTn / LPUART (your choice) */
#endif
#ifndef MCS_STM32_UART_RXBUF
#define MCS_STM32_UART_RXBUF 256   /* interrupt-driven receive ring per UART (power of two) */
#endif
#ifndef MCS_STM32_BUSES
#define MCS_STM32_BUSES 5          /* I2C / SPI / I2S / CAN / QSPI handles */
#endif
#ifndef MCS_STM32_ADC_CHANNELS
#define MCS_STM32_ADC_CHANNELS 16
#endif
#ifndef MCS_STM32_PWM_CHANNELS
#define MCS_STM32_PWM_CHANNELS 12
#endif
#ifndef MCS_STM32_TIMERS
#define MCS_STM32_TIMERS 4
#endif
#ifndef MCS_STM32_IRQ_PRIORITY
#define MCS_STM32_IRQ_PRIORITY 5   /* EXTI priority (>= configMAX_SYSCALL_INTERRUPT_PRIORITY with FreeRTOS) */
#endif
#ifndef MCS_STM32_TIMEOUT_MS
#define MCS_STM32_TIMEOUT_MS 100   /* I2C / SPI / QSPI transfer timeout */
#endif
/* Define HAL_UART_RxCpltCallback / HAL_UART_ErrorCallback / HAL_GPIO_EXTI_Callback
 * in the port. Set 0 if your project already has them and call
 * mcs_stm32_uart_rx_cplt(), mcs_stm32_uart_error() and mcs_stm32_exti() from yours. */
#ifndef MCS_STM32_DEFINE_CALLBACKS
#define MCS_STM32_DEFINE_CALLBACKS 1
#endif
/* HAL_TIM_PeriodElapsedCallback is usually generated by CubeMX (HAL time
 * base on a TIM): call mcs_stm32_tim_elapsed(htim) from it. Set 1 to let the
 * port define it instead. */
#ifndef MCS_STM32_DEFINE_TIM_CALLBACK
#define MCS_STM32_DEFINE_TIM_CALLBACK 0
#endif
/* Define the EXTIx_IRQHandler functions (when CubeMX does not generate them). */
#ifndef MCS_STM32_EXTI_HANDLERS
#define MCS_STM32_EXTI_HANDLERS 0
#endif

typedef struct { ADC_HandleTypeDef* h; uint32_t channel; } mcs_stm32_adc_t;
/* PWM output: timer + channel; clock_hz = timer input clock (0 = SystemCoreClock) */
typedef struct { TIM_HandleTypeDef* h; uint32_t channel; uint32_t clock_hz; } mcs_stm32_pwm_t;
/* Timer.Start(n): a basic/general timer with its update IRQ enabled in CubeMX */
typedef struct { TIM_HandleTypeDef* h; uint32_t clock_hz; } mcs_stm32_timer_t;

typedef struct {
    const char* name;                         /* Hal.Board, e.g. "NUCLEO-F446RE" */
    const char* led;                          /* pin behind the name "LED", e.g. "PA5"; NULL = none */
    /* handles generated by CubeMX; NULL = not available */
#ifdef HAL_UART_MODULE_ENABLED
    UART_HandleTypeDef* uart[MCS_STM32_UARTS];
#endif
#ifdef HAL_I2C_MODULE_ENABLED
    I2C_HandleTypeDef* i2c[MCS_STM32_BUSES];
#endif
#ifdef HAL_SPI_MODULE_ENABLED
    SPI_HandleTypeDef* spi[MCS_STM32_BUSES];
    uint32_t spi_clock_hz[MCS_STM32_BUSES];  /* kernel clock for SPI.Open(freq); 0 = keep CubeMX prescaler */
#endif
#ifdef HAL_ADC_MODULE_ENABLED
    mcs_stm32_adc_t adc[MCS_STM32_ADC_CHANNELS];
    uint8_t adc_bits;                         /* 0 = 12 */
    uint16_t adc_vref_mv;                     /* 0 = 3300 */
#endif
#ifdef MCS_STM32_HAS_DAC
    DAC_HandleTypeDef* dac;                   /* DAC.Write(0) = channel 1, (1) = channel 2 */
#endif
#ifdef HAL_TIM_MODULE_ENABLED
    mcs_stm32_pwm_t pwm[MCS_STM32_PWM_CHANNELS];
    mcs_stm32_timer_t timer[MCS_STM32_TIMERS];
#endif
#ifdef MCS_STM32_HAS_I2S
    I2S_HandleTypeDef* i2s[MCS_STM32_BUSES];
#endif
#ifdef MCS_STM32_HAS_QSPI
    QSPI_HandleTypeDef* qspi[MCS_STM32_BUSES];
#endif
#ifdef MCS_STM32_HAS_OSPI
    OSPI_HandleTypeDef* ospi[MCS_STM32_BUSES];
#endif
#ifdef MCS_STM32_HAS_CAN
    CAN_HandleTypeDef* can[MCS_STM32_BUSES];
#endif
#ifdef MCS_STM32_HAS_FDCAN
    FDCAN_HandleTypeDef* fdcan[MCS_STM32_BUSES];
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
    IWDG_HandleTypeDef* iwdg;                 /* NULL = the port uses its own handle */
#endif
#ifdef HAL_RTC_MODULE_ENABLED
    RTC_HandleTypeDef* rtc;
#endif
    /* ---- internal state ---- */
    struct {
        uint8_t buf[MCS_STM32_UART_RXBUF];
        volatile uint16_t head, tail;
        uint8_t byte;
        uint8_t active;
    } rx[MCS_STM32_UARTS];
    uint16_t exti_pin[16];                    /* C# pin number + 1 per EXTI line */
    uint16_t pull_up[11], pull_down[11];      /* pull set by GPIO.Mode, per port */
    uint32_t timer_count[MCS_STM32_TIMERS];
    uint8_t timer_once[MCS_STM32_TIMERS];
    uint8_t dac_started[2];
    uint8_t pwm_started[MCS_STM32_PWM_CHANNELS];
    uint8_t i2s_bits[MCS_STM32_BUSES];
} mcs_stm32_board_t;

/* Fill *hal with the functions for everything bound in *board (once). */
void mcs_stm32_hal_init(mcs_hal_t* hal, mcs_stm32_board_t* board);

#ifdef HAL_UART_MODULE_ENABLED
/* Console for the REPL / shell / mcs_runtime: interrupt-driven RX on a bound UART. */
mcs_transport_t mcs_stm32_console(mcs_stm32_board_t* board, int uart_index);
#endif
uint32_t mcs_stm32_ticks(void* ud);              /* HAL_GetTick */
void mcs_stm32_delay(void* ud, uint32_t ms);     /* HAL_Delay (or osDelay with FreeRTOS: wrap it) */

/* Call these from your own HAL callbacks when MCS_STM32_DEFINE_CALLBACKS is 0 */
#ifdef HAL_UART_MODULE_ENABLED
void mcs_stm32_uart_rx_cplt(UART_HandleTypeDef* huart);
void mcs_stm32_uart_error(UART_HandleTypeDef* huart);
#endif
void mcs_stm32_exti(uint16_t gpio_pin);
#ifdef HAL_TIM_MODULE_ENABLED
void mcs_stm32_tim_elapsed(TIM_HandleTypeDef* htim);
#endif

#ifdef __cplusplus
}
#endif
#endif
