/* MicroCS port for the Arduino API. See mcs_port_arduino.h. */
#include "mcs_port_arduino.h"
#include <string.h>
#if defined(ARDUINO_ARCH_AVR)
#include <avr/wdt.h>
#endif
#if defined(ESP32)
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 5
#include "soc/soc_caps.h"
#include "esp_task_wdt.h"
#if SOC_I2S_SUPPORTED
#include "driver/i2s_std.h"
#endif
#if SOC_TWAI_SUPPORTED
#include "driver/twai.h"
#endif
#endif
#endif
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
#include "pico/unique_id.h"
#endif

static mcs_arduino_cfg_t g_cfg;

/* ------------------------------------------------------------------ GPIO */
static int a_gpio_mode(void*, int pin, int mode) {
    if (pin < 0) return MCS_HAL_EINVAL;
    switch (mode) {
    case MCS_GPIO_INPUT: pinMode(pin, INPUT); break;
    case MCS_GPIO_OUTPUT: pinMode(pin, OUTPUT); break;
    case MCS_GPIO_INPUT_PULLUP: pinMode(pin, INPUT_PULLUP); break;
#ifdef INPUT_PULLDOWN
    case MCS_GPIO_INPUT_PULLDOWN: pinMode(pin, INPUT_PULLDOWN); break;
#endif
#ifdef OUTPUT_OPEN_DRAIN
    case MCS_GPIO_OPEN_DRAIN: pinMode(pin, OUTPUT_OPEN_DRAIN); break;
#endif
    case MCS_GPIO_ANALOG: pinMode(pin, INPUT); break;
    default: return MCS_HAL_ENOTSUP;
    }
    return 0;
}
static int a_gpio_write(void*, int pin, int v) { digitalWrite(pin, v ? HIGH : LOW); return 0; }
static int a_gpio_read(void*, int pin) { return digitalRead(pin) == HIGH ? 1 : 0; }

