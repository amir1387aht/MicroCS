/*
 * MicroCS - hardware abstraction (optional module, MCS_ENABLE_HAL).
 *
 * A board describes itself with one mcs_hal_t: a table of C function pointers.
 * A NULL entry means "not available"; the matching C# class is then not
 * registered and Hal.Has("SPI") returns false. Ports fill in only what the
 * hardware has; the core never calls vendor SDKs directly - ports in ports/
 * (ESP-IDF, STM32Cube, Pico SDK, Zephyr, Arduino) do, through the vendor's own
 * headers.
 *
 * Every function returns >= 0 on success and a negative value on failure:
 *   MCS_HAL_ERR (-1) generic       MCS_HAL_ENOTSUP (-2) not supported for that pin/bus
 *   MCS_HAL_ETIMEOUT (-3) timeout  MCS_HAL_ENODEV (-4) no device / NACK
 *   MCS_HAL_EBUSY (-5) busy        MCS_HAL_EINVAL (-6) bad argument
 * which become IOException / NotSupportedException / TimeoutException /
 * ArgumentException in C#. Calls happen on the VM thread, except
 * mcs_hal_post(), which is safe to call from interrupt handlers.
 *
 * Compatibility: the v1 members (gpio .. pwm_set) keep their order. Everything
 * after them is optional, so a v1 board table written with designated
 * initializers still compiles and works unchanged.
 */
#ifndef MCS_HAL_H
#define MCS_HAL_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MCS_HAL_API_VERSION 2

#ifndef MCS_HAL_MAX_XFER
#define MCS_HAL_MAX_XFER 256     /* largest single transfer per call; buffers live on the C stack */
#endif
#ifndef MCS_HAL_EVENT_QUEUE
#define MCS_HAL_EVENT_QUEUE 32   /* ISR -> VM event ring (power of two) */
#endif
#ifndef MCS_HAL_MAX_CALLBACKS
#define MCS_HAL_MAX_CALLBACKS 16 /* GPIO + timer + user event handlers per VM */
#endif
#ifndef MCS_HAL_MAX_VMS
#define MCS_HAL_MAX_VMS 2        /* VMs that can have the HAL open at the same time */
#endif
#ifndef MCS_HAL_MAX_BUSES
#define MCS_HAL_MAX_BUSES 4      /* per peripheral type, for cached bus settings */
#endif

enum { MCS_HAL_ERR = -1, MCS_HAL_ENOTSUP = -2, MCS_HAL_ETIMEOUT = -3, MCS_HAL_ENODEV = -4,
       MCS_HAL_EBUSY = -5, MCS_HAL_EINVAL = -6 };

/* GPIO modes and interrupt edges */
enum { MCS_GPIO_INPUT = 0, MCS_GPIO_OUTPUT = 1, MCS_GPIO_INPUT_PULLUP = 2,
       MCS_GPIO_INPUT_PULLDOWN = 3, MCS_GPIO_OPEN_DRAIN = 4, MCS_GPIO_ANALOG = 5 };
enum { MCS_GPIO_EDGE_NONE = 0, MCS_GPIO_EDGE_RISING = 1, MCS_GPIO_EDGE_FALLING = 2, MCS_GPIO_EDGE_BOTH = 3 };

/* UART frame settings */
enum { MCS_UART_PARITY_NONE = 0, MCS_UART_PARITY_ODD = 1, MCS_UART_PARITY_EVEN = 2 };
typedef struct {
    uint32_t baud;
    uint8_t data_bits;    /* 7, 8, 9 */
    uint8_t parity;       /* MCS_UART_PARITY_* */
    uint8_t stop_bits;    /* 1 or 2 */
    uint8_t flow_control; /* 0 none, 1 RTS/CTS */
} mcs_uart_cfg_t;

/* SPI bus settings */
typedef struct {
    uint32_t freq_hz;
    uint8_t mode;         /* 0..3 (CPOL << 1 | CPHA) */
    uint8_t bits;         /* word size, normally 8 */
    uint8_t lsb_first;
    uint8_t reserved;
} mcs_spi_cfg_t;

