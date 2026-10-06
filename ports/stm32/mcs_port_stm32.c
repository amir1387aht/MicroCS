/*
 * MicroCS port for STM32 - drives the STM32Cube HAL. See mcs_port_stm32.h.
 * Add this file + mcs_port_stm32.h to your Cube project next to MicroCS.
 */
#include "mcs_port_stm32.h"
#include <string.h>

static mcs_stm32_board_t* g_board;

static int st_err(HAL_StatusTypeDef s) {
    return s == HAL_OK ? 0 : s == HAL_TIMEOUT ? MCS_HAL_ETIMEOUT : s == HAL_BUSY ? MCS_HAL_EBUSY : MCS_HAL_ERR;
}

/* ------------------------------------------------------------------ GPIO */
static GPIO_TypeDef* port_of(int pin) {
    if (pin < 0) return NULL;
    switch (pin >> 4) {
#ifdef GPIOA
    case 0: __HAL_RCC_GPIOA_CLK_ENABLE(); return GPIOA;
#endif
#ifdef GPIOB
    case 1: __HAL_RCC_GPIOB_CLK_ENABLE(); return GPIOB;
#endif
#ifdef GPIOC
    case 2: __HAL_RCC_GPIOC_CLK_ENABLE(); return GPIOC;
#endif
#ifdef GPIOD
    case 3: __HAL_RCC_GPIOD_CLK_ENABLE(); return GPIOD;
#endif
#ifdef GPIOE
    case 4: __HAL_RCC_GPIOE_CLK_ENABLE(); return GPIOE;
#endif
#ifdef GPIOF
    case 5: __HAL_RCC_GPIOF_CLK_ENABLE(); return GPIOF;
#endif
#ifdef GPIOG
    case 6: __HAL_RCC_GPIOG_CLK_ENABLE(); return GPIOG;
#endif
#ifdef GPIOH
    case 7: __HAL_RCC_GPIOH_CLK_ENABLE(); return GPIOH;
#endif
#ifdef GPIOI
    case 8: __HAL_RCC_GPIOI_CLK_ENABLE(); return GPIOI;
#endif
#ifdef GPIOJ
    case 9: __HAL_RCC_GPIOJ_CLK_ENABLE(); return GPIOJ;
#endif
#ifdef GPIOK
    case 10: __HAL_RCC_GPIOK_CLK_ENABLE(); return GPIOK;
#endif
    default: return NULL;
    }
}
/* fast path for read/write: GPIO ports are 0x400 apart on every STM32
 * family; no clock enable here (GPIO.Mode() did it) */
#if defined(GPIOK)
#define ST_PORTS 11
#elif defined(GPIOJ)
#define ST_PORTS 10
#elif defined(GPIOI)
#define ST_PORTS 9
#elif defined(GPIOH)
#define ST_PORTS 8
#elif defined(GPIOG)
#define ST_PORTS 7
#elif defined(GPIOF)
#define ST_PORTS 6
#elif defined(GPIOE)
#define ST_PORTS 5
#else
#define ST_PORTS 4
#endif
static GPIO_TypeDef* port_fast(int pin) {
    unsigned p = (unsigned)pin >> 4;
    if (pin < 0 || p >= ST_PORTS) return NULL;
    return (GPIO_TypeDef*)(GPIOA_BASE + p * 0x400u);
}

static int st_gpio_mode(void* ctx, int pin, int mode) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    GPIO_TypeDef* g = port_of(pin);
    if (!g) return MCS_HAL_EINVAL;
    GPIO_InitTypeDef init;
    memset(&init, 0, sizeof init);
    init.Pin = 1u << (pin & 15);
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pull = GPIO_NOPULL;
    switch (mode) {
    case MCS_GPIO_INPUT: init.Mode = GPIO_MODE_INPUT; break;
    case MCS_GPIO_OUTPUT: init.Mode = GPIO_MODE_OUTPUT_PP; break;
    case MCS_GPIO_INPUT_PULLUP: init.Mode = GPIO_MODE_INPUT; init.Pull = GPIO_PULLUP; break;
    case MCS_GPIO_INPUT_PULLDOWN: init.Mode = GPIO_MODE_INPUT; init.Pull = GPIO_PULLDOWN; break;
    case MCS_GPIO_OPEN_DRAIN: init.Mode = GPIO_MODE_OUTPUT_OD; init.Pull = GPIO_PULLUP; break;
    case MCS_GPIO_ANALOG: init.Mode = GPIO_MODE_ANALOG; break;
    default: return MCS_HAL_EINVAL;
    }
    HAL_GPIO_Init(g, &init);
    uint16_t bit = (uint16_t)(1u << (pin & 15));
    int p = pin >> 4;
    b->pull_up[p] = (uint16_t)(init.Pull == GPIO_PULLUP ? b->pull_up[p] | bit : b->pull_up[p] & ~bit);
    b->pull_down[p] = (uint16_t)(init.Pull == GPIO_PULLDOWN ? b->pull_down[p] | bit : b->pull_down[p] & ~bit);
    return 0;
}
static int st_gpio_write(void* ctx, int pin, int v) {
    (void)ctx;
    GPIO_TypeDef* g = port_fast(pin);
    if (!g) return MCS_HAL_EINVAL;
    HAL_GPIO_WritePin(g, (uint16_t)(1u << (pin & 15)), v ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}
static int st_gpio_read(void* ctx, int pin) {
    (void)ctx;
    GPIO_TypeDef* g = port_fast(pin);
    if (!g) return MCS_HAL_EINVAL;
    return HAL_GPIO_ReadPin(g, (uint16_t)(1u << (pin & 15))) == GPIO_PIN_SET;
}

static IRQn_Type exti_irq(int line) {
#if defined(STM32F0) || defined(STM32L0) || defined(STM32G0) || defined(STM32C0) || defined(STM32U0)
    return line <= 1 ? EXTI0_1_IRQn : line <= 3 ? EXTI2_3_IRQn : EXTI4_15_IRQn;
#elif defined(STM32L5) || defined(STM32U5) || defined(STM32H5)
    return (IRQn_Type)(EXTI0_IRQn + line);
#else
    static const IRQn_Type t[5] = { EXTI0_IRQn, EXTI1_IRQn,
#if defined(STM32F3)
        EXTI2_TSC_IRQn,
#else
        EXTI2_IRQn,
#endif
        EXTI3_IRQn, EXTI4_IRQn };
    return line < 5 ? t[line] : line < 10 ? EXTI9_5_IRQn : EXTI15_10_IRQn;
#endif
}
static int st_gpio_irq(void* ctx, int pin, int edge) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    GPIO_TypeDef* g = port_of(pin);
    if (!g) return MCS_HAL_EINVAL;
    int line = pin & 15;
    if (edge && b->exti_pin[line] && b->exti_pin[line] != pin + 1) return MCS_HAL_EBUSY;  /* one port per EXTI line */
    GPIO_InitTypeDef init;
    memset(&init, 0, sizeof init);
    init.Pin = 1u << line;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    /* keep the pull configured by GPIO.Mode() */
    int p = pin >> 4;
    init.Pull = (b->pull_up[p] >> line) & 1 ? GPIO_PULLUP : (b->pull_down[p] >> line) & 1 ? GPIO_PULLDOWN : GPIO_NOPULL;
    init.Mode = edge == MCS_GPIO_EDGE_RISING ? GPIO_MODE_IT_RISING : edge == MCS_GPIO_EDGE_FALLING
              ? GPIO_MODE_IT_FALLING : edge == MCS_GPIO_EDGE_BOTH ? GPIO_MODE_IT_RISING_FALLING : GPIO_MODE_INPUT;
    HAL_GPIO_Init(g, &init);
    b->exti_pin[line] = edge ? (uint16_t)(pin + 1) : 0;
    if (edge) {
        HAL_NVIC_SetPriority(exti_irq(line), MCS_STM32_IRQ_PRIORITY, 0);
        HAL_NVIC_EnableIRQ(exti_irq(line));
    }
    return 0;
}