static volatile int g_irq_pin[MCS_ARDUINO_IRQS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
template <int N> static void irq_tramp() {
    int p = g_irq_pin[N];
    if (p >= 0) mcs_hal_post(MCS_HAL_EV_GPIO, p, digitalRead(p) == HIGH);
}
typedef void (*irq_fn)();
static const irq_fn g_tramps[8] = { irq_tramp<0>, irq_tramp<1>, irq_tramp<2>, irq_tramp<3>,
                                     irq_tramp<4>, irq_tramp<5>, irq_tramp<6>, irq_tramp<7> };
static int a_gpio_irq(void*, int pin, int edge) {
    int slot = -1;
    for (int i = 0; i < MCS_ARDUINO_IRQS; i++) if (g_irq_pin[i] == pin) slot = i;
    int irq = digitalPinToInterrupt(pin);
    if (irq < 0) return MCS_HAL_ENOTSUP;
    if (!edge) {
        if (slot >= 0) { detachInterrupt(irq); g_irq_pin[slot] = -1; }
        return 0;
    }
    if (slot < 0) for (int i = 0; i < MCS_ARDUINO_IRQS; i++) if (g_irq_pin[i] < 0) { slot = i; break; }
    if (slot < 0) return MCS_HAL_EBUSY;
    g_irq_pin[slot] = pin;
    attachInterrupt(irq, g_tramps[slot], edge == MCS_GPIO_EDGE_RISING ? RISING : edge == MCS_GPIO_EDGE_FALLING ? FALLING : CHANGE);
    return 0;
}
static int a_pin_lookup(void*, const char* s) {
    if (!strcmp(s, "NEOPIXEL") || !strcmp(s, "RGB_LED") || !strcmp(s, "WS2812")) {   /* on-board RGB LED */
#if defined(PIN_NEOPIXEL)
        return PIN_NEOPIXEL;
#elif defined(PIN_RGB_LED)
        return PIN_RGB_LED;
#else
        return -1;
#endif
    }
    if (!strcmp(s, "LED") || !strcmp(s, "LED_BUILTIN")) {
#ifdef LED_BUILTIN
        return LED_BUILTIN;
#else
        return -1;
#endif
    }
    if ((s[0] == 'A' || s[0] == 'a') && s[1] >= '0' && s[1] <= '9' && (!s[2] || (s[2] >= '0' && s[2] <= '9' && !s[3]))) {
        int n = atoi(s + 1);
#ifdef PIN_A0
        static const int pins[] = { PIN_A0,
#ifdef PIN_A1
            PIN_A1,
#endif
#ifdef PIN_A2
            PIN_A2,
#endif
#ifdef PIN_A3
            PIN_A3,
#endif
#ifdef PIN_A4
            PIN_A4,
#endif
#ifdef PIN_A5
            PIN_A5,
#endif
        };
        return n < (int)(sizeof pins / sizeof pins[0]) ? pins[n] : -1;
#elif defined(A0)
        return n == 0 ? (int)A0 : (int)A0 + n;     /* consecutive on most cores */
#else
        return -1;
#endif
    }
    return mcs_hal_parse_pin(s);
}

/* ------------------------------------------------------------------ UART */
static const mcs_arduino_uart_t* uart_of(int port) {
    return port >= 0 && port < MCS_ARDUINO_UARTS && g_cfg.uart[port].stream ? &g_cfg.uart[port] : NULL;
}
static int a_uart_open(void*, int port, uint32_t baud) {
    const mcs_arduino_uart_t* u = uart_of(port);
    if (!u) return MCS_HAL_ENOTSUP;
    if (u->begin) u->begin(u->stream, baud);
    return 0;
}
static int a_uart_close(void*, int port) {
    const mcs_arduino_uart_t* u = uart_of(port);
    if (!u) return MCS_HAL_ENOTSUP;
    if (u->end) u->end(u->stream);
    return 0;
}
static int a_uart_write(void*, int port, const uint8_t* d, size_t n) {
    const mcs_arduino_uart_t* u = uart_of(port);
    return u ? (int)u->stream->write(d, n) : MCS_HAL_ENOTSUP;
}
static int stream_read(Stream* s, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    uint32_t t0 = millis();
    size_t k = 0;
    while (k < n) {
        int c = s->read();
        if (c >= 0) { buf[k++] = (uint8_t)c; continue; }
        if (k || millis() - t0 >= timeout_ms) break;
        yield();
    }
    return (int)k;
}
static int a_uart_read(void*, int port, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    const mcs_arduino_uart_t* u = uart_of(port);
    return u ? stream_read(u->stream, buf, n, timeout_ms) : MCS_HAL_ENOTSUP;
}
static int a_uart_available(void*, int port) {
    const mcs_arduino_uart_t* u = uart_of(port);
    return u ? u->stream->available() : MCS_HAL_ENOTSUP;
}

/* ------------------------------------------------------------------ I2C */
#ifndef MCS_ARDUINO_NO_WIRE
static bool g_wire_on[2];
static TwoWire* wire_of(int bus) {
    TwoWire* w = bus >= 0 && bus < 2 ? g_cfg.i2c[bus] : NULL;
    if (w && !g_wire_on[bus]) { w->begin(); g_wire_on[bus] = true; }
    return w;
}
static int wire_rc(uint8_t e) { return e == 0 ? 0 : e == 2 || e == 3 ? MCS_HAL_ENODEV : e == 5 ? MCS_HAL_ETIMEOUT : MCS_HAL_ERR; }
static int a_i2c_open(void*, int bus, uint32_t freq) {
    TwoWire* w = wire_of(bus);
    if (!w) return MCS_HAL_ENOTSUP;
    if (freq) w->setClock(freq);
    return 0;
}
static int i2c_tx(TwoWire* w, int addr, const uint8_t* d, size_t n, bool stop) {
    w->beginTransmission((uint8_t)addr);
    if (n) w->write(d, n);
    return wire_rc(w->endTransmission(stop));
}
static int i2c_rx(TwoWire* w, int addr, uint8_t* buf, size_t n) {
    size_t got = (size_t)w->requestFrom((int)addr, (int)n);
    if (got < n) return MCS_HAL_ENODEV;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)w->read();
    return 0;
}
static int a_i2c_write(void*, int bus, int addr, const uint8_t* d, size_t n) {
    TwoWire* w = wire_of(bus);
    return w ? i2c_tx(w, addr, d, n, true) : MCS_HAL_ENOTSUP;
}
static int a_i2c_read(void*, int bus, int addr, uint8_t* buf, size_t n) {
    TwoWire* w = wire_of(bus);
    return w ? i2c_rx(w, addr, buf, n) : MCS_HAL_ENOTSUP;
}
static int a_i2c_write_read(void*, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    TwoWire* w = wire_of(bus);
    if (!w) return MCS_HAL_ENOTSUP;
    int r = i2c_tx(w, addr, tx, tn, false);     /* repeated start */
    return r ? r : i2c_rx(w, addr, rx, rn);
}
static int a_i2c_probe(void*, int bus, int addr) {
    TwoWire* w = wire_of(bus);
    return w ? i2c_tx(w, addr, NULL, 0, true) : MCS_HAL_ENOTSUP;
}
#endif