/* I2S stream settings */
enum { MCS_I2S_TX = 1, MCS_I2S_RX = 2, MCS_I2S_DUPLEX = 3 };
enum { MCS_I2S_PHILIPS = 0, MCS_I2S_MSB = 1, MCS_I2S_PCM = 2 };
typedef struct {
    uint32_t sample_rate;
    uint8_t bits;         /* 16, 24, 32 bits per sample */
    uint8_t channels;     /* 1 mono, 2 stereo */
    uint8_t direction;    /* MCS_I2S_TX / RX / DUPLEX */
    uint8_t format;       /* MCS_I2S_PHILIPS ... */
} mcs_i2s_cfg_t;

/* One QSPI / OSPI / dual-SPI command. Phases with 0 lines are skipped. */
typedef struct {
    uint8_t instruction;
    uint8_t instr_lines;   /* 0 = no instruction phase, 1, 2, 4, 8 */
    uint8_t addr_bytes;    /* 0 = no address phase, 1..4 */
    uint8_t addr_lines;
    uint32_t address;
    uint8_t dummy_cycles;
    uint8_t data_lines;    /* 1, 2, 4, 8 (ignored when n == 0) */
    uint8_t reserved[2];
} mcs_qspi_cmd_t;

/* Classic CAN / TWAI frame (CAN FD payloads are not supported yet) */
typedef struct {
    uint32_t id;
    uint8_t extended;      /* 29-bit identifier */
    uint8_t rtr;           /* remote frame */
    uint8_t len;           /* 0..8 */
    uint8_t data[8];
} mcs_can_frame_t;

/* Events posted by drivers (often from ISRs) and dispatched to C# callbacks
 * on the VM thread by mcs_hal_poll(). */
enum { MCS_HAL_EV_GPIO = 1,      /* source = pin,   value = level        */
       MCS_HAL_EV_TIMER = 2,     /* source = timer, value = fire count   */
       MCS_HAL_EV_UART = 3,      /* source = port,  value = bytes ready  */
       MCS_HAL_EV_CAN = 4,       /* source = bus,   value = frames ready */
       MCS_HAL_EV_USER = 16 };   /* MCS_HAL_EV_USER + n: your own events, Hal.OnEvent(n, ...) */
typedef struct {
    uint8_t type;
    uint8_t reserved;
    uint16_t source;
    int32_t value;
} mcs_hal_event_t;

