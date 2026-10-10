/*
 * MicroCS ready-to-flash firmware for popular STM32 boards (released as
 * microcs-<version>-<board>.bin/.hex; built by tools/build_stm32_firmware.sh).
 *
 * No CubeMX project needed: this file sets up the clock (internal HSI
 * oscillator + PLL, so it works with or without a crystal), the console UART
 * on the board's USB virtual COM port (ST-LINK) or USART1 pins, I2C1, four PWM
 * outputs on TIM3, the LED, and TinyFS in the top of the chip's own flash.
 * Then it runs the C# REPL / shell (MicroCS Studio connects at 115200 baud).
 *
 * One board is selected with -DMCS_FW_<BOARD>; see ports/stm32/firmware/README.md
 * for the pins of each board.
 */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/times.h>
#include "mcs_runtime.h"
#include "mcs_port_stm32.h"

/* ------------------------------------------------------------------ board table */
#if defined(MCS_FW_NUCLEO_F401RE) || defined(MCS_FW_NUCLEO_F411RE) || defined(MCS_FW_NUCLEO_F446RE)
#  if defined(MCS_FW_NUCLEO_F401RE)
#    define FW_NAME "NUCLEO-F401RE"
#  elif defined(MCS_FW_NUCLEO_F411RE)
#    define FW_NAME "NUCLEO-F411RE"
#  else
#    define FW_NAME "NUCLEO-F446RE"
#  endif
#  define FW_NUCLEO64 1
#elif defined(MCS_FW_BLACKPILL_F411CE)
#  define FW_NAME "BLACKPILL-F411CE"
#elif defined(MCS_FW_NUCLEO_G474RE)
#  define FW_NAME "NUCLEO-G474RE"
#  define FW_NUCLEO64 1
#elif defined(MCS_FW_NUCLEO_L476RG)
#  define FW_NAME "NUCLEO-L476RG"
#  define FW_NUCLEO64 1
#elif defined(MCS_FW_NUCLEO_H743ZI)
#  define FW_NAME "NUCLEO-H743ZI"
#else
#  error "select a board: -DMCS_FW_NUCLEO_F446RE, ... (see tools/build_stm32_firmware.sh)"
#endif

/* Console UART: handle, instance, index for UART.Open(n), pins (port, TX, RX, AF) */
#if defined(MCS_FW_NUCLEO_G474RE)          /* ST-LINK VCP = LPUART1 on PA2/PA3 */
#  define CON_INST LPUART1
#  define CON_IRQ LPUART1_IRQn
#  define CON_IRQ_HANDLER LPUART1_IRQHandler
#  define CON_CLK() __HAL_RCC_LPUART1_CLK_ENABLE()
#  define CON_INDEX 1
#  define CON_PORT GPIOA
#  define CON_PINS (GPIO_PIN_2 | GPIO_PIN_3)
#  define CON_AF GPIO_AF12_LPUART1
#elif defined(FW_NUCLEO64)                 /* ST-LINK VCP = USART2 on PA2/PA3 */
#  define CON_INST USART2
#  define CON_IRQ USART2_IRQn
#  define CON_IRQ_HANDLER USART2_IRQHandler
#  define CON_CLK() __HAL_RCC_USART2_CLK_ENABLE()
#  define CON_INDEX 2
#  define CON_PORT GPIOA
#  define CON_PINS (GPIO_PIN_2 | GPIO_PIN_3)
#  define CON_AF GPIO_AF7_USART2
#elif defined(MCS_FW_NUCLEO_H743ZI)        /* ST-LINK VCP = USART3 on PD8/PD9 */
#  define CON_INST USART3
#  define CON_IRQ USART3_IRQn
#  define CON_IRQ_HANDLER USART3_IRQHandler
#  define CON_CLK() __HAL_RCC_USART3_CLK_ENABLE()
#  define CON_INDEX 3
#  define CON_PORT GPIOD
#  define CON_PINS (GPIO_PIN_8 | GPIO_PIN_9)
#  define CON_AF GPIO_AF7_USART3
#else                                      /* Black Pill: USART1 PA9 (TX) / PA10 (RX), USB-serial adapter */
#  define CON_INST USART1
#  define CON_IRQ USART1_IRQn
#  define CON_IRQ_HANDLER USART1_IRQHandler
#  define CON_CLK() __HAL_RCC_USART1_CLK_ENABLE()
#  define CON_INDEX 1
#  define CON_PORT GPIOA
#  define CON_PINS (GPIO_PIN_9 | GPIO_PIN_10)
#  define CON_AF GPIO_AF7_USART1
#endif