/* ------------------------------------------------------------------ SPI */
#ifndef MCS_ARDUINO_NO_SPI
static bool g_spi_on[2];
static uint32_t g_spi_hz[2] = { 1000000, 1000000 };
static uint8_t g_spi_mode[2], g_spi_lsb[2];
static SPIClass* spi_of(int bus) {
    SPIClass* s = bus >= 0 && bus < 2 ? g_cfg.spi[bus] : NULL;
    if (s && !g_spi_on[bus]) { s->begin(); g_spi_on[bus] = true; }
    return s;
}
static int a_spi_open(void*, int bus, const mcs_spi_cfg_t* c) {
    if (!spi_of(bus)) return MCS_HAL_ENOTSUP;
    if (c->bits != 8) return MCS_HAL_ENOTSUP;
    g_spi_hz[bus] = c->freq_hz ? c->freq_hz : 1000000;
    g_spi_mode[bus] = c->mode;
    g_spi_lsb[bus] = c->lsb_first;
    return 0;
}
static int a_spi_transfer(void*, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    SPIClass* s = spi_of(bus);
    if (!s) return MCS_HAL_ENOTSUP;
    static const uint8_t modes[4] = { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };
    s->beginTransaction(SPISettings(g_spi_hz[bus], g_spi_lsb[bus] ? LSBFIRST : MSBFIRST, modes[g_spi_mode[bus] & 3]));
    for (size_t i = 0; i < n; i++) {
        uint8_t b = s->transfer(tx ? tx[i] : 0xFF);
        if (rx) rx[i] = b;
    }
    s->endTransaction();
    return 0;
}
#endif

/* ------------------------------------------------------------------ ADC / PWM / DAC */
static int a_adc_read(void*, int pin) { return analogRead(pin); }
static int a_pwm_set16(void*, int pin, uint32_t freq, uint16_t duty) {
    if (!freq) return MCS_HAL_EINVAL;
#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    analogWriteResolution((uint8_t)pin, 12);              /* 12-bit duty up to ~19 kHz */
    analogWriteFrequency((uint8_t)pin, freq);
    analogWrite(pin, (int)((uint32_t)duty * 4095u / 65535u));
#elif defined(ESP32)
    analogWriteFrequency(freq);
    analogWrite(pin, (int)((uint32_t)duty * 255u / 65535u));
#elif defined(TEENSYDUINO)
    analogWriteFrequency((uint8_t)pin, (float)freq);
    analogWriteResolution(16);
    analogWrite(pin, duty);
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
    analogWriteFreq(freq);
    analogWriteRange(65535);
    analogWrite(pin, duty);
#else
    (void)freq;           /* the core's fixed PWM frequency */
    analogWrite(pin, (int)((uint32_t)duty * 255u / 65535u));
#endif
    return 0;
}
static int a_pwm_set(void* ctx, int pin, uint32_t freq, uint16_t permille) {
    return a_pwm_set16(ctx, pin, freq, (uint16_t)((uint32_t)permille * 65535u / 1000u));
}
static int a_pwm_stop(void*, int pin) { analogWrite(pin, 0); return 0; }
#if defined(ESP32) && (defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2))
static int a_dac_write(void*, int ch, uint32_t v) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    int pin = ch == 0 ? 25 : ch == 1 ? 26 : -1;
#else
    int pin = ch == 0 ? 17 : ch == 1 ? 18 : -1;
#endif
    if (pin < 0) return MCS_HAL_ENOTSUP;
    dacWrite((uint8_t)pin, (uint8_t)(v > 255 ? 255 : v));
    return 0;
}
#define MCS_ARDUINO_HAS_DAC 8
#elif defined(DAC0) && !defined(ESP32)
static int a_dac_write(void*, int ch, uint32_t v) {
    if (ch != 0) return MCS_HAL_ENOTSUP;
    analogWrite(DAC0, (int)v);       /* SAMD / Due: 8-bit default resolution */
    return 0;
}
#define MCS_ARDUINO_HAS_DAC 8
#endif

/* ------------------------------------------------------------------ pins for CAN / I2S */
static int g_can_tx = -1, g_can_rx = -1;
static struct { int bclk, ws, dout, din, mclk; } g_i2s_pins[2] = { { -1, -1, -1, -1, -1 }, { -1, -1, -1, -1, -1 } };
void mcs_arduino_can_pins(int tx, int rx) { g_can_tx = tx; g_can_rx = rx; }
void mcs_arduino_i2s_pins(int bus, int bclk, int ws, int dout, int din, int mclk) {
    if (bus < 0 || bus > 1) return;
    g_i2s_pins[bus].bclk = bclk; g_i2s_pins[bus].ws = ws; g_i2s_pins[bus].dout = dout;
    g_i2s_pins[bus].din = din; g_i2s_pins[bus].mclk = mclk;
}

