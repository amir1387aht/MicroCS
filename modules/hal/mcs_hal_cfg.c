/*
 * MicroCS - peripheral configuration objects (HAL API v3).
 *
 * C#:  var cfg = new SpiConfig { Frequency = 20_000_000, Sck = 6, Mosi = 7, Miso = 2, Dma = true };
 *      cfg.Set("queue_size", 4);          // a port-specific option (Hal.Options("SPI"))
 *      SPI.Open(1, cfg);
 * Every property starts at "board default" (0 / null / -1 for pins); only what
 * a script sets is sent to the board. GpioConfig UartConfig I2cConfig SpiConfig
 * QspiConfig PwmConfig AdcConfig wrap the mcs_*_config_t structs of mcs_hal.h.
 */
#include "mcs_hal.h"
#include "mcs_hal_internal.h"
#if MCS_ENABLE_HAL
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

enum { T_U8, T_I8, T_U16, T_I16, T_U32, T_BOOL, T_TRI, T_PIN };
typedef struct { const char* name; uint16_t off; uint8_t type; int32_t lo, hi, def; } fld_t;
typedef struct {
    const char* name;            /* "SpiConfig" */
    const char* periph;          /* "SPI": option namespace */
    uint8_t kind, nf;
    uint16_t opts_off;
    const fld_t* f;
    const uint8_t* ctor;         /* fields filled by positional constructor arguments, 0xFF ends */
} cfg_cls_t;
typedef struct {
    uint8_t kind;
    union {
        mcs_gpio_config_t gpio; mcs_uart_config_t uart; mcs_i2c_config_t i2c; mcs_spi_config_t spi;
        mcs_qspi_config_t qspi; mcs_pwm_config_t pwm; mcs_adc_config_t adc; mcs_can_config_t can;
    } u;
} cfg_obj_t;