/* LED: Nucleo-64 LD2 = PA5, Nucleo-144 LD1 = PB0, Black Pill = PC13 */
#if defined(FW_NUCLEO64)
#  define FW_LED "PA5"
#elif defined(MCS_FW_NUCLEO_H743ZI)
#  define FW_LED "PB0"
#else
#  define FW_LED "PC13"
#endif

/* I2C1: Arduino D15 (SCL) / D14 (SDA) = PB8 / PB9 on Nucleo boards, PB6 / PB7 on the Black Pill */
#if defined(MCS_FW_BLACKPILL_F411CE)
#  define I2C_PINS (GPIO_PIN_6 | GPIO_PIN_7)
#else
#  define I2C_PINS (GPIO_PIN_8 | GPIO_PIN_9)
#endif

/* PWM 0..3 = TIM3 CH1..CH4 (AF2): PA6 PA7 PB0 PB1 (Nucleo-64 D12 D11 A3 -, Black Pill);
 * Nucleo-H743ZI: PA6 PB5 PC8 PC9 (PB0 is LD1) */
static const struct { GPIO_TypeDef* port; uint16_t pin; } pwm_pins[4] = {
#if defined(MCS_FW_NUCLEO_H743ZI)
    { GPIOA, GPIO_PIN_6 }, { GPIOB, GPIO_PIN_5 }, { GPIOC, GPIO_PIN_8 }, { GPIOC, GPIO_PIN_9 },
#else
    { GPIOA, GPIO_PIN_6 }, { GPIOA, GPIO_PIN_7 }, { GPIOB, GPIO_PIN_0 }, { GPIOB, GPIO_PIN_1 },
#endif
};

UART_HandleTypeDef huart_con;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim3;

/* ------------------------------------------------------------------ clocks */
static void fw_fault(void) {
    for (;;) { }
}

static void clock_init(void) {
    RCC_OscInitTypeDef o;
    RCC_ClkInitTypeDef c;
    memset(&o, 0, sizeof o);
    memset(&c, 0, sizeof c);
    o.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    o.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    o.PLL.PLLState = RCC_PLL_ON;
    o.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    c.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    c.AHBCLKDivider = RCC_SYSCLK_DIV1;
    uint32_t latency;
#if defined(STM32F4)
    /* HSI 16 MHz / 8 = 2 MHz VCO input */
    __HAL_RCC_PWR_CLK_ENABLE();
    o.HSIState = RCC_HSI_ON;
    o.PLL.PLLM = 8;
    o.PLL.PLLQ = 7;
#  if defined(STM32F401xE)                 /* 84 MHz */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
    o.PLL.PLLN = 168; o.PLL.PLLP = RCC_PLLP_DIV4;
    c.APB1CLKDivider = RCC_HCLK_DIV2; c.APB2CLKDivider = RCC_HCLK_DIV1;
    latency = FLASH_LATENCY_2;
#  elif defined(STM32F411xE)               /* 100 MHz */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    o.PLL.PLLN = 200; o.PLL.PLLP = RCC_PLLP_DIV4; o.PLL.PLLQ = 8;
    c.APB1CLKDivider = RCC_HCLK_DIV2; c.APB2CLKDivider = RCC_HCLK_DIV1;
    latency = FLASH_LATENCY_3;
#  else                                    /* F446: 168 MHz (180 needs over-drive) */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    o.PLL.PLLN = 168; o.PLL.PLLP = RCC_PLLP_DIV2;
#    if defined(RCC_PLLR_SUPPORT) || defined(STM32F446xx)
    o.PLL.PLLR = 2;
#    endif
    c.APB1CLKDivider = RCC_HCLK_DIV4; c.APB2CLKDivider = RCC_HCLK_DIV2;
    latency = FLASH_LATENCY_5;
#  endif
#elif defined(STM32G4)                     /* 170 MHz: HSI 16 / 4 * 85 / 2 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_DisableUCPDDeadBattery();    /* release PB4/PB6 (UCPD dead-battery pull-downs) */
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    o.HSIState = RCC_HSI_ON;
    o.PLL.PLLM = RCC_PLLM_DIV4; o.PLL.PLLN = 85;
    o.PLL.PLLP = RCC_PLLP_DIV2; o.PLL.PLLQ = RCC_PLLQ_DIV2; o.PLL.PLLR = RCC_PLLR_DIV2;
    c.APB1CLKDivider = RCC_HCLK_DIV1; c.APB2CLKDivider = RCC_HCLK_DIV1;
    latency = FLASH_LATENCY_4;
#elif defined(STM32L4)                     /* 80 MHz: HSI 16 * 10 / 2 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
    o.HSIState = RCC_HSI_ON;
    o.PLL.PLLM = 1; o.PLL.PLLN = 10;
    o.PLL.PLLP = RCC_PLLP_DIV7; o.PLL.PLLQ = RCC_PLLQ_DIV2; o.PLL.PLLR = RCC_PLLR_DIV2;
    c.APB1CLKDivider = RCC_HCLK_DIV1; c.APB2CLKDivider = RCC_HCLK_DIV1;
    latency = FLASH_LATENCY_4;
#elif defined(STM32H7)                     /* 400 MHz core, 200 MHz AXI/AHB, 100 MHz APB: HSI 64 / 4 * 50 / 2 */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) { }
    o.HSIState = RCC_HSI_DIV1;
    o.PLL.PLLM = 4; o.PLL.PLLN = 50; o.PLL.PLLP = 2; o.PLL.PLLQ = 4; o.PLL.PLLR = 2;
    o.PLL.PLLRGE = RCC_PLL1VCIRANGE_3; o.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE; o.PLL.PLLFRACN = 0;
    c.ClockType |= RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_D3PCLK1;
    c.SYSCLKDivider = RCC_SYSCLK_DIV1;
    c.AHBCLKDivider = RCC_HCLK_DIV2;
    c.APB3CLKDivider = RCC_APB3_DIV2; c.APB1CLKDivider = RCC_APB1_DIV2;
    c.APB2CLKDivider = RCC_APB2_DIV2; c.APB4CLKDivider = RCC_APB4_DIV2;
    latency = FLASH_LATENCY_2;
#else
#  error "clock setup missing for this family"
#endif
    if (HAL_RCC_OscConfig(&o) != HAL_OK) fw_fault();
    if (HAL_RCC_ClockConfig(&c, latency) != HAL_OK) fw_fault();

    /* kernel clocks: the console and I2C1 run from HSI 16 MHz (64 MHz on H7), independent of the PLL */
#if defined(STM32G4) || defined(STM32L4) || defined(STM32H7)
    RCC_PeriphCLKInitTypeDef p;
    memset(&p, 0, sizeof p);
#  if defined(STM32G4)
    p.PeriphClockSelection = RCC_PERIPHCLK_LPUART1 | RCC_PERIPHCLK_I2C1;
    p.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_HSI;
    p.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
#  elif defined(STM32L4)
    p.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    p.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
#  else
    p.PeriphClockSelection = RCC_PERIPHCLK_I2C123;
    p.I2c123ClockSelection = RCC_I2C123CLKSOURCE_HSI;
#  endif
    if (HAL_RCCEx_PeriphCLKConfig(&p) != HAL_OK) fw_fault();
#endif
}