/* ------------------------------------------------------------------ ESP32: I2S, CAN (TWAI), watchdog */
#if defined(ESP32) && defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
#define MCS_ARDUINO_ESP_IDF5 1
static int esp_rc(esp_err_t e) {
    return e == ESP_OK ? 0 : e == ESP_ERR_TIMEOUT ? MCS_HAL_ETIMEOUT : e == ESP_ERR_INVALID_ARG ? MCS_HAL_EINVAL
         : e == ESP_ERR_NOT_SUPPORTED ? MCS_HAL_ENOTSUP : e == ESP_ERR_INVALID_STATE ? MCS_HAL_EBUSY : MCS_HAL_ERR;
}
static TickType_t ms_ticks(uint32_t ms) { return ms == 0xFFFFFFFFu ? portMAX_DELAY : pdMS_TO_TICKS(ms); }
#if SOC_I2S_SUPPORTED
#define MCS_ARDUINO_I2S 1
static i2s_chan_handle_t g_i2s_tx[2], g_i2s_rx[2];
static int a_i2s_close(void*, int bus) {
    if (bus < 0 || bus >= (int)SOC_I2S_NUM || bus > 1) return MCS_HAL_ENOTSUP;
    if (g_i2s_tx[bus]) { i2s_channel_disable(g_i2s_tx[bus]); i2s_del_channel(g_i2s_tx[bus]); g_i2s_tx[bus] = NULL; }
    if (g_i2s_rx[bus]) { i2s_channel_disable(g_i2s_rx[bus]); i2s_del_channel(g_i2s_rx[bus]); g_i2s_rx[bus] = NULL; }
    return 0;
}
static int a_i2s_open(void* ctx, int bus, const mcs_i2s_cfg_t* c) {
    if (bus < 0 || bus >= (int)SOC_I2S_NUM || bus > 1 || g_i2s_pins[bus].bclk < 0) return MCS_HAL_ENOTSUP;
    a_i2s_close(ctx, bus);
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)bus, I2S_ROLE_MASTER);
    uint8_t dir = c->direction ? c->direction : MCS_I2S_TX;
    esp_err_t e = i2s_new_channel(&cc, (dir & MCS_I2S_TX) ? &g_i2s_tx[bus] : NULL, (dir & MCS_I2S_RX) ? &g_i2s_rx[bus] : NULL);
    if (e != ESP_OK) return esp_rc(e);
    i2s_data_bit_width_t bw = c->bits == 16 ? I2S_DATA_BIT_WIDTH_16BIT : c->bits == 24 ? I2S_DATA_BIT_WIDTH_24BIT : I2S_DATA_BIT_WIDTH_32BIT;
    i2s_slot_mode_t sm = c->channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_config_t sc;
    memset(&sc, 0, sizeof sc);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(c->sample_rate);
    sc.clk_cfg = clk;
    if (c->format == MCS_I2S_MSB) { i2s_std_slot_config_t s = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(bw, sm); sc.slot_cfg = s; }
    else if (c->format == MCS_I2S_PCM) { i2s_std_slot_config_t s = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(bw, sm); sc.slot_cfg = s; }
    else { i2s_std_slot_config_t s = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bw, sm); sc.slot_cfg = s; }
    sc.gpio_cfg.mclk = (gpio_num_t)g_i2s_pins[bus].mclk;
    sc.gpio_cfg.bclk = (gpio_num_t)g_i2s_pins[bus].bclk;
    sc.gpio_cfg.ws = (gpio_num_t)g_i2s_pins[bus].ws;
    sc.gpio_cfg.dout = (gpio_num_t)g_i2s_pins[bus].dout;
    sc.gpio_cfg.din = (gpio_num_t)g_i2s_pins[bus].din;
    i2s_chan_handle_t hs[2] = { g_i2s_tx[bus], g_i2s_rx[bus] };
    for (int k = 0; k < 2; k++) {
        if (!hs[k]) continue;
        if ((e = i2s_channel_init_std_mode(hs[k], &sc)) != ESP_OK || (e = i2s_channel_enable(hs[k])) != ESP_OK) {
            a_i2s_close(ctx, bus);
            return esp_rc(e);
        }
    }
    return 0;
}
static int a_i2s_write(void*, int bus, const uint8_t* d, size_t n, uint32_t timeout_ms) {
    if (bus < 0 || bus > 1 || !g_i2s_tx[bus]) return MCS_HAL_ENOTSUP;
    size_t done = 0;
    esp_err_t e = i2s_channel_write(g_i2s_tx[bus], d, n, &done, timeout_ms);
    return e == ESP_OK || (e == ESP_ERR_TIMEOUT && done) ? (int)done : esp_rc(e);
}
static int a_i2s_read(void*, int bus, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    if (bus < 0 || bus > 1 || !g_i2s_rx[bus]) return MCS_HAL_ENOTSUP;
    size_t done = 0;
    esp_err_t e = i2s_channel_read(g_i2s_rx[bus], buf, n, &done, timeout_ms);
    return e == ESP_OK || (e == ESP_ERR_TIMEOUT && done) ? (int)done : esp_rc(e);
}
#endif
#if SOC_TWAI_SUPPORTED
#define MCS_ARDUINO_CAN 1
static bool g_twai;
static int a_can_open(void*, int bus, uint32_t bitrate) {
    if (bus != 0 || g_can_tx < 0 || g_can_rx < 0) return MCS_HAL_ENOTSUP;
    if (g_twai) { twai_stop(); twai_driver_uninstall(); g_twai = false; }
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)g_can_tx, (gpio_num_t)g_can_rx, TWAI_MODE_NORMAL);
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_timing_config_t t;
    switch (bitrate) {
    case 25000:   { twai_timing_config_t x = TWAI_TIMING_CONFIG_25KBITS(); t = x; break; }
    case 50000:   { twai_timing_config_t x = TWAI_TIMING_CONFIG_50KBITS(); t = x; break; }
    case 100000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_100KBITS(); t = x; break; }
    case 125000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_125KBITS(); t = x; break; }
    case 250000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_250KBITS(); t = x; break; }
    case 500000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_500KBITS(); t = x; break; }
    case 800000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_800KBITS(); t = x; break; }
    case 1000000: { twai_timing_config_t x = TWAI_TIMING_CONFIG_1MBITS(); t = x; break; }
    default: return MCS_HAL_ENOTSUP;
    }
    esp_err_t e = twai_driver_install(&g, &t, &f);
    if (e == ESP_OK) e = twai_start();
    g_twai = e == ESP_OK;
    return esp_rc(e);
}
static int a_can_send(void*, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    if (bus != 0 || !g_twai) return MCS_HAL_ENOTSUP;
    twai_message_t m;
    memset(&m, 0, sizeof m);
    m.identifier = fr->id;
    m.extd = fr->extended ? 1 : 0;
    m.rtr = fr->rtr ? 1 : 0;
    m.data_length_code = fr->len > 8 ? 8 : fr->len;
    memcpy(m.data, fr->data, m.data_length_code);
    return esp_rc(twai_transmit(&m, ms_ticks(timeout_ms)));
}
static int a_can_recv(void*, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    if (bus != 0 || !g_twai) return MCS_HAL_ENOTSUP;
    twai_message_t m;
    esp_err_t e = twai_receive(&m, ms_ticks(timeout_ms));
    if (e != ESP_OK) return esp_rc(e);
    fr->id = m.identifier;
    fr->extended = m.extd;
    fr->rtr = m.rtr;
    fr->len = m.data_length_code > 8 ? 8 : m.data_length_code;
    memcpy(fr->data, m.data, fr->len);
    return 0;
}
#endif
#define MCS_ARDUINO_WDT 1
static int a_wdt_start(void*, uint32_t timeout_ms) {
    esp_task_wdt_config_t c;
    memset(&c, 0, sizeof c);
    c.timeout_ms = timeout_ms;
    c.idle_core_mask = 0;
    c.trigger_panic = true;
    esp_err_t e = esp_task_wdt_init(&c);
    if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_reconfigure(&c);   /* the core already started it */
    if (e != ESP_OK) return esp_rc(e);
    e = esp_task_wdt_add(NULL);                                         /* watch the task running loop() */
    return e == ESP_ERR_INVALID_ARG ? 0 : esp_rc(e);
}
static int a_wdt_feed(void*) { return esp_rc(esp_task_wdt_reset()); }
static int a_adc_read_mv(void*, int pin) { return (int)analogReadMilliVolts((uint8_t)pin); }
#endif