#define MAXI 0x7FFFFFFF
#define X_FLD(P, i, n, T, m, ty, lo, hi, df) { n, (uint16_t)offsetof(T, m), ty, lo, hi, df },
#define X_ACC(P, i, ...) \
    NATIVE(P##_g##i) { return cfg_getf(vm, self, i); } \
    NATIVE(P##_s##i) { cfg_setf(vm, self, i, argv[0]); return mcs_null(); }
#define X_MEM(P, i, n, ...) MCS_GET(n, P##_g##i), MCS_SET(n, P##_s##i),

#define GPIO_FIELDS(X) \
    X(gpio, 0, "Mode", mcs_gpio_config_t, mode, T_U8, 0, MCS_GPIO_ANALOG, 0) \
    X(gpio, 1, "Pull", mcs_gpio_config_t, pull, T_U8, 0, MCS_PULL_BOTH, 0) \
    X(gpio, 2, "Speed", mcs_gpio_config_t, speed, T_U8, 0, MCS_SPEED_VERY_HIGH, 0) \
    X(gpio, 3, "DriveMilliamps", mcs_gpio_config_t, drive_ma, T_U8, 0, 255, 0) \
    X(gpio, 4, "Schmitt", mcs_gpio_config_t, schmitt, T_TRI, 0, 2, 0) \
    X(gpio, 5, "Hold", mcs_gpio_config_t, hold, T_TRI, 0, 2, 0) \
    X(gpio, 6, "Invert", mcs_gpio_config_t, invert, T_TRI, 0, 2, 0) \
    X(gpio, 7, "Initial", mcs_gpio_config_t, initial, T_I8, -1, 1, -1) \
    X(gpio, 8, "Alternate", mcs_gpio_config_t, alternate, T_I16, -1, 255, -1) \
    X(gpio, 9, "FilterNs", mcs_gpio_config_t, filter_ns, T_U16, 0, 65535, 0)
#define UART_FIELDS(X) \
    X(uart, 0, "Baud", mcs_uart_config_t, frame.baud, T_U32, 1, MAXI, 115200) \
    X(uart, 1, "DataBits", mcs_uart_config_t, frame.data_bits, T_U8, 5, 9, 8) \
    X(uart, 2, "Parity", mcs_uart_config_t, frame.parity, T_U8, 0, 2, 0) \
    X(uart, 3, "StopBits", mcs_uart_config_t, frame.stop_bits, T_U8, 1, 2, 1) \
    X(uart, 4, "FlowControl", mcs_uart_config_t, frame.flow_control, T_BOOL, 0, 1, 0) \
    X(uart, 5, "Tx", mcs_uart_config_t, tx, T_PIN, -1, 32767, -1) \
    X(uart, 6, "Rx", mcs_uart_config_t, rx, T_PIN, -1, 32767, -1) \
    X(uart, 7, "Rts", mcs_uart_config_t, rts, T_PIN, -1, 32767, -1) \
    X(uart, 8, "Cts", mcs_uart_config_t, cts, T_PIN, -1, 32767, -1) \
    X(uart, 9, "RxBuffer", mcs_uart_config_t, rx_buffer, T_U16, 0, 65535, 0) \
    X(uart, 10, "TxBuffer", mcs_uart_config_t, tx_buffer, T_U16, 0, 65535, 0) \
    X(uart, 11, "InvertTx", mcs_uart_config_t, invert_tx, T_BOOL, 0, 1, 0) \
    X(uart, 12, "InvertRx", mcs_uart_config_t, invert_rx, T_BOOL, 0, 1, 0) \
    X(uart, 13, "Rs485", mcs_uart_config_t, rs485, T_BOOL, 0, 1, 0) \
    X(uart, 14, "Dma", mcs_uart_config_t, dma, T_TRI, 0, 2, 0) \
    X(uart, 15, "RxTimeout", mcs_uart_config_t, rx_timeout, T_U16, 0, 65535, 0)
#define I2C_FIELDS(X) \
    X(i2c, 0, "Frequency", mcs_i2c_config_t, freq_hz, T_U32, 1, MAXI, 100000) \
    X(i2c, 1, "Sda", mcs_i2c_config_t, sda, T_PIN, -1, 32767, -1) \
    X(i2c, 2, "Scl", mcs_i2c_config_t, scl, T_PIN, -1, 32767, -1) \
    X(i2c, 3, "PullUps", mcs_i2c_config_t, pullups, T_TRI, 0, 2, 0) \
    X(i2c, 4, "Dma", mcs_i2c_config_t, dma, T_TRI, 0, 2, 0) \
    X(i2c, 5, "TimeoutMs", mcs_i2c_config_t, timeout_ms, T_U16, 0, 65535, 0) \
    X(i2c, 6, "GlitchFilter", mcs_i2c_config_t, glitch_filter, T_U8, 0, 255, 0) \
    X(i2c, 7, "AnalogFilter", mcs_i2c_config_t, analog_filter, T_TRI, 0, 2, 0)
#define SPI_FIELDS(X) \
    X(spi, 0, "Frequency", mcs_spi_config_t, bus.freq_hz, T_U32, 1, MAXI, 1000000) \
    X(spi, 1, "Mode", mcs_spi_config_t, bus.mode, T_U8, 0, 3, 0) \
    X(spi, 2, "Bits", mcs_spi_config_t, bus.bits, T_U8, 1, 32, 8) \
    X(spi, 3, "LsbFirst", mcs_spi_config_t, bus.lsb_first, T_BOOL, 0, 1, 0) \
    X(spi, 4, "Sck", mcs_spi_config_t, sck, T_PIN, -1, 32767, -1) \
    X(spi, 5, "Mosi", mcs_spi_config_t, mosi, T_PIN, -1, 32767, -1) \
    X(spi, 6, "Miso", mcs_spi_config_t, miso, T_PIN, -1, 32767, -1) \
    X(spi, 7, "Cs", mcs_spi_config_t, cs, T_PIN, -1, 32767, -1) \
    X(spi, 8, "CsActiveHigh", mcs_spi_config_t, cs_active_high, T_BOOL, 0, 1, 0) \
    X(spi, 9, "ThreeWire", mcs_spi_config_t, three_wire, T_BOOL, 0, 1, 0) \
    X(spi, 10, "Dma", mcs_spi_config_t, dma, T_TRI, 0, 2, 0) \
    X(spi, 11, "InputDelayNs", mcs_spi_config_t, input_delay_ns, T_U16, 0, 65535, 0) \
    X(spi, 12, "MaxTransfer", mcs_spi_config_t, max_transfer, T_U32, 0, MAXI, 0)
#define QSPI_FIELDS(X) \
    X(qspi, 0, "Frequency", mcs_qspi_config_t, freq_hz, T_U32, 1, MAXI, 10000000) \
    X(qspi, 1, "Mode", mcs_qspi_config_t, mode, T_U8, 0, 3, 0) \
    X(qspi, 2, "Ddr", mcs_qspi_config_t, ddr, T_BOOL, 0, 1, 0) \
    X(qspi, 3, "Dma", mcs_qspi_config_t, dma, T_TRI, 0, 2, 0) \
    X(qspi, 4, "CsHighCycles", mcs_qspi_config_t, cs_high_cycles, T_U8, 0, 255, 0) \
    X(qspi, 5, "SampleShift", mcs_qspi_config_t, sample_shift, T_U8, 0, 255, 0) \
    X(qspi, 6, "FlashSizeLog2", mcs_qspi_config_t, flash_size_log2, T_U8, 0, 32, 0) \
    X(qspi, 7, "Sck", mcs_qspi_config_t, sck, T_PIN, -1, 32767, -1) \
    X(qspi, 8, "Cs", mcs_qspi_config_t, cs, T_PIN, -1, 32767, -1) \
    X(qspi, 9, "Io0", mcs_qspi_config_t, io[0], T_PIN, -1, 32767, -1) \
    X(qspi, 10, "Io1", mcs_qspi_config_t, io[1], T_PIN, -1, 32767, -1) \
    X(qspi, 11, "Io2", mcs_qspi_config_t, io[2], T_PIN, -1, 32767, -1) \
    X(qspi, 12, "Io3", mcs_qspi_config_t, io[3], T_PIN, -1, 32767, -1)
#define PWM_FIELDS(X) \
    X(pwm, 0, "Frequency", mcs_pwm_config_t, freq_hz, T_U32, 0, MAXI, 0) \
    X(pwm, 1, "Pin", mcs_pwm_config_t, pin, T_PIN, -1, 32767, -1) \
    X(pwm, 2, "ResolutionBits", mcs_pwm_config_t, resolution_bits, T_U8, 0, 32, 0) \
    X(pwm, 3, "Inverted", mcs_pwm_config_t, invert, T_BOOL, 0, 1, 0) \
    X(pwm, 4, "CenterAligned", mcs_pwm_config_t, center_aligned, T_BOOL, 0, 1, 0)
#define ADC_FIELDS(X) \
    X(adc, 0, "ResolutionBits", mcs_adc_config_t, resolution_bits, T_U8, 0, 32, 0) \
    X(adc, 1, "Attenuation", mcs_adc_config_t, attenuation, T_U8, 0, MCS_ADC_ATTEN_12DB, 0) \
    X(adc, 2, "Oversample", mcs_adc_config_t, oversample, T_U8, 0, 255, 0) \
    X(adc, 3, "SampleTime", mcs_adc_config_t, sample_time, T_U16, 0, 65535, 0)

#define CAN_FIELDS(X) \
    X(can, 0, "Bitrate", mcs_can_config_t, bitrate, T_U32, 1, 1000000, 500000) \
    X(can, 1, "Tx", mcs_can_config_t, tx, T_PIN, -1, 32767, -1) \
    X(can, 2, "Rx", mcs_can_config_t, rx, T_PIN, -1, 32767, -1) \
    X(can, 3, "Mode", mcs_can_config_t, mode, T_U8, 0, MCS_CAN_LOOPBACK, 0)
static const fld_t can_f[] = { CAN_FIELDS(X_FLD) };
static const uint8_t can_c[] = { 0, 1, 2, 0xFF };
static const fld_t gpio_f[] = { GPIO_FIELDS(X_FLD) };
static const fld_t uart_f[] = { UART_FIELDS(X_FLD) };
static const fld_t i2c_f[] = { I2C_FIELDS(X_FLD) };
static const fld_t spi_f[] = { SPI_FIELDS(X_FLD) };
static const fld_t qspi_f[] = { QSPI_FIELDS(X_FLD) };
static const fld_t pwm_f[] = { PWM_FIELDS(X_FLD) };
static const fld_t adc_f[] = { ADC_FIELDS(X_FLD) };
#define NF(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
static const uint8_t gpio_c[] = { 0, 1, 0xFF }, uart_c[] = { 0, 5, 6, 0xFF }, i2c_c[] = { 0, 1, 2, 0xFF },
                     spi_c[] = { 0, 1, 4, 5, 6, 0xFF }, qspi_c[] = { 0, 0xFF }, pwm_c[] = { 0, 1, 0xFF }, adc_c[] = { 1, 0, 0xFF };
static const cfg_cls_t classes[] = {
    { NULL, NULL, 0, 0, 0, NULL, NULL },
    { "GpioConfig", "GPIO", MCS__CFG_GPIO, NF(gpio_f), offsetof(mcs_gpio_config_t, opts), gpio_f, gpio_c },
    { "UartConfig", "UART", MCS__CFG_UART, NF(uart_f), offsetof(mcs_uart_config_t, opts), uart_f, uart_c },
    { "I2cConfig", "I2C", MCS__CFG_I2C, NF(i2c_f), offsetof(mcs_i2c_config_t, opts), i2c_f, i2c_c },
    { "SpiConfig", "SPI", MCS__CFG_SPI, NF(spi_f), offsetof(mcs_spi_config_t, opts), spi_f, spi_c },
    { "QspiConfig", "QSPI", MCS__CFG_QSPI, NF(qspi_f), offsetof(mcs_qspi_config_t, opts), qspi_f, qspi_c },
    { "PwmConfig", "PWM", MCS__CFG_PWM, NF(pwm_f), offsetof(mcs_pwm_config_t, opts), pwm_f, pwm_c },
    { "AdcConfig", "ADC", MCS__CFG_ADC, NF(adc_f), offsetof(mcs_adc_config_t, opts), adc_f, adc_c },
    { "CanConfig", "CAN", MCS__CFG_CAN, NF(can_f), offsetof(mcs_can_config_t, opts), can_f, can_c },
};

/* ------------------------------------------------------------ field access */
static int32_t fget(const void* base, const fld_t* f) {
    const uint8_t* p = (const uint8_t*)base + f->off;
    switch (f->type) {
    case T_I8: return *(const int8_t*)p;
    case T_U16: return *(const uint16_t*)p;
    case T_I16: case T_PIN: return *(const int16_t*)p;
    case T_U32: return (int32_t)*(const uint32_t*)p;
    default: return *p;
    }
}
static void fput(void* base, const fld_t* f, int32_t v) {
    uint8_t* p = (uint8_t*)base + f->off;
    switch (f->type) {
    case T_I8: *(int8_t*)p = (int8_t)v; break;
    case T_U16: *(uint16_t*)p = (uint16_t)v; break;
    case T_I16: case T_PIN: *(int16_t*)p = (int16_t)v; break;
    case T_U32: *(uint32_t*)p = (uint32_t)v; break;
    default: *p = (uint8_t)v; break;
    }
}
static void defaults(const cfg_cls_t* c, void* base, size_t size) {
    memset(base, 0, size);
    for (int i = 0; i < c->nf; i++) fput(base, &c->f[i], c->f[i].def);
}
static const size_t sizes[] = { 0, sizeof(mcs_gpio_config_t), sizeof(mcs_uart_config_t), sizeof(mcs_i2c_config_t),
                                sizeof(mcs_spi_config_t), sizeof(mcs_qspi_config_t), sizeof(mcs_pwm_config_t), sizeof(mcs_adc_config_t), sizeof(mcs_can_config_t) };

void mcs_gpio_config_init(mcs_gpio_config_t* c, int mode) { defaults(&classes[MCS__CFG_GPIO], c, sizeof *c); c->mode = (uint8_t)mode; }
void mcs_uart_config_init(mcs_uart_config_t* c, uint32_t baud) { defaults(&classes[MCS__CFG_UART], c, sizeof *c); if (baud) c->frame.baud = baud; }
void mcs_i2c_config_init(mcs_i2c_config_t* c, uint32_t f) { defaults(&classes[MCS__CFG_I2C], c, sizeof *c); if (f) c->freq_hz = f; }
void mcs_spi_config_init(mcs_spi_config_t* c, uint32_t f, int mode) { defaults(&classes[MCS__CFG_SPI], c, sizeof *c); if (f) c->bus.freq_hz = f; c->bus.mode = (uint8_t)mode; }
void mcs_qspi_config_init(mcs_qspi_config_t* c, uint32_t f) { defaults(&classes[MCS__CFG_QSPI], c, sizeof *c); if (f) c->freq_hz = f; }
void mcs_pwm_config_init(mcs_pwm_config_t* c, uint32_t f) { defaults(&classes[MCS__CFG_PWM], c, sizeof *c); c->freq_hz = f; }
void mcs_adc_config_init(mcs_adc_config_t* c) { defaults(&classes[MCS__CFG_ADC], c, sizeof *c); }

void mcs_can_config_init(mcs_can_config_t* c, uint32_t rate) { defaults(&classes[MCS__CFG_CAN], c, sizeof *c); if (rate) c->bitrate = rate; }

static int ieq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}
int32_t mcs_hal_opt(const mcs_hal_option_t* t, const mcs_hal_opts_t* o, const char* periph, const char* name, int32_t def) {
    if (!t || !o) return def;
    for (int i = 0; i < o->n && i < MCS_HAL_MAX_OPTS; i++) {
        const mcs_hal_option_t* e = &t[o->id[i]];
        if ((!periph || ieq(e->periph, periph)) && ieq(e->name, name)) return o->value[i];
    }
    return def;
}

const char* mcs__cfg_nondefault(const void* cfg, int kind, const char* const* allowed) {
    if (kind < 1 || kind > MCS__CFG_CAN) return NULL;
    const cfg_cls_t* c = &classes[kind];
    for (int i = 0; i < c->nf; i++) {
        if (fget(cfg, &c->f[i]) == c->f[i].def) continue;
        bool ok = false;
        for (const char* const* a = allowed; a && *a; a++) if (!strcmp(*a, c->f[i].name)) { ok = true; break; }
        if (!ok) return c->f[i].name;
    }
    const mcs_hal_opts_t* o = (const mcs_hal_opts_t*)((const uint8_t*)cfg + c->opts_off);
    return o->n ? "board-specific options (Set)" : NULL;
}

/* ------------------------------------------------------------ C# objects */
static const mcs_class_def_t* defs[9];
void* mcs__cfg_get(mcs_value_t v, int kind) {
    if (kind < 1 || kind > MCS__CFG_CAN || !defs[kind]) return NULL;
    cfg_obj_t* o = (cfg_obj_t*)mcs_userdata_of(v, defs[kind]);
    return o ? (void*)&o->u : NULL;
}
static cfg_obj_t* obj_of(mcs_value_t self) {
    cfg_obj_t* o = (cfg_obj_t*)mcs_userdata(self);
    return o && o->kind >= 1 && o->kind <= MCS__CFG_CAN ? o : NULL;
}
static mcs_value_t value_of(const fld_t* f, int32_t v) {
    if (f->type == T_TRI) return v == MCS_ON ? mcs_bool(true) : v == MCS_OFF ? mcs_bool(false) : mcs_null();
    if (f->type == T_BOOL) return mcs_bool(v != 0);
    if (f->type == T_U32) return mcs_int((mcs_int_t)(uint32_t)v);
    return mcs_int(v);
}
static bool convert(mcs_vm_t* vm, const cfg_cls_t* c, const fld_t* f, mcs_value_t v, int32_t* out) {
    mcs_int_t x;
    if (f->type == T_TRI) {
        if (mcs_is_null(v)) { *out = MCS_AUTO; return true; }
        if (mcs_is_bool(v)) { *out = mcs_truthy(v) ? MCS_ON : MCS_OFF; return true; }
    }
    if (f->type == T_PIN) {
        if (mcs_is_null(v)) { *out = -1; return true; }
        if (mcs_is_string(v)) { int p = mcs__pin_arg(vm, v); if (mcs_has_exception(vm)) return false; *out = p; return true; }
    }
    if (mcs_is_bool(v)) x = mcs_truthy(v) ? 1 : 0;
    else { x = mcs_to_int(vm, v); if (mcs_has_exception(vm)) return false; }
    if (x < f->lo || x > (mcs_int_t)(uint32_t)f->hi) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "%s.%s must be %d..%d", c->name, f->name, (int)f->lo, (int)f->hi);
        return false;
    }
    *out = (int32_t)x;
    return true;
}
static mcs_value_t cfg_getf(mcs_vm_t* vm, mcs_value_t self, int i) {
    cfg_obj_t* o = obj_of(self);
    if (!o) return mcs_null();
    return value_of(&classes[o->kind].f[i], fget(&o->u, &classes[o->kind].f[i]));
}
static void cfg_setf(mcs_vm_t* vm, mcs_value_t self, int i, mcs_value_t v) {
    cfg_obj_t* o = obj_of(self);
    if (!o) return;
    const cfg_cls_t* c = &classes[o->kind];
    int32_t x;
    if (convert(vm, c, &c->f[i], v, &x)) fput(&o->u, &c->f[i], x);
}
static mcs_hal_opts_t* opts_of(cfg_obj_t* o) { return (mcs_hal_opts_t*)((uint8_t*)&o->u + classes[o->kind].opts_off); }
static const mcs_hal_option_t* options(mcs_vm_t* vm) { return ST() && HAL() ? HAL()->options : NULL; }
static int find_option(mcs_vm_t* vm, const cfg_cls_t* c, const char* name) {
    const mcs_hal_option_t* t = options(vm);
    for (int i = 0; t && t[i].name && i < 255; i++) if (ieq(t[i].periph, c->periph) && ieq(t[i].name, name)) return i;
    return -1;
}
static int find_field(const cfg_cls_t* c, const char* name) {
    for (int i = 0; i < c->nf; i++) if (ieq(c->f[i].name, name)) return i;
    return -1;
}
/* cfg.Set("name", value): a property by name or a port option; returns the config for chaining */
NATIVE(cfg_set) {
    cfg_obj_t* o = obj_of(self);
    if (!o) return mcs_null();
    const cfg_cls_t* c = &classes[o->kind];
    const char* name = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    int fi = find_field(c, name);
    if (fi >= 0) { cfg_setf(vm, self, fi, argv[1]); return self; }
    int id = find_option(vm, c, name);
    if (id < 0) {
        mcs_raise(vm, "ArgumentException", "%s: unknown setting '%s' (Hal.Options(\"%s\") lists this board's options)", c->name, name, c->periph);
        return mcs_null();
    }
    const mcs_hal_option_t* e = &options(vm)[id];
    mcs_hal_opts_t* op = opts_of(o);
    if (mcs_is_null(argv[1])) {                   /* back to the default */
        for (int i = 0; i < op->n; i++) if (op->id[i] == id) {
            for (int j = i + 1; j < op->n; j++) { op->id[j - 1] = op->id[j]; op->value[j - 1] = op->value[j]; }
            op->n--; break;
        }
        return self;
    }
    mcs_int_t v = mcs_is_bool(argv[1]) ? (mcs_truthy(argv[1]) ? 1 : 0) : mcs_to_int(vm, argv[1]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (v < e->min || v > e->max) { mcs_raise(vm, "ArgumentOutOfRangeException", "%s option %s must be %d..%d", c->name, e->name, (int)e->min, (int)e->max); return mcs_null(); }
    int i = 0;
    while (i < op->n && op->id[i] != id) i++;
    if (i == op->n) {
        if (op->n >= MCS_HAL_MAX_OPTS) { mcs_raise(vm, "InvalidOperationException", "%s: more than %d options (MCS_HAL_MAX_OPTS)", c->name, MCS_HAL_MAX_OPTS); return mcs_null(); }
        op->n++;
    }
    op->id[i] = (uint8_t)id; op->value[i] = (int32_t)v;
    return self;
}
NATIVE(cfg_get) {
    cfg_obj_t* o = obj_of(self);
    if (!o) return mcs_null();
    const cfg_cls_t* c = &classes[o->kind];
    const char* name = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    int fi = find_field(c, name);
    if (fi >= 0) return value_of(&c->f[fi], fget(&o->u, &c->f[fi]));
    int id = find_option(vm, c, name);
    if (id < 0) { mcs_raise(vm, "ArgumentException", "%s: unknown setting '%s'", c->name, name); return mcs_null(); }
    mcs_hal_opts_t* op = opts_of(o);
    for (int i = 0; i < op->n; i++) if (op->id[i] == id) return mcs_int(op->value[i]);
    return mcs_null();
}
NATIVE(cfg_reset) {
    cfg_obj_t* o = obj_of(self);
    if (o) defaults(&classes[o->kind], &o->u, sizes[o->kind]);
    return self;
}
/* "SpiConfig { Frequency = 8000000, Sck = 6, Dma = true, queue_size = 4 }" - the non-default settings */
NATIVE(cfg_tostring) {
    cfg_obj_t* o = obj_of(self);
    if (!o) return mcs_string(vm, "");
    const cfg_cls_t* c = &classes[o->kind];
    char buf[320];
    size_t n = (size_t)snprintf(buf, sizeof buf, "%s {", c->name);
    bool first = true;
    for (int i = 0; i < c->nf && n < sizeof buf; i++) {
        int32_t v = fget(&o->u, &c->f[i]);
        if (v == c->f[i].def) continue;
        const char* t = c->f[i].type == T_TRI ? (v == MCS_ON ? "true" : "false") : c->f[i].type == T_BOOL ? (v ? "true" : "false") : NULL;
        if (t) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %s = %s", first ? "" : ",", c->f[i].name, t);
        else if (c->f[i].type == T_U32) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %s = %lu", first ? "" : ",", c->f[i].name, (unsigned long)(uint32_t)v);
        else n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %s = %d", first ? "" : ",", c->f[i].name, (int)v);
        first = false;
    }
    const mcs_hal_option_t* t = options(vm);
    mcs_hal_opts_t* op = opts_of(o);
    for (int i = 0; t && i < op->n && n < sizeof buf; i++) {
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %s = %d", first ? "" : ",", t[op->id[i]].name, (int)op->value[i]);
        first = false;
    }
    if (n < sizeof buf) n += (size_t)snprintf(buf + n, sizeof buf - n, " }");
    if (n >= sizeof buf) n = sizeof buf - 1;
    return mcs_string_n(vm, buf, n);
}
static void cfg_ctor(mcs_vm_t* vm, mcs_value_t self, int kind, int argc, mcs_value_t* argv) {
    cfg_obj_t* o = (cfg_obj_t*)mcs_userdata(self);
    const cfg_cls_t* c = &classes[kind];
    o->kind = (uint8_t)kind;
    defaults(c, &o->u, sizes[kind]);
    int max = 0;
    while (c->ctor[max] != 0xFF) max++;
    if (argc > max) { mcs_raise(vm, "ArgumentException", "%s: at most %d constructor arguments", c->name, max); return; }
    for (int i = 0; i < argc; i++) {
        int32_t x;
        const fld_t* f = &c->f[c->ctor[i]];
        if (!convert(vm, c, f, argv[i], &x)) return;
        fput(&o->u, f, x);
    }
}

#define COMMON_MEMBERS \
    MCS_FN("Set", cfg_set, 2), MCS_FN("Get", cfg_get, 1), MCS_FN("Reset", cfg_reset, 0), MCS_FN("ToString", cfg_tostring, 0),
#define CLASS(P, KIND, NAME, FIELDS) \
    FIELDS(X_ACC) \
    static const mcs_reg_t P##_members[] = { FIELDS(X_MEM) COMMON_MEMBERS MCS_REG_END }; \
    static void P##_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { cfg_ctor(vm, self, KIND, argc, argv); } \
    static const mcs_class_def_t P##_def = { NAME, sizeof(cfg_obj_t), P##_ctor, NULL, P##_members, NULL };
CLASS(gpio, MCS__CFG_GPIO, "GpioConfig", GPIO_FIELDS)
CLASS(uart, MCS__CFG_UART, "UartConfig", UART_FIELDS)
CLASS(i2c, MCS__CFG_I2C, "I2cConfig", I2C_FIELDS)
CLASS(spi, MCS__CFG_SPI, "SpiConfig", SPI_FIELDS)
CLASS(qspi, MCS__CFG_QSPI, "QspiConfig", QSPI_FIELDS)
CLASS(pwm, MCS__CFG_PWM, "PwmConfig", PWM_FIELDS)
CLASS(adc, MCS__CFG_ADC, "AdcConfig", ADC_FIELDS)
CLASS(can, MCS__CFG_CAN, "CanConfig", CAN_FIELDS)

/* Hal.Options([peripheral]): this board's extra settings, "SPI.queue_size (1..16): queued transactions" */
NATIVE(hal_options) {
    const char* want = argc > 0 && !mcs_is_null(argv[0]) ? mcs_to_cstr(vm, argv[0]) : NULL;
    if (mcs_has_exception(vm)) return mcs_null();
    mcs_value_t list = mcs_new_list(vm);
    mcs_push_root(vm, list);
    const mcs_hal_option_t* t = options(vm);
    for (int i = 0; t && t[i].name && i < 255; i++) {
        if (want && !ieq(want, t[i].periph)) continue;
        char line[160];
        int n = snprintf(line, sizeof line, "%s.%s (%ld..%ld)%s%s", t[i].periph, t[i].name, (long)t[i].min, (long)t[i].max,
                         t[i].help ? ": " : "", t[i].help ? t[i].help : "");
        mcs_value_t s = mcs_string_n(vm, line, n < 0 ? 0 : (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
        mcs_list_add(vm, list, s);
    }
    mcs_pop_root(vm, 1);
    return list;
}
static const mcs_reg_t hal_cfg_fns[] = { MCS_FN("Options", hal_options, -1), MCS_REG_END };

static const mcs_const_t gpio_cfg_consts[] = {
    MCS_CONST("PullDefault", MCS_PULL_DEFAULT), MCS_CONST("PullNone", MCS_PULL_NONE), MCS_CONST("PullUp", MCS_PULL_UP),
    MCS_CONST("PullDown", MCS_PULL_DOWN), MCS_CONST("PullBoth", MCS_PULL_BOTH),
    MCS_CONST("SpeedDefault", MCS_SPEED_DEFAULT), MCS_CONST("SpeedLow", MCS_SPEED_LOW), MCS_CONST("SpeedMedium", MCS_SPEED_MEDIUM),
    MCS_CONST("SpeedHigh", MCS_SPEED_HIGH), MCS_CONST("SpeedVeryHigh", MCS_SPEED_VERY_HIGH), MCS_CONST_END
};
static const mcs_const_t adc_cfg_consts[] = {
    MCS_CONST("AttenDefault", MCS_ADC_ATTEN_DEFAULT), MCS_CONST("Atten0dB", MCS_ADC_ATTEN_0DB), MCS_CONST("Atten2_5dB", MCS_ADC_ATTEN_2_5DB),
    MCS_CONST("Atten6dB", MCS_ADC_ATTEN_6DB), MCS_CONST("Atten12dB", MCS_ADC_ATTEN_12DB), MCS_CONST_END
};

static bool hal_has(const mcs_hal_t* h, int kind) {
    switch (kind) {
    case MCS__CFG_GPIO: return h->gpio_write != NULL;
    case MCS__CFG_UART: return h->uart_write != NULL;
    case MCS__CFG_I2C: return h->i2c_write != NULL;
    case MCS__CFG_SPI: return h->spi_transfer != NULL;
    case MCS__CFG_QSPI: return h->qspi_command != NULL;
    case MCS__CFG_PWM: return h->pwm_set != NULL || h->pwm_set16 != NULL;
    case MCS__CFG_ADC: return h->adc_read != NULL;
    case MCS__CFG_CAN: return h->can_send != NULL;
    default: return false;
    }
}
void mcs__cfg_open(mcs_vm_t* vm, const mcs_hal_t* h) {
    static const mcs_class_def_t* const all[9] = { NULL, &gpio_def, &uart_def, &i2c_def, &spi_def, &qspi_def, &pwm_def, &adc_def, &can_def };
    for (int k = 1; k <= MCS__CFG_CAN; k++) {
        defs[k] = all[k];
        if (hal_has(h, k)) mcs_register_class(vm, all[k]);
    }
    mcs_register_module(vm, "Hal", hal_cfg_fns);
    if (hal_has(h, MCS__CFG_GPIO)) mcs_register_consts(vm, "GPIO", gpio_cfg_consts);
    if (hal_has(h, MCS__CFG_ADC)) mcs_register_consts(vm, "ADC", adc_cfg_consts);
}
#endif
