/* MicroCS - C# peripheral API (GPIO, UART, I2C, SPI, ADC, PWM, Hal) over mcs_hal_t. */
#include "mcs_hal.h"
#if MCS_ENABLE_HAL
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define HAL() ((const mcs_hal_t*)mcs_get_ext(vm, MCS_EXT_HAL))
#define INT_ARG(i, var) int var = (int)mcs_to_int(vm, argv[i]); if (mcs_has_exception(vm)) return mcs_null()

static mcs_value_t hal_fail(mcs_vm_t* vm, const char* op, int rc) {
    const char* why = rc == MCS_HAL_ENOTSUP ? "not supported" : rc == MCS_HAL_ETIMEOUT ? "timeout"
                    : rc == MCS_HAL_ENODEV ? "no device" : "failed";
    if (rc == MCS_HAL_ETIMEOUT) mcs_raise(vm, "TimeoutException", "%s: %s", op, why);
    else if (rc == MCS_HAL_ENOTSUP) mcs_raise(vm, "NotSupportedException", "%s: %s", op, why);
    else mcs_raise(vm, "IOException", "%s: %s (%d)", op, why, rc);
    return mcs_null();
}

/* byte[] / List<byte> / string -> buffer */
static bool to_bytes(mcs_vm_t* vm, mcs_value_t v, uint8_t* buf, size_t* n) {
    if (mcs_is_string(v)) {
        size_t l = mcs_strlen(v);
        if (l > MCS_HAL_MAX_XFER) goto too_big;
        memcpy(buf, mcs_cstr(v), l); *n = l;
        return true;
    }
    int k = mcs_obj_kind(v);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "expected byte[] or string"); return false; }
    uint32_t l = mcs_len(v);
    if (l > MCS_HAL_MAX_XFER) goto too_big;
    for (uint32_t i = 0; i < l; i++) {
        buf[i] = (uint8_t)mcs_to_int(vm, mcs_index(v, i));
        if (mcs_has_exception(vm)) return false;
    }
    *n = l;
    return true;
too_big:
    mcs_raise(vm, "ArgumentOutOfRangeException", "transfer larger than %d bytes", MCS_HAL_MAX_XFER);
    return false;
}
static mcs_value_t bytes_val(mcs_vm_t* vm, const uint8_t* b, size_t n) {
    mcs_value_t a = mcs_new_array(vm, (uint32_t)n);
    for (size_t i = 0; i < n; i++) mcs_set_index(a, (uint32_t)i, mcs_int(b[i]));
    return a;
}
static int count_arg(mcs_vm_t* vm, mcs_value_t v) {
    mcs_int_t n = mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (n < 0 || n > MCS_HAL_MAX_XFER) { mcs_raise(vm, "ArgumentOutOfRangeException", "count must be 0..%d", MCS_HAL_MAX_XFER); return -1; }
    return (int)n;
}

/* ------------------------------------------------------------- GPIO */
NATIVE(gpio_mode) {
    INT_ARG(0, pin); INT_ARG(1, mode);
    if (!HAL()->gpio_mode) return hal_fail(vm, "GPIO.Mode", MCS_HAL_ENOTSUP);
    int rc = HAL()->gpio_mode(HAL()->ctx, pin, mode);
    return rc < 0 ? hal_fail(vm, "GPIO.Mode", rc) : mcs_null();
}
NATIVE(gpio_write) {
    INT_ARG(0, pin);
    int rc = HAL()->gpio_write(HAL()->ctx, pin, mcs_truthy(argv[1]) ? 1 : 0);
    return rc < 0 ? hal_fail(vm, "GPIO.Write", rc) : mcs_null();
}
NATIVE(gpio_read) {
    INT_ARG(0, pin);
    if (!HAL()->gpio_read) return hal_fail(vm, "GPIO.Read", MCS_HAL_ENOTSUP);
    int rc = HAL()->gpio_read(HAL()->ctx, pin);
    return rc < 0 ? hal_fail(vm, "GPIO.Read", rc) : mcs_bool(rc != 0);
}
NATIVE(gpio_toggle) {
    INT_ARG(0, pin);
    if (!HAL()->gpio_read) return hal_fail(vm, "GPIO.Toggle", MCS_HAL_ENOTSUP);
    int rc = HAL()->gpio_read(HAL()->ctx, pin);
    if (rc >= 0) rc = HAL()->gpio_write(HAL()->ctx, pin, !rc);
    return rc < 0 ? hal_fail(vm, "GPIO.Toggle", rc) : mcs_null();
}
static const mcs_reg_t gpio_fns[] = {
    MCS_FN("Mode", gpio_mode, 2), MCS_FN("Write", gpio_write, 2), MCS_FN("Read", gpio_read, 1),
    MCS_FN("Toggle", gpio_toggle, 1), MCS_REG_END
};