void mcs_stm32_exti(uint16_t gpio_pin) {
    mcs_stm32_board_t* b = g_board;
    if (!b) return;
    for (int line = 0; line < 16; line++) {
        if (!(gpio_pin & (1u << line)) || !b->exti_pin[line]) continue;
        int pin = b->exti_pin[line] - 1;
        GPIO_TypeDef* g = port_fast(pin);
        int level = g ? HAL_GPIO_ReadPin(g, (uint16_t)(1u << line)) == GPIO_PIN_SET : 0;
        mcs_hal_post(MCS_HAL_EV_GPIO, pin, level);
    }
}

/* ------------------------------------------------------------------ UART */
#ifdef HAL_UART_MODULE_ENABLED
static int uart_index(mcs_stm32_board_t* b, UART_HandleTypeDef* h) {
    for (int i = 0; i < MCS_STM32_UARTS; i++) if (b->uart[i] == h) return i;
    return -1;
}
static void rx_arm(mcs_stm32_board_t* b, int i) {
    b->rx[i].active = HAL_UART_Receive_IT(b->uart[i], &b->rx[i].byte, 1) == HAL_OK;
}
void mcs_stm32_uart_rx_cplt(UART_HandleTypeDef* huart) {
    mcs_stm32_board_t* b = g_board;
    if (!b) return;
    int i = uart_index(b, huart);
    if (i < 0) return;
    uint16_t next = (uint16_t)((b->rx[i].head + 1) & (MCS_STM32_UART_RXBUF - 1));
    if (next != b->rx[i].tail) { b->rx[i].buf[b->rx[i].head] = b->rx[i].byte; b->rx[i].head = next; }
    rx_arm(b, i);
}
void mcs_stm32_uart_error(UART_HandleTypeDef* huart) {
    mcs_stm32_board_t* b = g_board;
    if (!b) return;
    int i = uart_index(b, huart);
    if (i >= 0) rx_arm(b, i);       /* overrun / framing error: restart reception */
}
static UART_HandleTypeDef* uart_of(mcs_stm32_board_t* b, int port) {
    return port >= 0 && port < MCS_STM32_UARTS ? b->uart[port] : NULL;
}
static int st_uart_config(void* ctx, int port, const mcs_uart_cfg_t* c) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    UART_HandleTypeDef* h = uart_of(b, port);
    if (!h) return MCS_HAL_ENOTSUP;
    if (c->baud) {
        HAL_UART_AbortReceive_IT(h);
        h->Init.BaudRate = c->baud;
        /* STM32 counts the parity bit as a data bit */
        int bits = c->data_bits + (c->parity ? 1 : 0);
        h->Init.WordLength = bits == 9 ? UART_WORDLENGTH_9B :
#ifdef UART_WORDLENGTH_7B
                             bits == 7 ? UART_WORDLENGTH_7B :
#endif
                             UART_WORDLENGTH_8B;
        if (bits != 8 && bits != 9
#ifdef UART_WORDLENGTH_7B
            && bits != 7
#endif
        ) return MCS_HAL_ENOTSUP;
        h->Init.Parity = c->parity == MCS_UART_PARITY_ODD ? UART_PARITY_ODD
                       : c->parity == MCS_UART_PARITY_EVEN ? UART_PARITY_EVEN : UART_PARITY_NONE;
        h->Init.StopBits = c->stop_bits == 2 ? UART_STOPBITS_2 : UART_STOPBITS_1;
        h->Init.HwFlowCtl = c->flow_control ? UART_HWCONTROL_RTS_CTS : UART_HWCONTROL_NONE;
        int r = st_err(HAL_UART_Init(h));
        if (r) return r;
    }
    b->rx[port].head = b->rx[port].tail = 0;
    rx_arm(b, port);
    return 0;
}
static int st_uart_open(void* ctx, int port, uint32_t baud) {
    mcs_uart_cfg_t c = { baud, 8, MCS_UART_PARITY_NONE, 1, 0 };
    return st_uart_config(ctx, port, &c);
}
static int st_uart_close(void* ctx, int port) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    UART_HandleTypeDef* h = uart_of(b, port);
    if (!h) return MCS_HAL_ENOTSUP;
    HAL_UART_AbortReceive_IT(h);
    b->rx[port].active = 0;
    return 0;
}
static int st_uart_write(void* ctx, int port, const uint8_t* d, size_t n) {
    UART_HandleTypeDef* h = uart_of((mcs_stm32_board_t*)ctx, port);
    if (!h) return MCS_HAL_ENOTSUP;
    int r = st_err(HAL_UART_Transmit(h, (uint8_t*)(uintptr_t)d, (uint16_t)n, 1000 + (uint32_t)n));
    return r ? r : (int)n;
}
static int rx_pop(mcs_stm32_board_t* b, int port) {
    if (b->rx[port].tail == b->rx[port].head) return -1;
    uint8_t c = b->rx[port].buf[b->rx[port].tail];
    b->rx[port].tail = (uint16_t)((b->rx[port].tail + 1) & (MCS_STM32_UART_RXBUF - 1));
    return c;
}
static int st_uart_read(void* ctx, int port, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    UART_HandleTypeDef* h = uart_of(b, port);
    if (!h) return MCS_HAL_ENOTSUP;
    if (!b->rx[port].active) rx_arm(b, port);
    size_t k = 0;
    uint32_t t0 = HAL_GetTick();
    while (k < n) {
        int c = rx_pop(b, port);
        if (c >= 0) { buf[k++] = (uint8_t)c; continue; }
        if (k || HAL_GetTick() - t0 >= timeout_ms) break;   /* return what we have */
    }
    return (int)k;
}
static int st_uart_available(void* ctx, int port) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (!uart_of(b, port)) return MCS_HAL_ENOTSUP;
    if (!b->rx[port].active) rx_arm(b, port);
    return (int)((b->rx[port].head - b->rx[port].tail) & (MCS_STM32_UART_RXBUF - 1));
}

