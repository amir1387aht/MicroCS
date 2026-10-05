/*
 * MicroCS - hardware abstraction (optional module, MCS_ENABLE_HAL).
 *
 * A board describes itself with one mcs_hal_t: a table of C function pointers.
 * A NULL entry means "not available"; the matching C# class is then not
 * registered and Hal.Has("SPI") returns false. Ports fill in only what the
 * hardware has; the core never calls vendor SDKs directly.
 *
 * All functions return >= 0 on success and a negative value on failure
 * (convention: -1 generic, -2 not supported for that pin/bus, -3 timeout,
 * -4 no device / NACK). Calls happen on the VM thread.
 */
#ifndef MCS_HAL_H
#define MCS_HAL_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef MCS_HAL_MAX_XFER
#define MCS_HAL_MAX_XFER 256     /* largest single UART/I2C/SPI transfer, on the C stack */
#endif

enum { MCS_HAL_ERR = -1, MCS_HAL_ENOTSUP = -2, MCS_HAL_ETIMEOUT = -3, MCS_HAL_ENODEV = -4 };
enum { MCS_GPIO_INPUT = 0, MCS_GPIO_OUTPUT = 1, MCS_GPIO_INPUT_PULLUP = 2,
       MCS_GPIO_INPUT_PULLDOWN = 3, MCS_GPIO_OPEN_DRAIN = 4 };

typedef struct mcs_hal {
    const char* board;            /* shown as Hal.Board */
    void* ctx;                    /* passed to every function */

    int (*gpio_mode)(void* ctx, int pin, int mode);
    int (*gpio_write)(void* ctx, int pin, int value);
    int (*gpio_read)(void* ctx, int pin);

    int (*uart_open)(void* ctx, int port, uint32_t baud);
    int (*uart_write)(void* ctx, int port, const uint8_t* data, size_t n);
    int (*uart_read)(void* ctx, int port, uint8_t* buf, size_t n, uint32_t timeout_ms);
    int (*uart_available)(void* ctx, int port);

    int (*i2c_write)(void* ctx, int bus, int addr, const uint8_t* data, size_t n);
    int (*i2c_read)(void* ctx, int bus, int addr, uint8_t* buf, size_t n);

    int (*spi_transfer)(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n);

    int (*adc_read)(void* ctx, int channel);  /* raw counts */
    uint8_t adc_bits;                          /* resolution, e.g. 12 */

    int (*pwm_set)(void* ctx, int channel, uint32_t freq_hz, uint16_t duty_permille);
} mcs_hal_t;

/* Register the C# peripheral classes the board supports, plus `Hal`. The HAL
 * struct must outlive the VM. Stored in MCS_EXT_HAL. */
void mcs_hal_open_lib(mcs_vm_t* vm, const mcs_hal_t* hal);

/* ---- simulator board: used by tests, the host CLI (--sim) and examples ----
 * GPIO: 64 pins; outputs read back their value; inputs read `inputs[pin]`
 *       (pull-ups read 1 unless driven).
 * UART: per-port loopback, i.e. a TX-RX jumper.
 * I2C:  0x48 temperature sensor (pointer reg 0 = 25.0 C), 0x50 256-byte EEPROM.
 * SPI:  MOSI-MISO loopback.   ADC: 12-bit, returns adc[ch].   PWM: stored. */
typedef struct {
    uint8_t mode[64], out[64], inputs[64];
    uint8_t uart_buf[4][256];
    uint16_t uart_head[4], uart_tail[4];
    uint32_t uart_baud[4];
    uint8_t temp_ptr;
    int16_t temp_centi;             /* sensor reading, 1/100 C */
    uint8_t eeprom[256], eeprom_ptr;
    uint16_t adc[16];
    uint32_t pwm_freq[8];
    uint16_t pwm_duty[8];
    mcs_write_fn log;               /* optional trace of every operation */
    void* log_ud;
} mcs_hal_sim_t;
void mcs_hal_sim_init(mcs_hal_t* hal, mcs_hal_sim_t* sim);

#ifdef __cplusplus
}
#endif
#endif