/* ------------------------------------------------------------------ WS2812 / NeoPixel
 * ESP32 (core 3.x): RMT, RP2040 / RP2350 (Arduino-Pico): PIO - the same drivers as the
 * native ports. Other boards: the Adafruit NeoPixel library when the sketch includes
 * <Adafruit_NeoPixel.h> (the include also tells the IDE to build that library). */
#if defined(ESP32) && ESP_IDF_VERSION_MAJOR >= 5 && SOC_RMT_SUPPORTED
#define MCS_LEDSTRIP_ESP32_RMT 1
#include "mcs_ledstrip_drivers.h"
#define MCS_ARDUINO_LEDSTRIP 1
static int a_ledstrip_write(void*, int pin, const uint8_t* d, size_t n, int) { return mcs_ledstrip_rmt_write(pin, d, n); }
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
#define MCS_LEDSTRIP_RP2_PIO 1
#include "mcs_ledstrip_drivers.h"
#define MCS_ARDUINO_LEDSTRIP 1
static int a_ledstrip_write(void*, int pin, const uint8_t* d, size_t n, int) { return mcs_ledstrip_pio_write(pin, d, n); }
#elif defined(__has_include)
#if __has_include(<Adafruit_NeoPixel.h>)
#include <Adafruit_NeoPixel.h>
#define MCS_ARDUINO_LEDSTRIP 1
static Adafruit_NeoPixel* g_np[2];
static int a_ledstrip_write(void*, int pin, const uint8_t* d, size_t n, int order) {
    if (pin < 0) return MCS_HAL_EINVAL;
    size_t bpp = order == MCS_LED_GRBW ? 4 : 3;
    uint16_t count = (uint16_t)(n / bpp);
    neoPixelType type = (bpp == 4 ? NEO_GRBW : NEO_GRB) + NEO_KHZ800;   /* bytes arrive in wire order */
    int k = 0;
    while (k < 2 && g_np[k] && g_np[k]->getPin() != pin) k++;
    if (k == 2) return MCS_HAL_EBUSY;
    if (!g_np[k]) { g_np[k] = new Adafruit_NeoPixel(count, (int16_t)pin, type); if (!g_np[k]) return MCS_HAL_ERR; g_np[k]->begin(); }
    if (g_np[k]->numPixels() != count) g_np[k]->updateLength(count);
    g_np[k]->updateType(type);
    if (!g_np[k]->getPixels()) return MCS_HAL_ERR;
    memcpy(g_np[k]->getPixels(), d, (size_t)count * bpp);
    g_np[k]->show();
    delayMicroseconds(300);
    return 0;
}
#endif
#endif
/* ------------------------------------------------------------------ RP2040 / RP2350 (Arduino-Pico): I2S, watchdog */
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
#define MCS_ARDUINO_WDT 1
static int a_wdt_start(void*, uint32_t ms) { rp2040.wdt_begin(ms > 8300 ? 8300 : ms); return 0; }
static int a_wdt_feed(void*) { rp2040.wdt_reset(); return 0; }
#if defined(__has_include)
#if __has_include(<I2S.h>)
#include <I2S.h>
#define MCS_ARDUINO_I2S 1
static I2S* g_i2s[2];
static int a_i2s_close(void*, int bus) {
    if (bus < 0 || bus > 1) return MCS_HAL_ENOTSUP;
    if (g_i2s[bus]) { g_i2s[bus]->end(); delete g_i2s[bus]; g_i2s[bus] = NULL; }
    return 0;
}
static int a_i2s_open(void* ctx, int bus, const mcs_i2s_cfg_t* c) {
    if (bus < 0 || bus > 1 || g_i2s_pins[bus].bclk < 0) return MCS_HAL_ENOTSUP;
    a_i2s_close(ctx, bus);
    uint8_t dir = c->direction ? c->direction : MCS_I2S_TX;
    if (dir == MCS_I2S_DUPLEX) return MCS_HAL_ENOTSUP;
    int data = dir == MCS_I2S_RX ? g_i2s_pins[bus].din : g_i2s_pins[bus].dout;
    if (data < 0) return MCS_HAL_EINVAL;
    I2S* s = new I2S(dir == MCS_I2S_RX ? INPUT : OUTPUT);
    if (!s) return MCS_HAL_ERR;
    s->setBCLK((pin_size_t)g_i2s_pins[bus].bclk);        /* WS (LRCLK) is BCLK + 1 */
    s->setDATA((pin_size_t)data);
    if (g_i2s_pins[bus].mclk >= 0) s->setMCLK((pin_size_t)g_i2s_pins[bus].mclk);
    s->setBitsPerSample(c->bits ? c->bits : 16);
    if (c->format == MCS_I2S_MSB) s->setLSBJFormat();
    if (!s->begin((long)c->sample_rate)) { delete s; return MCS_HAL_EINVAL; }
    g_i2s[bus] = s;
    return 0;
}
static int a_i2s_write(void*, int bus, const uint8_t* d, size_t n, uint32_t) {
    if (bus < 0 || bus > 1 || !g_i2s[bus]) return MCS_HAL_ENOTSUP;
    return (int)g_i2s[bus]->write(d, n);
}
static int a_i2s_read(void*, int bus, uint8_t* buf, size_t n, uint32_t) {
    if (bus < 0 || bus > 1 || !g_i2s[bus]) return MCS_HAL_ENOTSUP;
    return (int)g_i2s[bus]->read(buf, n);
}
#endif
#endif
#endif