/* ------------------------------------------------------------------ peripherals */
static void pin_af(GPIO_TypeDef* port, uint16_t pins, uint32_t mode, uint32_t pull, uint32_t af) {
    GPIO_InitTypeDef g;
    memset(&g, 0, sizeof g);
    g.Pin = pins;
    g.Mode = mode;
    g.Pull = pull;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = af;
    HAL_GPIO_Init(port, &g);
}

static void console_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
#if defined(GPIOD)
    __HAL_RCC_GPIOD_CLK_ENABLE();
#endif
    CON_CLK();
    pin_af(CON_PORT, CON_PINS, GPIO_MODE_AF_PP, GPIO_PULLUP, CON_AF);
    huart_con.Instance = CON_INST;
    huart_con.Init.BaudRate = 115200;
    huart_con.Init.WordLength = UART_WORDLENGTH_8B;
    huart_con.Init.StopBits = UART_STOPBITS_1;
    huart_con.Init.Parity = UART_PARITY_NONE;
    huart_con.Init.Mode = UART_MODE_TX_RX;
    huart_con.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_con.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart_con) != HAL_OK) fw_fault();
    HAL_NVIC_SetPriority(CON_IRQ, 1, 0);
    HAL_NVIC_EnableIRQ(CON_IRQ);
}

static void i2c_init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    pin_af(GPIOB, I2C_PINS, GPIO_MODE_AF_OD, GPIO_PULLUP, GPIO_AF4_I2C1);
    hi2c1.Instance = I2C1;
#if defined(STM32F4)
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
#else
    /* 100 kHz standard mode from the RM timing table: prescaler to 4 MHz, SCLL 5 us, SCLH 4 us */
#  if defined(STM32H7)
    const uint32_t kernel = 64000000u;
#  else
    const uint32_t kernel = 16000000u;
#  endif
    hi2c1.Init.Timing = ((kernel / 4000000u - 1u) << 28) | 0x00420F13u;
#endif
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    (void)HAL_I2C_Init(&hi2c1);            /* a missing bus is reported by I2C calls, not here */
}