/* ------------------------------------------------------------- UART */
NATIVE(uart_open) {
    INT_ARG(0, port); INT_ARG(1, baud);
    if (!HAL()->uart_open) return mcs_null();
    int rc = HAL()->uart_open(HAL()->ctx, port, (uint32_t)baud);
    return rc < 0 ? hal_fail(vm, "UART.Open", rc) : mcs_null();
}
NATIVE(uart_write) {
    INT_ARG(0, port);
    uint8_t buf[MCS_HAL_MAX_XFER]; size_t n;
    if (!to_bytes(vm, argv[1], buf, &n)) return mcs_null();
    int rc = HAL()->uart_write(HAL()->ctx, port, buf, n);
    return rc < 0 ? hal_fail(vm, "UART.Write", rc) : mcs_int(rc);
}
static int uart_read_common(mcs_vm_t* vm, int argc, mcs_value_t* argv, uint8_t* buf) {
    int port = (int)mcs_to_int(vm, argv[0]);
    int n = count_arg(vm, argv[1]);
    uint32_t timeout = argc > 2 ? (uint32_t)mcs_to_int(vm, argv[2]) : 0;
    if (mcs_has_exception(vm) || n < 0) return -100;
    if (!HAL()->uart_read) { hal_fail(vm, "UART.Read", MCS_HAL_ENOTSUP); return -100; }
    int rc = HAL()->uart_read(HAL()->ctx, port, buf, (size_t)n, timeout);
    if (rc < 0) { hal_fail(vm, "UART.Read", rc); return -100; }
    return rc;
}
NATIVE(uart_read) {
    uint8_t buf[MCS_HAL_MAX_XFER];
    int rc = uart_read_common(vm, argc, argv, buf);
    return rc < 0 ? mcs_null() : bytes_val(vm, buf, (size_t)rc);
}
NATIVE(uart_read_string) {
    uint8_t buf[MCS_HAL_MAX_XFER];
    int rc = uart_read_common(vm, argc, argv, buf);
    return rc < 0 ? mcs_null() : mcs_string_n(vm, (const char*)buf, (size_t)rc);
}
NATIVE(uart_available) {
    INT_ARG(0, port);
    if (!HAL()->uart_available) return mcs_int(0);
    int rc = HAL()->uart_available(HAL()->ctx, port);
    return rc < 0 ? hal_fail(vm, "UART.Available", rc) : mcs_int(rc);
}
static const mcs_reg_t uart_fns[] = {
    MCS_FN("Open", uart_open, 2), MCS_FN("Write", uart_write, 2), MCS_FN("Read", uart_read, -1),
    MCS_FN("ReadString", uart_read_string, -1), MCS_FN("Available", uart_available, 1), MCS_REG_END
};

/* ------------------------------------------------------------- I2C */
NATIVE(i2c_write) {
    INT_ARG(0, bus); INT_ARG(1, addr);
    uint8_t buf[MCS_HAL_MAX_XFER]; size_t n;
    if (!to_bytes(vm, argv[2], buf, &n)) return mcs_null();
    int rc = HAL()->i2c_write(HAL()->ctx, bus, addr, buf, n);
    return rc < 0 ? hal_fail(vm, "I2C.Write", rc) : mcs_null();
}
NATIVE(i2c_read) {
    INT_ARG(0, bus); INT_ARG(1, addr);
    int n = count_arg(vm, argv[2]);
    if (n < 0) return mcs_null();
    if (!HAL()->i2c_read) return hal_fail(vm, "I2C.Read", MCS_HAL_ENOTSUP);
    uint8_t buf[MCS_HAL_MAX_XFER];
    int rc = HAL()->i2c_read(HAL()->ctx, bus, addr, buf, (size_t)n);
    return rc < 0 ? hal_fail(vm, "I2C.Read", rc) : bytes_val(vm, buf, (size_t)n);
}
/* register read: write the pointer bytes, then read (repeated start if the HAL
 * implements it that way; otherwise stop + start, which most sensors accept) */
NATIVE(i2c_write_read) {
    INT_ARG(0, bus); INT_ARG(1, addr);
    uint8_t tx[MCS_HAL_MAX_XFER], rx[MCS_HAL_MAX_XFER]; size_t tn;
    if (!to_bytes(vm, argv[2], tx, &tn)) return mcs_null();
    int n = count_arg(vm, argv[3]);
    if (n < 0) return mcs_null();
    if (!HAL()->i2c_read) return hal_fail(vm, "I2C.WriteRead", MCS_HAL_ENOTSUP);
    int rc = HAL()->i2c_write(HAL()->ctx, bus, addr, tx, tn);
    if (rc >= 0) rc = HAL()->i2c_read(HAL()->ctx, bus, addr, rx, (size_t)n);
    return rc < 0 ? hal_fail(vm, "I2C.WriteRead", rc) : bytes_val(vm, rx, (size_t)n);
}
static const mcs_reg_t i2c_fns[] = {
    MCS_FN("Write", i2c_write, 3), MCS_FN("Read", i2c_read, 3), MCS_FN("WriteRead", i2c_write_read, 4), MCS_REG_END
};