/* ------------------------------------------------------------------ software timers (polled) */
static struct { uint32_t period_us, next_us, count; bool on, periodic; } g_tim[MCS_ARDUINO_TIMERS];
static int a_timer_start(void*, int i, uint32_t period_us, int periodic) {
    if (i < 0 || i >= MCS_ARDUINO_TIMERS) return MCS_HAL_ENOTSUP;
    if (!period_us) return MCS_HAL_EINVAL;
    g_tim[i].period_us = period_us;
    g_tim[i].next_us = micros() + period_us;
    g_tim[i].count = 0;
    g_tim[i].periodic = periodic != 0;
    g_tim[i].on = true;
    return 0;
}
static int a_timer_stop(void*, int i) {
    if (i < 0 || i >= MCS_ARDUINO_TIMERS) return MCS_HAL_ENOTSUP;
    g_tim[i].on = false;
    return 0;
}
static int g_uart_seen[MCS_ARDUINO_UARTS];
#if defined(MCS_ARDUINO_CAN)
static uint32_t g_can_seen;
#endif
static int a_poll_event(void*, mcs_hal_event_t* ev) {
    /* UART.OnReceive / CAN.OnReceive: report newly arrived data (polled - Arduino has no RX hook) */
    for (int p = 0; p < MCS_ARDUINO_UARTS; p++) {
        if (!g_cfg.uart[p].stream) continue;
        int n = g_cfg.uart[p].stream->available();
        if (n > g_uart_seen[p]) {
            g_uart_seen[p] = n;
            ev->type = MCS_HAL_EV_UART; ev->reserved = 0; ev->source = (uint16_t)p; ev->value = n;
            return 1;
        }
        if (n < g_uart_seen[p]) g_uart_seen[p] = n;
    }
#if defined(MCS_ARDUINO_CAN)
    if (g_twai) {
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            if (st.msgs_to_rx > g_can_seen) {
                g_can_seen = st.msgs_to_rx;
                ev->type = MCS_HAL_EV_CAN; ev->reserved = 0; ev->source = 0; ev->value = (int32_t)st.msgs_to_rx;
                return 1;
            }
            if (st.msgs_to_rx < g_can_seen) g_can_seen = st.msgs_to_rx;
        }
    }
