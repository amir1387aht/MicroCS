/* MicroCS port for the Arduino API. See mcs_port_arduino.h. */
#include "mcs_port_arduino.h"
#include <string.h>
#if defined(ARDUINO_ARCH_AVR)
#include <avr/wdt.h>
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
#if defined(ESP32)
    analogWriteFrequency((uint8_t)pin, freq);
#elif defined(TEENSYDUINO)
    analogWriteFrequency((uint8_t)pin, (float)freq);
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
    analogWriteFreq(freq);
#else
    (void)freq;           /* the core's fixed PWM frequency */
#endif
    analogWrite(pin, (int)((uint32_t)duty * 255u / 65535u));
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
static int a_poll_event(void*, mcs_hal_event_t* ev) {
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
#if defined(ARDUINO_ARCH_AVR)
    hal->wdt_start = a_wdt_start;
    hal->wdt_feed = a_wdt_feed;
#endif
    hal->rtc_get = a_rtc_get;
    hal->rtc_set = a_rtc_set;
    hal->micros = a_micros;
    hal->delay_us = a_delay_us;
    hal->reset = a_reset;
    hal->unique_id = a_unique_id;
#ifdef F_CPU
    hal->cpu_hz = (uint32_t)F_CPU;
#endif
}