typedef struct mcs_hal {
    const char* board;            /* shown as Hal.Board */
    void* ctx;                    /* passed to every function */

    /* ---------------- v1 ---------------- */
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

    /* ---------------- v2 (all optional) ---------------- */
    /* GPIO: enable an edge interrupt (edge 0 disables). The driver reports
     * edges with mcs_hal_post(MCS_HAL_EV_GPIO, pin, level) or poll_event. */
    int (*gpio_irq)(void* ctx, int pin, int edge);
    /* Board pin names ("PA5", "GPIO13", "LED"). NULL = generic parser, see
     * mcs_hal_parse_pin(). Return the pin number or < 0. */
    int (*pin_lookup)(void* ctx, const char* name);

    int (*uart_config)(void* ctx, int port, const mcs_uart_cfg_t* cfg);   /* preferred over uart_open */
    int (*uart_close)(void* ctx, int port);

    int (*i2c_open)(void* ctx, int bus, uint32_t freq_hz);
    /* write then read with a repeated start. NULL = i2c_write + i2c_read. */
    int (*i2c_write_read)(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn);
    /* address probe for I2C.Scan; 0 = ACK, MCS_HAL_ENODEV = no device. NULL = 1-byte read. */
    int (*i2c_probe)(void* ctx, int bus, int addr);

    int (*spi_open)(void* ctx, int bus, const mcs_spi_cfg_t* cfg);

    /* ADC: calibrated millivolts (e.g. ESP32 eFuse calibration). NULL = raw * vref / full scale */
    int (*adc_read_mv)(void* ctx, int channel);
    uint16_t adc_vref_mv;         /* 0 = 3300 */

    int (*dac_write)(void* ctx, int channel, uint32_t value);
    uint8_t dac_bits;             /* 0 = 8 */

    /* PWM with 16-bit duty (0..65535); preferred over pwm_set when present */
    int (*pwm_set16)(void* ctx, int channel, uint32_t freq_hz, uint16_t duty);
    int (*pwm_stop)(void* ctx, int channel);

    /* Hardware timers: fire every period_us (or once). The driver posts
     * MCS_HAL_EV_TIMER from the timer ISR. */
    int (*timer_start)(void* ctx, int timer, uint32_t period_us, int periodic);
    int (*timer_stop)(void* ctx, int timer);

    int (*i2s_open)(void* ctx, int bus, const mcs_i2s_cfg_t* cfg);
    int (*i2s_write)(void* ctx, int bus, const uint8_t* data, size_t n, uint32_t timeout_ms); /* bytes written */
    int (*i2s_read)(void* ctx, int bus, uint8_t* buf, size_t n, uint32_t timeout_ms);       /* bytes read */
    int (*i2s_close)(void* ctx, int bus);

    int (*qspi_open)(void* ctx, int bus, uint32_t freq_hz);
    /* one command: instruction / address / dummy phases, then n data bytes
     * written from tx (rx == NULL) or read into rx (tx == NULL) */
    int (*qspi_command)(void* ctx, int bus, const mcs_qspi_cmd_t* cmd, const uint8_t* tx, uint8_t* rx, size_t n);

    int (*can_open)(void* ctx, int bus, uint32_t bitrate);
    int (*can_send)(void* ctx, int bus, const mcs_can_frame_t* f, uint32_t timeout_ms);
    int (*can_recv)(void* ctx, int bus, mcs_can_frame_t* f, uint32_t timeout_ms);  /* MCS_HAL_ETIMEOUT if none */

    int (*wdt_start)(void* ctx, uint32_t timeout_ms);
    int (*wdt_feed)(void* ctx);

    int (*rtc_get)(void* ctx, uint32_t* unix_seconds);
    int (*rtc_set)(void* ctx, uint32_t unix_seconds);

    uint32_t (*micros)(void* ctx);                 /* free-running microsecond counter */
    void (*delay_us)(void* ctx, uint32_t us);      /* busy-wait */
    int (*reset)(void* ctx);                       /* normally does not return */
    int (*unique_id)(void* ctx, uint8_t* buf, size_t cap);  /* returns length */
    uint32_t cpu_hz;

    /* Drain one driver event (alternative to mcs_hal_post, e.g. from an RTOS
     * queue). Return 1 when *ev was filled, 0 when empty. */
    int (*poll_event)(void* ctx, mcs_hal_event_t* ev);
} mcs_hal_t;

/* Register the C# peripheral classes the board supports, plus `Hal` and the
 * object classes Pin / I2cDevice / SpiDevice / CanFrame. The HAL struct must
 * outlive the VM. Stored in MCS_EXT_HAL. */
void mcs_hal_open_lib(mcs_vm_t* vm, const mcs_hal_t* hal);
/* Release the VM's HAL slot (call before mcs_free when VMs are recycled). */
void mcs_hal_close_lib(mcs_vm_t* vm);
/* The board table opened on this VM (NULL if none). */
const mcs_hal_t* mcs_hal_get(mcs_vm_t* vm);

/* Queue a driver event. Safe from interrupt handlers (lock-free single
 * producer ring; define MCS_HAL_CRITICAL_ENTER/EXIT when ISRs of different
 * priorities post concurrently - on Cortex-M PRIMASK is used by default).
 * Returns false when the queue is full (the event is counted as dropped). */
bool mcs_hal_post(int type, int source, int32_t value);
uint32_t mcs_hal_dropped_events(void);