/* ------------------------------------------------------------- SPI */
NATIVE(spi_transfer) {
    INT_ARG(0, bus);
    uint8_t tx[MCS_HAL_MAX_XFER], rx[MCS_HAL_MAX_XFER]; size_t n;
    if (!to_bytes(vm, argv[1], tx, &n)) return mcs_null();
    int rc = HAL()->spi_transfer(HAL()->ctx, bus, tx, rx, n);
    return rc < 0 ? hal_fail(vm, "SPI.Transfer", rc) : bytes_val(vm, rx, n);
}
static const mcs_reg_t spi_fns[] = { MCS_FN("Transfer", spi_transfer, 2), MCS_REG_END };

/* ------------------------------------------------------------- ADC / PWM */
NATIVE(adc_read) {
    INT_ARG(0, ch);
    int rc = HAL()->adc_read(HAL()->ctx, ch);
    return rc < 0 ? hal_fail(vm, "ADC.Read", rc) : mcs_int(rc);
}
static const mcs_reg_t adc_fns[] = { MCS_FN("Read", adc_read, 1), MCS_REG_END };

static mcs_value_t pwm_common(mcs_vm_t* vm, mcs_value_t* argv, int permille) {
    INT_ARG(0, ch); INT_ARG(1, freq);
    if (permille < 0 || permille > 1000) { mcs_raise(vm, "ArgumentOutOfRangeException", "duty cycle out of range"); return mcs_null(); }
    int rc = HAL()->pwm_set(HAL()->ctx, ch, (uint32_t)freq, (uint16_t)permille);
    return rc < 0 ? hal_fail(vm, "PWM.Set", rc) : mcs_null();
}
#if MCS_ENABLE_FLOAT
NATIVE(pwm_set) {
    mcs_float_t d = mcs_to_float(vm, argv[2]);
    if (mcs_has_exception(vm)) return mcs_null();
    return pwm_common(vm, argv, (int)(d * 1000 + (mcs_float_t)0.5));
}
#endif
NATIVE(pwm_set_permille) {
    INT_ARG(2, pm);
    return pwm_common(vm, argv, pm);
}
static const mcs_reg_t pwm_fns[] = {
#if MCS_ENABLE_FLOAT
    MCS_FN("Set", pwm_set, 3),           /* duty cycle 0.0 .. 1.0 */
#endif
    MCS_FN("SetPermille", pwm_set_permille, 3), MCS_REG_END
};

/* ------------------------------------------------------------- Hal */
static bool has(const mcs_hal_t* h, const char* n) {
    if (!strcmp(n, "GPIO")) return h->gpio_write != NULL;
    if (!strcmp(n, "UART")) return h->uart_write != NULL;
    if (!strcmp(n, "I2C")) return h->i2c_write != NULL;
    if (!strcmp(n, "SPI")) return h->spi_transfer != NULL;
    if (!strcmp(n, "ADC")) return h->adc_read != NULL;
    if (!strcmp(n, "PWM")) return h->pwm_set != NULL;
    return false;
}
NATIVE(hal_has) {
    const char* n = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs_bool(has(HAL(), n));
}
static const mcs_reg_t hal_fns[] = { MCS_FN("Has", hal_has, 1), MCS_REG_END };

void mcs_hal_open_lib(mcs_vm_t* vm, const mcs_hal_t* h) {
    mcs_set_ext(vm, MCS_EXT_HAL, (void*)h);
    mcs_register_module(vm, "Hal", hal_fns);
    mcs_module_set(vm, "Hal", "Board", mcs_string(vm, h->board ? h->board : "unknown"));
    if (has(h, "GPIO")) {
        mcs_register_module(vm, "GPIO", gpio_fns);
        mcs_module_set(vm, "GPIO", "Input", mcs_int(MCS_GPIO_INPUT));
        mcs_module_set(vm, "GPIO", "Output", mcs_int(MCS_GPIO_OUTPUT));
        mcs_module_set(vm, "GPIO", "InputPullUp", mcs_int(MCS_GPIO_INPUT_PULLUP));
        mcs_module_set(vm, "GPIO", "InputPullDown", mcs_int(MCS_GPIO_INPUT_PULLDOWN));
        mcs_module_set(vm, "GPIO", "OpenDrain", mcs_int(MCS_GPIO_OPEN_DRAIN));
        mcs_module_set(vm, "GPIO", "High", mcs_bool(true));
        mcs_module_set(vm, "GPIO", "Low", mcs_bool(false));
    }
    if (has(h, "UART")) mcs_register_module(vm, "UART", uart_fns);
    if (has(h, "I2C")) mcs_register_module(vm, "I2C", i2c_fns);
    if (has(h, "SPI")) mcs_register_module(vm, "SPI", spi_fns);
    if (has(h, "ADC")) {
        mcs_register_module(vm, "ADC", adc_fns);
        mcs_module_set(vm, "ADC", "Resolution", mcs_int(h->adc_bits ? h->adc_bits : 12));
    }
    if (has(h, "PWM")) mcs_register_module(vm, "PWM", pwm_fns);
}
#endif