typedef struct { mcs_stm32_board_t* b; int port; } console_t;
static console_t g_console;
static int con_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    console_t* c = (console_t*)ud;
    return st_uart_read(c->b, c->port, buf, n, timeout_ms);
}
static void con_write(void* ud, const char* s, size_t n) {
    console_t* c = (console_t*)ud;
    st_uart_write(c->b, c->port, (const uint8_t*)s, n);
}
mcs_transport_t mcs_stm32_console(mcs_stm32_board_t* b, int port) {
    g_board = b;
    g_console.b = b; g_console.port = port;
    rx_arm(b, port);
    mcs_transport_t t = { con_read, con_write, &g_console };
    return t;
}
#if MCS_STM32_DEFINE_CALLBACKS
void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) { mcs_stm32_uart_rx_cplt(huart); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) { mcs_stm32_uart_error(huart); }
#endif
#endif /* UART */

#if MCS_STM32_DEFINE_CALLBACKS
#if defined(STM32G0) || defined(STM32L5) || defined(STM32U5) || defined(STM32H5) || defined(STM32C0) || defined(STM32U0) || defined(STM32MP1)
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin) { mcs_stm32_exti(pin); }
void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin) { mcs_stm32_exti(pin); }
#else
void HAL_GPIO_EXTI_Callback(uint16_t pin) { mcs_stm32_exti(pin); }
#endif
#endif
#if MCS_STM32_EXTI_HANDLERS
static void exti_range(int lo, int hi) { for (int l = lo; l <= hi; l++) HAL_GPIO_EXTI_IRQHandler((uint16_t)(1u << l)); }
#if defined(STM32F0) || defined(STM32L0) || defined(STM32G0) || defined(STM32C0) || defined(STM32U0)
void EXTI0_1_IRQHandler(void) { exti_range(0, 1); }
void EXTI2_3_IRQHandler(void) { exti_range(2, 3); }
void EXTI4_15_IRQHandler(void) { exti_range(4, 15); }
#elif !(defined(STM32L5) || defined(STM32U5) || defined(STM32H5))
void EXTI0_IRQHandler(void) { exti_range(0, 0); }
void EXTI1_IRQHandler(void) { exti_range(1, 1); }
#if defined(STM32F3)
void EXTI2_TSC_IRQHandler(void) { exti_range(2, 2); }
#else
void EXTI2_IRQHandler(void) { exti_range(2, 2); }
#endif
void EXTI3_IRQHandler(void) { exti_range(3, 3); }
void EXTI4_IRQHandler(void) { exti_range(4, 4); }
void EXTI9_5_IRQHandler(void) { exti_range(5, 9); }
void EXTI15_10_IRQHandler(void) { exti_range(10, 15); }
#endif
#endif

/* ------------------------------------------------------------------ I2C */
#ifdef HAL_I2C_MODULE_ENABLED
static I2C_HandleTypeDef* i2c_of(void* ctx, int bus) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    return bus >= 0 && bus < MCS_STM32_BUSES ? b->i2c[bus] : NULL;
}
static int i2c_err(I2C_HandleTypeDef* h, HAL_StatusTypeDef s) {
    if (s == HAL_OK) return 0;
    if (s == HAL_ERROR && (HAL_I2C_GetError(h) & HAL_I2C_ERROR_AF)) return MCS_HAL_ENODEV;   /* NACK */
    return st_err(s);
}
static int st_i2c_open(void* ctx, int bus, uint32_t freq) {
    I2C_HandleTypeDef* h = i2c_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
#if defined(STM32F1) || defined(STM32F2) || defined(STM32F4) || defined(STM32L1)
    if (freq && freq != h->Init.ClockSpeed) {     /* older I2C IP: speed is a plain field */
        h->Init.ClockSpeed = freq;
        return st_err(HAL_I2C_Init(h));
    }
#else
    (void)freq;      /* I2C v2 timing register: computed by CubeMX (Init.Timing) */
#endif
    return 0;
}
static int st_i2c_write(void* ctx, int bus, int addr, const uint8_t* d, size_t n) {
    I2C_HandleTypeDef* h = i2c_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    return i2c_err(h, HAL_I2C_Master_Transmit(h, (uint16_t)(addr << 1), (uint8_t*)(uintptr_t)d, (uint16_t)n, MCS_STM32_TIMEOUT_MS));
}
static int st_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    I2C_HandleTypeDef* h = i2c_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    return i2c_err(h, HAL_I2C_Master_Receive(h, (uint16_t)(addr << 1), buf, (uint16_t)n, MCS_STM32_TIMEOUT_MS));
}
static int st_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    I2C_HandleTypeDef* h = i2c_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    if (tn == 1 || tn == 2) {   /* register read with a repeated start */
        uint16_t reg = tn == 1 ? tx[0] : (uint16_t)(tx[0] << 8 | tx[1]);
        return i2c_err(h, HAL_I2C_Mem_Read(h, (uint16_t)(addr << 1), reg, tn == 1 ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT,
                                           rx, (uint16_t)rn, MCS_STM32_TIMEOUT_MS));
    }
    int r = st_i2c_write(ctx, bus, addr, tx, tn);
    return r ? r : st_i2c_read(ctx, bus, addr, rx, rn);
}
static int st_i2c_probe(void* ctx, int bus, int addr) {
    I2C_HandleTypeDef* h = i2c_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    return HAL_I2C_IsDeviceReady(h, (uint16_t)(addr << 1), 1, 5) == HAL_OK ? 0 : MCS_HAL_ENODEV;
}
#endif