#endif
    uint32_t now = micros();
    for (int i = 0; i < MCS_ARDUINO_TIMERS; i++) {
        if (!g_tim[i].on || (int32_t)(now - g_tim[i].next_us) < 0) continue;
        g_tim[i].count++;
        if (g_tim[i].periodic) {
            g_tim[i].next_us += g_tim[i].period_us;
            if ((int32_t)(now - g_tim[i].next_us) > 0) g_tim[i].next_us = now + g_tim[i].period_us;   /* fell behind: skip */
        } else g_tim[i].on = false;
        ev->type = MCS_HAL_EV_TIMER; ev->reserved = 0; ev->source = (uint16_t)i; ev->value = (int32_t)g_tim[i].count;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ system */
#if defined(ARDUINO_ARCH_AVR)
static int a_wdt_start(void*, uint32_t ms) {
    wdt_enable(ms >= 8000 ? WDTO_8S : ms >= 4000 ? WDTO_4S : ms >= 2000 ? WDTO_2S : ms >= 1000 ? WDTO_1S : ms >= 500 ? WDTO_500MS : WDTO_250MS);
    return 0;
}
static int a_wdt_feed(void*) { wdt_reset(); return 0; }
#endif
static int64_t g_rtc_offset;
static int a_rtc_get(void*, uint32_t* s) { *s = (uint32_t)(g_rtc_offset + (int64_t)(millis() / 1000)); return 0; }
static int a_rtc_set(void*, uint32_t s) { g_rtc_offset = (int64_t)s - (int64_t)(millis() / 1000); return 0; }
static uint32_t a_micros(void*) { return micros(); }
static void a_delay_us(void*, uint32_t us) { delayMicroseconds(us); }
static int a_reset(void*) {
#if defined(ESP32) || defined(ESP8266)
    ESP.restart();
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
    rp2040.reboot();
#elif defined(__ARM_ARCH) && defined(__CORTEX_M)
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_AVR)
    wdt_enable(WDTO_15MS); for (;;) {}
#endif
    return MCS_HAL_ENOTSUP;
}
static int a_unique_id(void*, uint8_t* buf, size_t cap) {
#if defined(ESP32)
    uint64_t mac = ESP.getEfuseMac();
    size_t n = cap < 6 ? cap : 6;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(mac >> (8 * i));
    return (int)n;
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    size_t n = cap < PICO_UNIQUE_BOARD_ID_SIZE_BYTES ? cap : PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
    memcpy(buf, id.id, n);
    return (int)n;
#elif defined(NRF52_SERIES) || defined(NRF52) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_ARCH_NRF52840)
    uint32_t w[2] = { NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1] };
    size_t n = cap < 8 ? cap : 8;
    memcpy(buf, w, n);
    return (int)n;
#elif defined(__SAMD51__)
    uint32_t w[4] = { *(volatile uint32_t*)0x008061FC, *(volatile uint32_t*)0x00806010, *(volatile uint32_t*)0x00806014, *(volatile uint32_t*)0x00806018 };
    size_t n = cap < 16 ? cap : 16;
    memcpy(buf, w, n);
    return (int)n;