/* Dispatch queued events to their C# callbacks on the VM thread. Returns the
 * number of callbacks run or a negative mcs_result_t if one failed. Called by
 * Hal.Poll() / Hal.Run(), the shell and mcs_sched_run(); call it from your own
 * main loop when you drive the VM yourself. */
int mcs_hal_poll(mcs_vm_t* vm);

/* Generic pin-name parser used when the board has no pin_lookup:
 *   "13", "GPIO13", "GP13", "IO13", "D13", "A0"(=adc_base+0) -> number
 *   "PA5", "PB12" -> port * 16 + pin        (STM32, GD32, CH32 ...)
 *   "P0.13", "P1.02" -> port * 32 + pin     (nRF, LPC) */
int mcs_hal_parse_pin(const char* name);

/* ---- simulator board: used by tests, the host CLI (--sim) and examples ----
 * GPIO: 64 pins; outputs read back their value; inputs read `inputs[pin]`
 *       (pull-ups read 1 unless driven). An output with an interrupt enabled
 *       fires on its own edges (like a jumper to an input).
 * UART: per-port loopback, i.e. a TX-RX jumper.
 * I2C:  0x48 temperature sensor (pointer reg 0 = 25.0 C), 0x50 256-byte EEPROM,
 *       0x68 register file (WHO_AM_I at 0x75 = 0x68, like an MPU-6050).
 * SPI:  MOSI-MISO loopback.   ADC: 12-bit, returns adc[ch].   PWM: stored.
 * DAC:  12-bit, stored.       I2S: loopback FIFO.          CAN: loopback.
 * QSPI: 4 KB NOR flash (JEDEC ID EF 40 16; 03/0B/6B/EB read, 02/32 program,
 *       20 sector erase, 06 write enable, 05 status).
 * Timers fire from `clock` (or a virtual 1 ms per poll when NULL). */
#ifndef MCS_HAL_SIM_FLASH
#define MCS_HAL_SIM_FLASH 4096
#endif
typedef struct {
    uint8_t mode[64], out[64], inputs[64], irq[64];
    uint8_t uart_buf[4][256];
    uint16_t uart_head[4], uart_tail[4];
    uint32_t uart_baud[4];
    uint8_t temp_ptr;
    int16_t temp_centi;             /* sensor reading, 1/100 C */
    uint8_t eeprom[256], eeprom_ptr;
    uint8_t regs[128], reg_ptr;     /* device 0x68 */
    uint16_t adc[16];
    uint32_t pwm_freq[8];
    uint16_t pwm_duty[8];           /* permille */
    uint16_t pwm_duty16[8];
    uint16_t dac[4];
    uint32_t timer_period[4], timer_next[4], timer_fired[4];
    uint8_t timer_on[4], timer_periodic[4];
    uint8_t i2s_buf[512];
    uint16_t i2s_len;
    mcs_i2s_cfg_t i2s_cfg;
    mcs_can_frame_t can_q[8];
    uint8_t can_head, can_count;
    uint8_t flash[MCS_HAL_SIM_FLASH];
    uint8_t flash_wel;
    uint32_t wdt_timeout, wdt_feeds;
    uint32_t rtc_base;
    uint32_t virt_us;
    int reset_requested;
    mcs_uart_cfg_t uart_cfg[4];
    mcs_spi_cfg_t spi_cfg;
    uint32_t i2c_freq;
    mcs_write_fn log;               /* optional trace of every operation */
    void* log_ud;
    uint32_t (*clock_us)(void* ud); /* optional real clock for timers / Hal.Micros */
    void* clock_ud;
} mcs_hal_sim_t;
void mcs_hal_sim_init(mcs_hal_t* hal, mcs_hal_sim_t* sim);
/* Drive a simulated input pin (posts a GPIO event when its interrupt is armed). */
void mcs_hal_sim_set_input(mcs_hal_sim_t* sim, int pin, int level);

#ifdef __cplusplus
}
#endif
#endif