/* ------------------------------------------------------------------ SPI */
#ifdef HAL_SPI_MODULE_ENABLED
static SPI_HandleTypeDef* spi_of(void* ctx, int bus) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    return bus >= 0 && bus < MCS_STM32_BUSES ? b->spi[bus] : NULL;
}
static int st_spi_open(void* ctx, int bus, const mcs_spi_cfg_t* c) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    SPI_HandleTypeDef* h = spi_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    if (c->bits != 8) return MCS_HAL_ENOTSUP;
    h->Init.CLKPolarity = (c->mode & 2) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    h->Init.CLKPhase = (c->mode & 1) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    h->Init.FirstBit = c->lsb_first ? SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB;
    uint32_t clk = b->spi_clock_hz[bus];
    if (clk && c->freq_hz) {
        static const uint32_t pre[8] = { SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4, SPI_BAUDRATEPRESCALER_8,
            SPI_BAUDRATEPRESCALER_16, SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
            SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256 };
        int i = 0;
        while (i < 7 && (clk >> (i + 1)) > c->freq_hz) i++;   /* fastest clock <= requested */
        h->Init.BaudRatePrescaler = pre[i];
    }
    return st_err(HAL_SPI_Init(h));
}
static int st_spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    SPI_HandleTypeDef* h = spi_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    uint32_t to = MCS_STM32_TIMEOUT_MS + (uint32_t)n / 8;
    HAL_StatusTypeDef s;
    if (tx && rx) s = HAL_SPI_TransmitReceive(h, (uint8_t*)(uintptr_t)tx, rx, (uint16_t)n, to);
    else if (tx) s = HAL_SPI_Transmit(h, (uint8_t*)(uintptr_t)tx, (uint16_t)n, to);
    else s = HAL_SPI_Receive(h, rx, (uint16_t)n, to);
    return st_err(s);
}
#endif

/* ------------------------------------------------------------------ ADC / DAC */
#ifdef HAL_ADC_MODULE_ENABLED
#ifndef MCS_STM32_ADC_SAMPLETIME
#  if defined(ADC_SAMPLETIME_480CYCLES)
#    define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_480CYCLES       /* F2 F4 F7 */
#  elif defined(ADC_SAMPLETIME_810CYCLES_5)
#    define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_810CYCLES_5     /* H7 */
#  elif defined(ADC_SAMPLETIME_640CYCLES_5)
#    define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_640CYCLES_5     /* G4 L4 L5 U5 WB H5 */
#  elif defined(ADC_SAMPLETIME_601CYCLES_5)
#    define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_601CYCLES_5     /* F3 */
#  elif defined(ADC_SAMPLETIME_384CYCLES)
#    define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_384CYCLES       /* L1 */
#  elif defined(ADC_SAMPLETIME_239CYCLES_5)
#    define MCS_STM32_ADC_SAMPLETIME ADC_SAMPLETIME_239CYCLES_5     /* F0 F1 */
#  endif
#endif
static int st_adc_read(void* ctx, int ch) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (ch < 0 || ch >= MCS_STM32_ADC_CHANNELS || !b->adc[ch].h) return MCS_HAL_ENOTSUP;
    ADC_HandleTypeDef* h = b->adc[ch].h;
    ADC_ChannelConfTypeDef c;
    memset(&c, 0, sizeof c);
    c.Channel = b->adc[ch].channel;
#ifdef ADC_REGULAR_RANK_1
    c.Rank = ADC_REGULAR_RANK_1;
#else
    c.Rank = 1;
#endif
#if defined(ADC_SAMPLINGTIME_COMMON_1)
    c.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;           /* G0 C0 U0 WL: time set in hadc.Init */
#elif defined(MCS_STM32_ADC_SAMPLETIME) && !defined(STM32L0)
    c.SamplingTime = MCS_STM32_ADC_SAMPLETIME;            /* L0: set in hadc.Init */
#endif
#if defined(STM32F3) || defined(STM32G4) || defined(STM32H7) || defined(STM32L4) || defined(STM32L5) \
    || defined(STM32U5) || defined(STM32WB) || defined(STM32H5)
    c.SingleDiff = ADC_SINGLE_ENDED;
    c.OffsetNumber = ADC_OFFSET_NONE;
#endif
    int r = st_err(HAL_ADC_ConfigChannel(h, &c));
    if (r) return r;
    if ((r = st_err(HAL_ADC_Start(h)))) return r;
    r = st_err(HAL_ADC_PollForConversion(h, 10));
    int v = r ? r : (int)HAL_ADC_GetValue(h);
    HAL_ADC_Stop(h);
    return v;
}
#endif
#ifdef MCS_STM32_HAS_DAC
static int st_dac_write(void* ctx, int ch, uint32_t v) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (!b->dac || ch < 0 || ch > 1) return MCS_HAL_ENOTSUP;
    uint32_t c = ch == 0 ? DAC_CHANNEL_1 :
#ifdef DAC_CHANNEL_2
        DAC_CHANNEL_2;
#else
        0xFFFFFFFFu;
    if (c == 0xFFFFFFFFu) return MCS_HAL_ENOTSUP;
#endif
    int r = st_err(HAL_DAC_SetValue(b->dac, c, DAC_ALIGN_12B_R, v > 4095 ? 4095 : v));
    if (!r && !b->dac_started[ch]) { r = st_err(HAL_DAC_Start(b->dac, c)); b->dac_started[ch] = !r; }
    return r;
}
#endif