#elif defined(__SAMD21__) || defined(ARDUINO_ARCH_SAMD)
    uint32_t w[4] = { *(volatile uint32_t*)0x0080A00C, *(volatile uint32_t*)0x0080A040, *(volatile uint32_t*)0x0080A044, *(volatile uint32_t*)0x0080A048 };
    size_t n = cap < 16 ? cap : 16;
    memcpy(buf, w, n);
    return (int)n;
#elif defined(ARDUINO_ARCH_STM32) && defined(UID_BASE)
    uint32_t w[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
    size_t n = cap < 12 ? cap : 12;
    memcpy(buf, w, n);
    return (int)n;
#else
    (void)buf; (void)cap;
    return MCS_HAL_ENOTSUP;
#endif
}

extern "C" uint32_t mcs_arduino_ticks(void*) { return millis(); }
extern "C" void mcs_arduino_delay(void*, uint32_t ms) { delay(ms); }

static int con_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) { return stream_read((Stream*)ud, buf, n, timeout_ms); }
static void con_write(void* ud, const char* s, size_t n) { ((Stream*)ud)->write((const uint8_t*)s, n); }
mcs_transport_t mcs_arduino_console(Stream* s) {
    mcs_transport_t t = { con_read, con_write, s };
    return t;
}

void mcs_arduino_hal_init(mcs_hal_t* hal, const mcs_arduino_cfg_t* cfg) {
    if (cfg) g_cfg = *cfg;
    memset(hal, 0, sizeof *hal);
    hal->board = g_cfg.name ? g_cfg.name : "Arduino";
    hal->ctx = &g_cfg;
    hal->pin_lookup = a_pin_lookup;
    hal->gpio_mode = a_gpio_mode;
    hal->gpio_write = a_gpio_write;
    hal->gpio_read = a_gpio_read;
    hal->gpio_irq = a_gpio_irq;
    hal->uart_open = a_uart_open;
    hal->uart_close = a_uart_close;
    hal->uart_write = a_uart_write;
    hal->uart_read = a_uart_read;
    hal->uart_available = a_uart_available;
#ifndef MCS_ARDUINO_NO_WIRE
    hal->i2c_open = a_i2c_open;
    hal->i2c_write = a_i2c_write;
    hal->i2c_read = a_i2c_read;
    hal->i2c_write_read = a_i2c_write_read;
    hal->i2c_probe = a_i2c_probe;
#endif
#ifndef MCS_ARDUINO_NO_SPI
    hal->spi_open = a_spi_open;
    hal->spi_transfer = a_spi_transfer;
#endif
    hal->adc_read = a_adc_read;
#ifdef MCS_ARDUINO_ADC_BITS
    analogReadResolution(MCS_ARDUINO_ADC_BITS);
    hal->adc_bits = MCS_ARDUINO_ADC_BITS;
#elif defined(ESP32)
    hal->adc_bits = 12;
#else
    hal->adc_bits = 10;              /* Arduino default */
#endif
    hal->pwm_set = a_pwm_set;
    hal->pwm_set16 = a_pwm_set16;
    hal->pwm_stop = a_pwm_stop;
#ifdef MCS_ARDUINO_HAS_DAC
    hal->dac_write = a_dac_write;
    hal->dac_bits = MCS_ARDUINO_HAS_DAC;
#endif
    hal->timer_start = a_timer_start;
    hal->timer_stop = a_timer_stop;
    hal->poll_event = a_poll_event;
#if defined(ARDUINO_ARCH_AVR) || defined(MCS_ARDUINO_WDT)
    hal->wdt_start = a_wdt_start;
    hal->wdt_feed = a_wdt_feed;
#endif
#ifdef MCS_ARDUINO_I2S
    hal->i2s_open = a_i2s_open;
    hal->i2s_write = a_i2s_write;
    hal->i2s_read = a_i2s_read;
    hal->i2s_close = a_i2s_close;
#endif
#ifdef MCS_ARDUINO_CAN
    hal->can_open = a_can_open;
    hal->can_send = a_can_send;
    hal->can_recv = a_can_recv;
#endif
#ifdef MCS_ARDUINO_ESP_IDF5
    hal->adc_read_mv = a_adc_read_mv;
#endif
    hal->rtc_get = a_rtc_get;
    hal->rtc_set = a_rtc_set;
    hal->micros = a_micros;
    hal->delay_us = a_delay_us;
    hal->reset = a_reset;
    hal->unique_id = a_unique_id;
#ifdef MCS_ARDUINO_LEDSTRIP
    hal->ledstrip_write = a_ledstrip_write;
#endif
#if defined(ESP32)
    hal->cpu_hz = (uint32_t)getCpuFrequencyMhz() * 1000000u;
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
    hal->cpu_hz = (uint32_t)rp2040.f_cpu();
#elif defined(F_CPU)
    hal->cpu_hz = (uint32_t)F_CPU;
#endif
}