/* timer input clock of an APB1 timer: PCLK1, doubled when APB1 is divided */
static uint32_t apb1_timer_clock(void) {
    uint32_t p = HAL_RCC_GetPCLK1Freq();
    return p == HAL_RCC_GetHCLKFreq() ? p : 2u * p;
}

static void pwm_init(void) {
    __HAL_RCC_TIM3_CLK_ENABLE();
    for (int i = 0; i < 4; i++)
        pin_af(pwm_pins[i].port, pwm_pins[i].pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_AF2_TIM3);
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 0xFFFF;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) return;
    TIM_OC_InitTypeDef oc;
    memset(&oc, 0, sizeof oc);
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    static const uint32_t ch[4] = { TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3, TIM_CHANNEL_4 };
    for (int i = 0; i < 4; i++) (void)HAL_TIM_PWM_ConfigChannel(&htim3, &oc, ch[i]);
}

/* ------------------------------------------------------------------ interrupts */
void SysTick_Handler(void) { HAL_IncTick(); }
void CON_IRQ_HANDLER(void) { HAL_UART_IRQHandler(&huart_con); }

/* newlib system calls: malloc (unused by MicroCS itself) gets a small, bounded area;
 * stdio is not used - output goes through the console UART */
extern uint8_t _sbrk_start[], _sbrk_end[];
void* _sbrk(ptrdiff_t n) {
    static uint8_t* brk = _sbrk_start;
    if (n < 0 || brk + n > _sbrk_end) { errno = ENOMEM; return (void*)-1; }
    uint8_t* p = brk;
    brk += n;
    return p;
}
int _write(int fd, const char* buf, int n) {
    (void)fd;
    HAL_UART_Transmit(&huart_con, (const uint8_t*)buf, (uint16_t)n, 1000);
    return n;
}
int _read(int fd, char* buf, int n) { (void)fd; (void)buf; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int w) { (void)fd; (void)off; (void)w; return 0; }
int _fstat(int fd, struct stat* st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
clock_t _times(struct tms* t) { (void)t; return (clock_t)-1; }
void _exit(int code) { (void)code; NVIC_SystemReset(); for (;;) { } }

/* ------------------------------------------------------------------ MicroCS */
extern uint8_t _heap_start[], _heap_end[];    /* the rest of the RAM (linker script) */
extern const uint8_t __mcs_fs_flash_start[], __mcs_fs_flash_end[];   /* F4: sectors 1-3 */
static mcs_stm32_board_t board;
static mcs_hal_t hal;
static mcs_runtime_t rt;
static mcs_stm32_flash_t flash;
static mcs_flashfs_t flashfs;

int main(void) {
#if defined(STM32H7)
    SCB_EnableICache();
#endif
    HAL_Init();
    clock_init();
    console_init();
    i2c_init();
    pwm_init();

    board.name = FW_NAME;
    board.led = FW_LED;                                   /* new Pin("LED") in C# */
    board.uart[CON_INDEX] = &huart_con;                   /* the console */
    board.i2c[1] = &hi2c1;                                /* I2C.Open(1, ...) */
    uint32_t tclk = apb1_timer_clock();
    static const uint32_t ch[4] = { TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3, TIM_CHANNEL_4 };
    for (int i = 0; i < 4; i++) board.pwm[i] = (mcs_stm32_pwm_t){ &htim3, ch[i], tclk };   /* PWM / Servo 0..3 */
    mcs_stm32_hal_init(&hal, &board);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = _heap_start;
    cfg.heap_size = (size_t)(_heap_end - _heap_start);
    /* F4: the small sectors next to the vector table (the code starts at sector 4);
     * other families: the top MCS_INTFLASH_SIZE bytes */
    uint32_t fs_addr = (uint32_t)(uintptr_t)__mcs_fs_flash_start;
    uint32_t fs_size = (uint32_t)(__mcs_fs_flash_end - __mcs_fs_flash_start);
    if (!fs_size) fs_addr = 0;
    if (mcs_stm32_flash_init(&flash, fs_addr, fs_size) == 0 &&
        mcs_flashfs_mount(&flashfs, &flash.flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
        cfg.fs_ops = flashfs.ops;                         /* /boot.cs, /main.cs ... survive resets */
        cfg.fs_ctx = flashfs.ctx;
    } else {
        cfg.ramfs_size = 16 * 1024;                       /* RAM disk if the flash region is unusable */
    }
    cfg.console = mcs_stm32_console(&board, CON_INDEX);
    cfg.ticks = mcs_stm32_ticks;
    cfg.delay = mcs_stm32_delay;
    cfg.hal = &hal;
    for (;;) mcs_runtime_run(&rt, &cfg);
}