/* ------------------------------------------------------------------ PWM / timers */
#ifdef HAL_TIM_MODULE_ENABLED
/* pick prescaler + auto-reload for f = clk / (psc+1) / (arr+1), arr as large as possible (16-bit) */
static int tim_period(uint32_t clk, uint32_t f, uint32_t* psc, uint32_t* arr) {
    if (!f || f > clk / 2) return MCS_HAL_EINVAL;
    uint32_t total = clk / f;                    /* (psc+1)*(arr+1) */
    uint32_t p = total / 65536u;
    if (p > 65535u) return MCS_HAL_EINVAL;
    *psc = p;
    *arr = total / (p + 1) - 1;
    return 0;
}
static int st_pwm_set16(void* ctx, int ch, uint32_t freq, uint16_t duty) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (ch < 0 || ch >= MCS_STM32_PWM_CHANNELS || !b->pwm[ch].h) return MCS_HAL_ENOTSUP;
    mcs_stm32_pwm_t* p = &b->pwm[ch];
    uint32_t psc, arr;
    int r = tim_period(p->clock_hz ? p->clock_hz : SystemCoreClock, freq, &psc, &arr);
    if (r) return r;
    __HAL_TIM_SET_PRESCALER(p->h, psc);
    __HAL_TIM_SET_AUTORELOAD(p->h, arr);
    __HAL_TIM_SET_COMPARE(p->h, p->channel, (uint32_t)(((uint64_t)(arr + 1) * duty) / 65535u));
    if (!b->pwm_started[ch]) {
        if ((r = st_err(HAL_TIM_PWM_Start(p->h, p->channel)))) return r;
        b->pwm_started[ch] = 1;
    }
    return 0;
}
static int st_pwm_set(void* ctx, int ch, uint32_t freq, uint16_t permille) {
    return st_pwm_set16(ctx, ch, freq, (uint16_t)((uint32_t)permille * 65535u / 1000u));
}
static int st_pwm_stop(void* ctx, int ch) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (ch < 0 || ch >= MCS_STM32_PWM_CHANNELS || !b->pwm[ch].h) return MCS_HAL_ENOTSUP;
    b->pwm_started[ch] = 0;
    return st_err(HAL_TIM_PWM_Stop(b->pwm[ch].h, b->pwm[ch].channel));
}
static int st_timer_start(void* ctx, int t, uint32_t period_us, int periodic) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (t < 0 || t >= MCS_STM32_TIMERS || !b->timer[t].h) return MCS_HAL_ENOTSUP;
    if (!period_us) return MCS_HAL_EINVAL;
    TIM_HandleTypeDef* h = b->timer[t].h;
    uint32_t clk = b->timer[t].clock_hz ? b->timer[t].clock_hz : SystemCoreClock;
    /* 1 MHz tick when it fits in 16 bits, else 10 kHz, else 1 kHz */
    uint32_t tick = period_us <= 65536u ? 1000000u : period_us <= 6553600u ? 10000u : 1000u;
    uint32_t counts = (uint32_t)((uint64_t)period_us * tick / 1000000u);
    if (counts < 1 || counts > 65536u || clk / tick > 65536u) return MCS_HAL_EINVAL;
    HAL_TIM_Base_Stop_IT(h);
    __HAL_TIM_SET_PRESCALER(h, clk / tick - 1);
    __HAL_TIM_SET_AUTORELOAD(h, counts - 1);
    __HAL_TIM_SET_COUNTER(h, 0);
    h->Instance->EGR = TIM_EGR_UG;                 /* load the prescaler now */
    __HAL_TIM_CLEAR_FLAG(h, TIM_FLAG_UPDATE);
    b->timer_count[t] = 0;
    b->timer_once[t] = !periodic;
    return st_err(HAL_TIM_Base_Start_IT(h));
}
static int st_timer_stop(void* ctx, int t) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (t < 0 || t >= MCS_STM32_TIMERS || !b->timer[t].h) return MCS_HAL_ENOTSUP;
    return st_err(HAL_TIM_Base_Stop_IT(b->timer[t].h));
}
void mcs_stm32_tim_elapsed(TIM_HandleTypeDef* htim) {
    mcs_stm32_board_t* b = g_board;
    if (!b) return;
    for (int t = 0; t < MCS_STM32_TIMERS; t++) {
        if (b->timer[t].h != htim) continue;
        b->timer_count[t]++;
        if (b->timer_once[t]) HAL_TIM_Base_Stop_IT(htim);
        mcs_hal_post(MCS_HAL_EV_TIMER, t, (int32_t)b->timer_count[t]);
    }
}
#if MCS_STM32_DEFINE_TIM_CALLBACK
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim) { mcs_stm32_tim_elapsed(htim); }
#endif
#endif

/* ------------------------------------------------------------------ I2S */
#ifdef MCS_STM32_HAS_I2S
static I2S_HandleTypeDef* i2s_of(void* ctx, int bus) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    return bus >= 0 && bus < MCS_STM32_BUSES ? b->i2s[bus] : NULL;
}
static int st_i2s_open(void* ctx, int bus, const mcs_i2s_cfg_t* c) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    I2S_HandleTypeDef* h = i2s_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    if (c->direction == MCS_I2S_DUPLEX) return MCS_HAL_ENOTSUP;
    h->Init.Mode = c->direction == MCS_I2S_RX ? I2S_MODE_MASTER_RX : I2S_MODE_MASTER_TX;
    h->Init.Standard = c->format == MCS_I2S_MSB ? I2S_STANDARD_MSB : c->format == MCS_I2S_PCM ? I2S_STANDARD_PCM_SHORT : I2S_STANDARD_PHILIPS;
    h->Init.DataFormat = c->bits == 16 ? I2S_DATAFORMAT_16B : c->bits == 24 ? I2S_DATAFORMAT_24B : I2S_DATAFORMAT_32B;
    h->Init.AudioFreq = c->sample_rate;
    b->i2s_bits[bus] = c->bits;
    if (c->channels != 2) return MCS_HAL_ENOTSUP;  /* I2S frames are always stereo on STM32 */
    return st_err(HAL_I2S_Init(h));
}
static int st_i2s_write(void* ctx, int bus, const uint8_t* d, size_t n, uint32_t timeout_ms) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    I2S_HandleTypeDef* h = i2s_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    size_t unit = b->i2s_bits[bus] > 16 ? 4 : 2;
    int r = st_err(HAL_I2S_Transmit(h, (uint16_t*)(uintptr_t)d, (uint16_t)(n / unit), timeout_ms));
    return r ? r : (int)(n / unit * unit);
}
static int st_i2s_read(void* ctx, int bus, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    I2S_HandleTypeDef* h = i2s_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    size_t unit = b->i2s_bits[bus] > 16 ? 4 : 2;
    int r = st_err(HAL_I2S_Receive(h, (uint16_t*)(void*)buf, (uint16_t)(n / unit), timeout_ms));
    return r ? r : (int)(n / unit * unit);
}
static int st_i2s_close(void* ctx, int bus) {
    I2S_HandleTypeDef* h = i2s_of(ctx, bus);
    return h ? st_err(HAL_I2S_DMAStop(h)) : MCS_HAL_ENOTSUP;
}
#endif

/* ------------------------------------------------------------------ QSPI / OSPI */
#if defined(MCS_STM32_HAS_QSPI)
static int st_qspi_open(void* ctx, int bus, uint32_t freq) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    (void)freq;     /* clock prescaler comes from CubeMX */
    return bus >= 0 && bus < MCS_STM32_BUSES && b->qspi[bus] ? 0 : MCS_HAL_ENOTSUP;
}
static uint32_t q_lines(uint8_t n, uint32_t none, uint32_t l1, uint32_t l2, uint32_t l4) {
    return n == 0 ? none : n == 1 ? l1 : n == 2 ? l2 : l4;
}
static int st_qspi_command(void* ctx, int bus, const mcs_qspi_cmd_t* q, const uint8_t* tx, uint8_t* rx, size_t n) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (bus < 0 || bus >= MCS_STM32_BUSES || !b->qspi[bus]) return MCS_HAL_ENOTSUP;
    if (q->instr_lines > 4 || q->addr_lines > 4 || q->data_lines > 4) return MCS_HAL_ENOTSUP;
    QSPI_HandleTypeDef* h = b->qspi[bus];
    QSPI_CommandTypeDef c;
    memset(&c, 0, sizeof c);
    c.Instruction = q->instruction;
    c.InstructionMode = q_lines(q->instr_lines, QSPI_INSTRUCTION_NONE, QSPI_INSTRUCTION_1_LINE, QSPI_INSTRUCTION_2_LINES, QSPI_INSTRUCTION_4_LINES);
    c.Address = q->address;
    c.AddressMode = q->addr_bytes ? q_lines(q->addr_lines ? q->addr_lines : 1, QSPI_ADDRESS_NONE, QSPI_ADDRESS_1_LINE, QSPI_ADDRESS_2_LINES, QSPI_ADDRESS_4_LINES) : QSPI_ADDRESS_NONE;
    c.AddressSize = q->addr_bytes <= 1 ? QSPI_ADDRESS_8_BITS : q->addr_bytes == 2 ? QSPI_ADDRESS_16_BITS
                  : q->addr_bytes == 3 ? QSPI_ADDRESS_24_BITS : QSPI_ADDRESS_32_BITS;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    c.DummyCycles = q->dummy_cycles;
    c.DataMode = n ? q_lines(q->data_lines ? q->data_lines : 1, QSPI_DATA_NONE, QSPI_DATA_1_LINE, QSPI_DATA_2_LINES, QSPI_DATA_4_LINES) : QSPI_DATA_NONE;
    c.NbData = (uint32_t)n;
    c.DdrMode = QSPI_DDR_MODE_DISABLE;
    c.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    int r = st_err(HAL_QSPI_Command(h, &c, MCS_STM32_TIMEOUT_MS));
    if (r || !n) return r;
    if (tx) return st_err(HAL_QSPI_Transmit(h, (uint8_t*)(uintptr_t)tx, MCS_STM32_TIMEOUT_MS));
    return st_err(HAL_QSPI_Receive(h, rx, MCS_STM32_TIMEOUT_MS));
}
#elif defined(MCS_STM32_HAS_OSPI)
static int st_qspi_open(void* ctx, int bus, uint32_t freq) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    (void)freq;
    return bus >= 0 && bus < MCS_STM32_BUSES && b->ospi[bus] ? 0 : MCS_HAL_ENOTSUP;
}
static uint32_t o_lines(uint8_t n, uint32_t none, uint32_t l1, uint32_t l2, uint32_t l4, uint32_t l8) {
    return n == 0 ? none : n == 1 ? l1 : n == 2 ? l2 : n == 4 ? l4 : l8;
}
static int st_qspi_command(void* ctx, int bus, const mcs_qspi_cmd_t* q, const uint8_t* tx, uint8_t* rx, size_t n) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (bus < 0 || bus >= MCS_STM32_BUSES || !b->ospi[bus]) return MCS_HAL_ENOTSUP;
    OSPI_HandleTypeDef* h = b->ospi[bus];
    OSPI_RegularCmdTypeDef c;
    memset(&c, 0, sizeof c);
    c.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId = HAL_OSPI_FLASH_ID_1;
    c.Instruction = q->instruction;
    c.InstructionMode = o_lines(q->instr_lines, HAL_OSPI_INSTRUCTION_NONE, HAL_OSPI_INSTRUCTION_1_LINE,
                                HAL_OSPI_INSTRUCTION_2_LINES, HAL_OSPI_INSTRUCTION_4_LINES, HAL_OSPI_INSTRUCTION_8_LINES);
    c.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    c.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    c.Address = q->address;
    c.AddressMode = q->addr_bytes ? o_lines(q->addr_lines ? q->addr_lines : 1, HAL_OSPI_ADDRESS_NONE, HAL_OSPI_ADDRESS_1_LINE,
                                            HAL_OSPI_ADDRESS_2_LINES, HAL_OSPI_ADDRESS_4_LINES, HAL_OSPI_ADDRESS_8_LINES) : HAL_OSPI_ADDRESS_NONE;
    c.AddressSize = q->addr_bytes <= 1 ? HAL_OSPI_ADDRESS_8_BITS : q->addr_bytes == 2 ? HAL_OSPI_ADDRESS_16_BITS
                  : q->addr_bytes == 3 ? HAL_OSPI_ADDRESS_24_BITS : HAL_OSPI_ADDRESS_32_BITS;
    c.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DataMode = n ? o_lines(q->data_lines ? q->data_lines : 1, HAL_OSPI_DATA_NONE, HAL_OSPI_DATA_1_LINE,
                             HAL_OSPI_DATA_2_LINES, HAL_OSPI_DATA_4_LINES, HAL_OSPI_DATA_8_LINES) : HAL_OSPI_DATA_NONE;
    c.NbData = (uint32_t)n;
    c.DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;
    c.DummyCycles = q->dummy_cycles;
    c.DQSMode = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    int r = st_err(HAL_OSPI_Command(h, &c, MCS_STM32_TIMEOUT_MS));
    if (r || !n) return r;
    if (tx) return st_err(HAL_OSPI_Transmit(h, (uint8_t*)(uintptr_t)tx, MCS_STM32_TIMEOUT_MS));
    return st_err(HAL_OSPI_Receive(h, rx, MCS_STM32_TIMEOUT_MS));
}
#endif

/* ------------------------------------------------------------------ CAN (bxCAN or FDCAN in classic mode) */
#if defined(MCS_STM32_HAS_CAN)
static CAN_HandleTypeDef* can_of(void* ctx, int bus) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    return bus >= 0 && bus < MCS_STM32_BUSES ? b->can[bus] : NULL;
}
static int st_can_open(void* ctx, int bus, uint32_t bitrate) {
    CAN_HandleTypeDef* h = can_of(ctx, bus);
    (void)bitrate;   /* bit timing comes from CubeMX */
    if (!h) return MCS_HAL_ENOTSUP;
    CAN_FilterTypeDef f;
    memset(&f, 0, sizeof f);            /* accept everything into FIFO 0 */
    f.FilterBank = bus == 0 ? 0 : 14;
    f.FilterMode = CAN_FILTERMODE_IDMASK;
    f.FilterScale = CAN_FILTERSCALE_32BIT;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation = ENABLE;
    f.SlaveStartFilterBank = 14;
    int r = st_err(HAL_CAN_ConfigFilter(h, &f));
    if (r) return r;
    if (HAL_CAN_GetState(h) == HAL_CAN_STATE_LISTENING) return 0;
    return st_err(HAL_CAN_Start(h));
}
static int st_can_send(void* ctx, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    CAN_HandleTypeDef* h = can_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    uint32_t t0 = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(h) == 0)
        if (HAL_GetTick() - t0 >= timeout_ms) return MCS_HAL_ETIMEOUT;
    CAN_TxHeaderTypeDef hd;
    memset(&hd, 0, sizeof hd);
    hd.StdId = fr->id & 0x7FF;
    hd.ExtId = fr->id;
    hd.IDE = fr->extended ? CAN_ID_EXT : CAN_ID_STD;
    hd.RTR = fr->rtr ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    hd.DLC = fr->len;
    hd.TransmitGlobalTime = DISABLE;
    uint32_t mbox;
    return st_err(HAL_CAN_AddTxMessage(h, &hd, (uint8_t*)(uintptr_t)fr->data, &mbox));
}
static int st_can_recv(void* ctx, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    CAN_HandleTypeDef* h = can_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    uint32_t t0 = HAL_GetTick();
    while (HAL_CAN_GetRxFifoFillLevel(h, CAN_RX_FIFO0) == 0)
        if (HAL_GetTick() - t0 >= timeout_ms) return MCS_HAL_ETIMEOUT;
    CAN_RxHeaderTypeDef hd;
    int r = st_err(HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &hd, fr->data));
    if (r) return r;
    fr->extended = hd.IDE == CAN_ID_EXT;
    fr->id = fr->extended ? hd.ExtId : hd.StdId;
    fr->rtr = hd.RTR == CAN_RTR_REMOTE;
    fr->len = (uint8_t)(hd.DLC > 8 ? 8 : hd.DLC);
    return 0;
}
#elif defined(MCS_STM32_HAS_FDCAN)
static FDCAN_HandleTypeDef* can_of(void* ctx, int bus) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    return bus >= 0 && bus < MCS_STM32_BUSES ? b->fdcan[bus] : NULL;
}
static const uint32_t fd_dlc[9] = { FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2, FDCAN_DLC_BYTES_3,
    FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5, FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8 };
static int st_can_open(void* ctx, int bus, uint32_t bitrate) {
    FDCAN_HandleTypeDef* h = can_of(ctx, bus);
    (void)bitrate;
    if (!h) return MCS_HAL_ENOTSUP;
    if (h->State == HAL_FDCAN_STATE_BUSY) return 0;
    int r = st_err(HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                                FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE));
    return r ? r : st_err(HAL_FDCAN_Start(h));
}
static int st_can_send(void* ctx, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    FDCAN_HandleTypeDef* h = can_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    uint32_t t0 = HAL_GetTick();
    while (HAL_FDCAN_GetTxFifoFreeLevel(h) == 0)
        if (HAL_GetTick() - t0 >= timeout_ms) return MCS_HAL_ETIMEOUT;
    FDCAN_TxHeaderTypeDef hd;
    memset(&hd, 0, sizeof hd);
    hd.Identifier = fr->id;
    hd.IdType = fr->extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    hd.TxFrameType = fr->rtr ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    hd.DataLength = fd_dlc[fr->len > 8 ? 8 : fr->len];
    hd.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    hd.BitRateSwitch = FDCAN_BRS_OFF;
    hd.FDFormat = FDCAN_CLASSIC_CAN;
    hd.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    return st_err(HAL_FDCAN_AddMessageToTxFifoQ(h, &hd, (uint8_t*)(uintptr_t)fr->data));
}
static int st_can_recv(void* ctx, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    FDCAN_HandleTypeDef* h = can_of(ctx, bus);
    if (!h) return MCS_HAL_ENOTSUP;
    uint32_t t0 = HAL_GetTick();
    while (HAL_FDCAN_GetRxFifoFillLevel(h, FDCAN_RX_FIFO0) == 0)
        if (HAL_GetTick() - t0 >= timeout_ms) return MCS_HAL_ETIMEOUT;
    FDCAN_RxHeaderTypeDef hd;
    uint8_t data[64];
    int r = st_err(HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO0, &hd, data));
    if (r) return r;
    fr->extended = hd.IdType == FDCAN_EXTENDED_ID;
    fr->id = hd.Identifier;
    fr->rtr = hd.RxFrameType == FDCAN_REMOTE_FRAME;
    fr->len = 8;
    for (uint8_t i = 0; i < 9; i++) if (fd_dlc[i] == hd.DataLength) { fr->len = i; break; }
    memcpy(fr->data, data, fr->len);
    return 0;
}
#endif

/* ------------------------------------------------------------------ watchdog / RTC */
#ifdef HAL_IWDG_MODULE_ENABLED
static IWDG_HandleTypeDef g_iwdg;
static IWDG_HandleTypeDef* iwdg_of(mcs_stm32_board_t* b) {
    if (b->iwdg) return b->iwdg;
#if defined(IWDG1)
    g_iwdg.Instance = IWDG1;
#else
    g_iwdg.Instance = IWDG;
#endif
    return &g_iwdg;
}
static int st_wdt_start(void* ctx, uint32_t timeout_ms) {
    IWDG_HandleTypeDef* h = iwdg_of((mcs_stm32_board_t*)ctx);
    /* LSI ~32 kHz: ticks = ms * 32 / prescaler, reload <= 4095 */
    static const uint32_t pre[7] = { IWDG_PRESCALER_4, IWDG_PRESCALER_8, IWDG_PRESCALER_16, IWDG_PRESCALER_32,
                                     IWDG_PRESCALER_64, IWDG_PRESCALER_128, IWDG_PRESCALER_256 };
    uint32_t div = 4;
    int i = 0;
    while (i < 6 && (uint64_t)timeout_ms * 32u / div > 4095u) { i++; div <<= 1; }
    uint32_t reload = (uint32_t)((uint64_t)timeout_ms * 32u / div);
    if (reload < 1) reload = 1;
    if (reload > 4095) reload = 4095;
    h->Init.Prescaler = pre[i];
    h->Init.Reload = reload;
#ifdef IWDG_WINDOW_DISABLE
    h->Init.Window = IWDG_WINDOW_DISABLE;
#endif
    return st_err(HAL_IWDG_Init(h));
}
static int st_wdt_feed(void* ctx) { return st_err(HAL_IWDG_Refresh(iwdg_of((mcs_stm32_board_t*)ctx))); }
#endif
#ifdef HAL_RTC_MODULE_ENABLED
static int32_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}
static void civil_from_days(int32_t z, int* y, unsigned* m, unsigned* d) {
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)yoe + era * 400 + (*m <= 2);
}
static int st_rtc_get(void* ctx, uint32_t* secs) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (!b->rtc) return MCS_HAL_ENOTSUP;
    RTC_TimeTypeDef t; RTC_DateTypeDef d;
    int r = st_err(HAL_RTC_GetTime(b->rtc, &t, RTC_FORMAT_BIN));
    if (!r) r = st_err(HAL_RTC_GetDate(b->rtc, &d, RTC_FORMAT_BIN));   /* must follow GetTime */
    if (r) return r;
    int32_t days = days_from_civil(2000 + d.Year, d.Month, d.Date);
    *secs = (uint32_t)days * 86400u + t.Hours * 3600u + t.Minutes * 60u + t.Seconds;
    return 0;
}
static int st_rtc_set(void* ctx, uint32_t secs) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if (!b->rtc) return MCS_HAL_ENOTSUP;
    if (secs < 946684800u) return MCS_HAL_EINVAL;      /* RTC calendar starts in 2000 */
    int y; unsigned m, dd;
    int32_t days = (int32_t)(secs / 86400u);
    civil_from_days(days, &y, &m, &dd);
    RTC_TimeTypeDef t; RTC_DateTypeDef d;
    memset(&t, 0, sizeof t); memset(&d, 0, sizeof d);
    uint32_t s = secs % 86400u;
    t.Hours = (uint8_t)(s / 3600); t.Minutes = (uint8_t)(s / 60 % 60); t.Seconds = (uint8_t)(s % 60);
#ifdef RTC_DAYLIGHTSAVING_NONE
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
#endif
    d.Year = (uint8_t)(y - 2000); d.Month = (uint8_t)m; d.Date = (uint8_t)dd;
    d.WeekDay = (uint8_t)((days + 3) % 7 + 1);          /* 1970-01-01 was a Thursday; Monday = 1 */
    int r = st_err(HAL_RTC_SetTime(b->rtc, &t, RTC_FORMAT_BIN));
    return r ? r : st_err(HAL_RTC_SetDate(b->rtc, &d, RTC_FORMAT_BIN));
}
#endif

/* ------------------------------------------------------------------ system */
static uint32_t st_micros(void* ctx) {
    (void)ctx;
#if defined(DWT) && defined(CoreDebug_DEMCR_TRCENA_Msk)
    /* cycle counter, extended across wrap-arounds (call at least once per wrap) */
    static uint32_t last, acc_us, rem;
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        last = 0;
    }
    uint32_t now = DWT->CYCCNT, mhz = SystemCoreClock / 1000000u;
    if (!mhz) mhz = 1;
    rem += now - last;
    last = now;
    acc_us += rem / mhz;
    rem %= mhz;
    return acc_us;
#else
    /* Cortex-M0/M0+: millisecond tick + SysTick sub-millisecond position */
    uint32_t ms, val;
    do { ms = HAL_GetTick(); val = SysTick->VAL; } while (ms != HAL_GetTick());
    uint32_t load = SysTick->LOAD + 1;
    return ms * 1000u + (uint32_t)((uint64_t)(load - val) * 1000u / load);
#endif
}
static void st_delay_us(void* ctx, uint32_t us) {
    uint32_t t0 = st_micros(ctx);
    while (st_micros(ctx) - t0 < us) {}
}
static int st_reset(void* ctx) { (void)ctx; NVIC_SystemReset(); return 0; }
static int st_unique_id(void* ctx, uint8_t* buf, size_t cap) {
    (void)ctx;
#ifdef UID_BASE
    size_t n = cap < 12 ? cap : 12;
    memcpy(buf, (const void*)UID_BASE, n);
    return (int)n;
#else
    (void)buf; (void)cap;
    return MCS_HAL_ENOTSUP;
#endif
}
uint32_t mcs_stm32_ticks(void* ud) { (void)ud; return HAL_GetTick(); }
void mcs_stm32_delay(void* ud, uint32_t ms) { (void)ud; HAL_Delay(ms); }

static int s_pin_lookup(void* ctx, const char* name) {
    mcs_stm32_board_t* b = (mcs_stm32_board_t*)ctx;
    if ((!strcmp(name, "LED") || !strcmp(name, "LED_BUILTIN")) && b && b->led) return mcs_hal_parse_pin(b->led);
    return -1;                       /* -> generic parser: "PA5", "PC13" */
}
void mcs_stm32_hal_init(mcs_hal_t* hal, mcs_stm32_board_t* b) {
    memset(hal, 0, sizeof *hal);
    g_board = b;
    hal->board = b->name ? b->name : "STM32";
    hal->ctx = b;
    hal->gpio_mode = st_gpio_mode;
    hal->gpio_write = st_gpio_write;
    hal->gpio_read = st_gpio_read;
    hal->gpio_irq = st_gpio_irq;
#ifdef HAL_UART_MODULE_ENABLED
    hal->uart_open = st_uart_open;
    hal->uart_config = st_uart_config;
    hal->uart_close = st_uart_close;
    hal->uart_write = st_uart_write;
    hal->uart_read = st_uart_read;
    hal->uart_available = st_uart_available;
#endif
#ifdef HAL_I2C_MODULE_ENABLED
    hal->i2c_open = st_i2c_open;
    hal->i2c_write = st_i2c_write;
    hal->i2c_read = st_i2c_read;
    hal->i2c_write_read = st_i2c_write_read;
    hal->i2c_probe = st_i2c_probe;
#endif
#ifdef HAL_SPI_MODULE_ENABLED
    hal->spi_open = st_spi_open;
    hal->spi_transfer = st_spi_transfer;
#endif
#ifdef HAL_ADC_MODULE_ENABLED
    hal->adc_read = st_adc_read;
    hal->adc_bits = b->adc_bits ? b->adc_bits : 12;
    hal->adc_vref_mv = b->adc_vref_mv ? b->adc_vref_mv : 3300;
#endif
#ifdef MCS_STM32_HAS_DAC
    if (b->dac) { hal->dac_write = st_dac_write; hal->dac_bits = 12; }
#endif
#ifdef HAL_TIM_MODULE_ENABLED
    hal->pwm_set = st_pwm_set;
    hal->pwm_set16 = st_pwm_set16;
    hal->pwm_stop = st_pwm_stop;
    hal->timer_start = st_timer_start;
    hal->timer_stop = st_timer_stop;
#endif
#ifdef MCS_STM32_HAS_I2S
    hal->i2s_open = st_i2s_open;
    hal->i2s_write = st_i2s_write;
    hal->i2s_read = st_i2s_read;
    hal->i2s_close = st_i2s_close;
#endif
#if defined(MCS_STM32_HAS_QSPI) || defined(MCS_STM32_HAS_OSPI)
    hal->qspi_open = st_qspi_open;
    hal->qspi_command = st_qspi_command;
#endif
#if defined(MCS_STM32_HAS_CAN) || defined(MCS_STM32_HAS_FDCAN)
    hal->can_open = st_can_open;
    hal->can_send = st_can_send;
    hal->can_recv = st_can_recv;
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
    hal->wdt_start = st_wdt_start;
    hal->wdt_feed = st_wdt_feed;
#endif
#ifdef HAL_RTC_MODULE_ENABLED
    if (b->rtc) { hal->rtc_get = st_rtc_get; hal->rtc_set = st_rtc_set; }
#endif
    hal->micros = st_micros;
    hal->delay_us = st_delay_us;
    hal->reset = st_reset;
    hal->unique_id = st_unique_id;
    hal->pin_lookup = s_pin_lookup;
    hal->cpu_hz = SystemCoreClock;
}
