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
 *
 * v3 adds full peripheral configuration (mcs_*_config_t: pins, pulls, drive
 * strength, filters, DMA, buffer sizes ... with "board default" for every
 * field, plus named port-specific options) and DMA: asynchronous SPI / I2C /
 * UART / QSPI transfers and memory copies that complete with MCS_HAL_EV_DMA.
 * See docs/PERIPHERALS.md and docs/DMA.md.
 */
#ifndef MCS_HAL_H
#define MCS_HAL_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MCS_HAL_API_VERSION 3

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
#ifndef MCS_HAL_MAX_DMA
#define MCS_HAL_MAX_DMA 4        /* asynchronous (DMA) transfers in flight per VM, 1..8 */
#endif
#ifndef MCS_HAL_MAX_OPTS
#define MCS_HAL_MAX_OPTS 8       /* port-specific options per config object */
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

/* ---------------- v3: peripheral configuration ----------------
 * Every field has a "board default" value: 0 for numbers and tri-states,
 * -1 (MCS_PIN_DEFAULT) for pins. Initialise with the mcs_*_config_init()
 * helpers (or the C# config classes) and set only what you need. A port that
 * cannot honour a non-default field returns MCS_HAL_ENOTSUP and may name the
 * field with mcs_hal_error("..."), which is appended to the C# exception. */
#define MCS_PIN_DEFAULT (-1)
enum { MCS_AUTO = 0, MCS_ON = 1, MCS_OFF = 2 };   /* tri-state: board default / enable / disable */

/* Port-specific options: a NULL-terminated table in mcs_hal_t.options lists the
 * names a board accepts ("i2c.timing", "spi.queue_size" ...); config objects
 * carry (index, value) pairs set with cfg.Set("timing", 0x10909CEC) in C#. */
typedef struct {
    const char* periph;            /* "GPIO", "UART", "I2C", "SPI", "QSPI", "PWM", "ADC" */
    const char* name;              /* "timing" */
    int32_t min, max;
    const char* help;              /* one line shown by Hal.Options() */
} mcs_hal_option_t;
typedef struct {
    uint8_t n;
    uint8_t id[MCS_HAL_MAX_OPTS];  /* index into mcs_hal_t.options */
    int32_t value[MCS_HAL_MAX_OPTS];
} mcs_hal_opts_t;
/* value of option `name` (periph "SPI"...) or `def` when not set */
int32_t mcs_hal_opt(const mcs_hal_option_t* table, const mcs_hal_opts_t* o, const char* periph, const char* name, int32_t def);

enum { MCS_PULL_DEFAULT = 0, MCS_PULL_NONE = 1, MCS_PULL_UP = 2, MCS_PULL_DOWN = 3, MCS_PULL_BOTH = 4 };
enum { MCS_SPEED_DEFAULT = 0, MCS_SPEED_LOW = 1, MCS_SPEED_MEDIUM = 2, MCS_SPEED_HIGH = 3, MCS_SPEED_VERY_HIGH = 4 };
typedef struct {
    uint8_t mode;                  /* MCS_GPIO_INPUT / OUTPUT / OPEN_DRAIN / ANALOG ... */
    uint8_t pull;                  /* MCS_PULL_*: DEFAULT = what the mode implies */
    uint8_t speed;                 /* slew rate / output speed, MCS_SPEED_* */
    uint8_t drive_ma;              /* drive strength in mA, 0 = default (nearest supported) */
    uint8_t schmitt;               /* tri-state: input hysteresis */
    uint8_t hold;                  /* tri-state: latch the pad state (deep sleep / reset) */
    uint8_t invert;                /* tri-state: invert input and output in hardware */
    int8_t initial;                /* output level set before the pin turns into an output, -1 = keep */
    int16_t alternate;             /* alternate function / pad mux number, -1 = GPIO */
    uint16_t filter_ns;            /* input glitch filter, 0 = off / default */
    mcs_hal_opts_t opts;
} mcs_gpio_config_t;

typedef struct {
    mcs_uart_cfg_t frame;          /* baud, data bits, parity, stop bits, flow control */
    int16_t tx, rx, rts, cts;      /* pins, MCS_PIN_DEFAULT = board default */
    uint16_t rx_buffer, tx_buffer; /* driver buffer sizes in bytes, 0 = default */
    uint8_t invert_tx, invert_rx;  /* bool: inverted line levels */
    uint8_t rs485;                 /* bool: half duplex, RTS drives the transceiver DE pin */
    uint8_t dma;                   /* tri-state */
    uint16_t rx_timeout;           /* idle time in symbols before buffered bytes are reported, 0 = default */
    mcs_hal_opts_t opts;
} mcs_uart_config_t;

typedef struct {
    uint32_t freq_hz;              /* 0 = 100 kHz */
    int16_t sda, scl;              /* pins */
    uint8_t pullups;               /* tri-state: internal pull-ups */
    uint8_t dma;                   /* tri-state */
    uint16_t timeout_ms;           /* 0 = default */
    uint8_t glitch_filter;         /* digital filter length in clock cycles, 0 = default */
    uint8_t analog_filter;         /* tri-state */
    mcs_hal_opts_t opts;
} mcs_i2c_config_t;

typedef struct {
    mcs_spi_cfg_t bus;             /* frequency, mode, word size, bit order */
    int16_t sck, mosi, miso;       /* pins */
    int16_t cs;                    /* default chip select for SPI.Transfer(bus, data), driven by MicroCS */
    uint8_t cs_active_high;        /* bool */
    uint8_t three_wire;            /* bool: half duplex on MOSI; spi_transfer(_async) then gets exactly one of tx / rx (the other NULL), any length */
    uint8_t dma;                   /* tri-state */
    uint8_t reserved;
    uint16_t input_delay_ns;       /* MISO sampling delay (long wires, level shifters) */
    uint32_t max_transfer;         /* largest DMA transfer in bytes, 0 = default */
    mcs_hal_opts_t opts;
} mcs_spi_config_t;

typedef struct {
    uint32_t freq_hz;              /* 0 = 10 MHz */
    uint8_t mode;                  /* SPI mode 0 or 3 */
    uint8_t ddr;                   /* bool: double data rate */
    uint8_t dma;                   /* tri-state */
    uint8_t cs_high_cycles;        /* minimum CS high time between commands, 0 = default */
    uint8_t sample_shift;          /* half-cycle sample delay, 0 = default */
    uint8_t flash_size_log2;       /* memory size as 2^n bytes (memory-mapped / STM32 FSIZE), 0 = default */
    int16_t sck, cs, io[4];        /* pins: io0 (MOSI) io1 (MISO) io2 (WP) io3 (HOLD) */
    mcs_hal_opts_t opts;
} mcs_qspi_config_t;

typedef struct {
    uint32_t freq_hz;              /* 0 = keep */
    int16_t pin;                   /* output pin for the channel, MCS_PIN_DEFAULT = board default */
    uint8_t resolution_bits;       /* duty resolution, 0 = best for the frequency */
    uint8_t invert;                /* bool: active-low output */
    uint8_t center_aligned;        /* bool: phase-correct / centre-aligned counting */
    mcs_hal_opts_t opts;
} mcs_pwm_config_t;

enum { MCS_ADC_ATTEN_DEFAULT = 0, MCS_ADC_ATTEN_0DB = 1, MCS_ADC_ATTEN_2_5DB = 2,
       MCS_ADC_ATTEN_6DB = 3, MCS_ADC_ATTEN_12DB = 4 };
typedef struct {
    uint8_t resolution_bits;       /* 0 = default */
    uint8_t attenuation;           /* MCS_ADC_ATTEN_* (input range), 0 = default */
    uint8_t oversample;            /* hardware averaging: samples per reading (power of two), 0 = off */
    uint8_t reserved;
    uint16_t sample_time;          /* sampling time in ADC clock cycles, 0 = default */
    mcs_hal_opts_t opts;
} mcs_adc_config_t;

void mcs_gpio_config_init(mcs_gpio_config_t* c, int mode);
void mcs_uart_config_init(mcs_uart_config_t* c, uint32_t baud);
void mcs_i2c_config_init(mcs_i2c_config_t* c, uint32_t freq_hz);
void mcs_spi_config_init(mcs_spi_config_t* c, uint32_t freq_hz, int mode);
void mcs_qspi_config_init(mcs_qspi_config_t* c, uint32_t freq_hz);
void mcs_pwm_config_init(mcs_pwm_config_t* c, uint32_t freq_hz);
void mcs_adc_config_init(mcs_adc_config_t* c);
/* Explain the last MCS_HAL_ENOTSUP / EINVAL ("drive_ma: 2, 4, 8 or 12 mA"); the
 * text must be static. Call from the VM thread only. */
void mcs_hal_error(const char* detail);

/* ---------------- v3: DMA ----------------
 * Asynchronous transfers get an id from the library and finish with
 * mcs_hal_post(MCS_HAL_EV_DMA, id, result) - bytes transferred, or a negative
 * MCS_HAL_E* code - usually from the DMA-complete interrupt. Buffers passed to
 * *_async functions stay valid and untouched until that event. */
enum { MCS_DMA_SRC_FIXED = 1,      /* dma_copy: repeat src[0] (fill) / read one peripheral register */
       MCS_DMA_DST_FIXED = 2 };    /* dma_copy: write every byte to the same address (peripheral register) */

/* Events posted by drivers (often from ISRs) and dispatched to C# callbacks
 * on the VM thread by mcs_hal_poll(). */
enum { MCS_HAL_EV_GPIO = 1,      /* source = pin,   value = level        */
       MCS_HAL_EV_TIMER = 2,     /* source = timer, value = fire count   */
       MCS_HAL_EV_UART = 3,      /* source = port,  value = bytes ready  */
       MCS_HAL_EV_CAN = 4,       /* source = bus,   value = frames ready */
       MCS_HAL_EV_DMA = 5,       /* source = transfer id, value = bytes or MCS_HAL_E* */
       MCS_HAL_EV_NET = 6,       /* source = MCS_NET_EV_*, value = event data (mcs_net.h) */
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

    /* ---------------- v3 (all optional) ---------------- */
    /* Full configuration. NULL = the library uses the v1/v2 calls above and
     * raises NotSupportedException for fields they cannot express. */
    int (*gpio_configure)(void* ctx, int pin, const mcs_gpio_config_t* cfg);
    int (*uart_configure)(void* ctx, int port, const mcs_uart_config_t* cfg);
    int (*i2c_configure)(void* ctx, int bus, const mcs_i2c_config_t* cfg);
    int (*spi_configure)(void* ctx, int bus, const mcs_spi_config_t* cfg);
    int (*qspi_configure)(void* ctx, int bus, const mcs_qspi_config_t* cfg);
    int (*pwm_configure)(void* ctx, int channel, const mcs_pwm_config_t* cfg);
    int (*adc_configure)(void* ctx, int channel, const mcs_adc_config_t* cfg);
    const mcs_hal_option_t* options;   /* port-specific option names, NULL-terminated table */

    /* DMA. Memory for DmaBuffer: dma_alloc returns DMA-capable memory (internal
     * SRAM, outside DTCM, cache-line aligned ...). NULL = VM heap aligned to
     * dma_align (0 = 4). */
    void* (*dma_alloc)(void* ctx, size_t n);
    void (*dma_free)(void* ctx, void* p);
    uint8_t dma_align;
    uint8_t dma_channels;              /* hardware channels free for MicroCS, 0 = no DMA engine */
    /* memory copy / fill (flags MCS_DMA_*), completes with MCS_HAL_EV_DMA */
    int (*dma_copy)(void* ctx, void* dst, const void* src, size_t n, unsigned flags, int id);
    /* stop transfer `id` (best effort); no completion event follows */
    int (*dma_abort)(void* ctx, int id);
    /* Asynchronous peripheral transfers (return < 0 when they cannot start).
     * NULL, or MCS_HAL_ENOTSUP for one particular request (dma_copy fill, a
     * size the engine cannot do ...) = the library runs the synchronous call
     * and completes at once. */
    int (*spi_transfer_async)(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n, int id);
    int (*i2c_transfer_async)(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn, int id);
    int (*uart_write_async)(void* ctx, int port, const uint8_t* data, size_t n, int id);
    int (*uart_read_async)(void* ctx, int port, uint8_t* buf, size_t n, int id);   /* completes when n bytes arrived */
    int (*qspi_command_async)(void* ctx, int bus, const mcs_qspi_cmd_t* cmd, const uint8_t* tx, uint8_t* rx, size_t n, int id);
} mcs_hal_t;

/* Register the C# peripheral classes the board supports, plus `Hal` and the
 * object classes Pin / I2cDevice / SpiDevice / CanFrame. The HAL struct must
 * outlive the VM. Stored in MCS_EXT_HAL. */
void mcs_hal_open_lib(mcs_vm_t* vm, const mcs_hal_t* hal);
/* Helpers for driver front ends (mcs_driver.h) and your own natives:
 * a pin given as a number or a board name ("GP16", "PA5", "NEOPIXEL") - raises
 * ArgumentException and returns -1 when unknown; and the C# exception for a
 * negative MCS_HAL_E* code ("op: not supported" ...), returns null. */
int mcs_hal_pin_arg(mcs_vm_t* vm, mcs_value_t v);
mcs_value_t mcs_hal_raise(mcs_vm_t* vm, const char* op, int rc);
/* Same with MCS_HAL_* flags. MCS_HAL_NO_EVENTS: the VM never dispatches interrupt /
 * timer / UART / CAN events (VMs on OS threads: the event queue has one consumer, the
 * main VM); registering a callback raises InvalidOperationException. */
#define MCS_HAL_NO_EVENTS 1u
void mcs_hal_open_lib_ex(mcs_vm_t* vm, const mcs_hal_t* h, unsigned flags);
/* Release the VM's HAL slot (call before mcs_free when VMs are recycled). */
void mcs_hal_close_lib(mcs_vm_t* vm);
/* The board table opened on this VM (NULL if none). */
const mcs_hal_t* mcs_hal_get(mcs_vm_t* vm);
/* Configure SPI `bus` for a driver: calls spi_open only when the settings differ from
 * the last ones used on that bus by this VM (C# SPI / SpiDevice share the cache).
 * Returns 0, the spi_open result, or MCS_HAL_ENOTSUP without a HAL. */
int mcs_hal_spi_config(mcs_vm_t* vm, int bus, const mcs_spi_cfg_t* cfg);

/* Queue a driver event. Safe from interrupt handlers (lock-free single
 * producer ring; define MCS_HAL_CRITICAL_ENTER/EXIT when ISRs of different
 * priorities post concurrently - on Cortex-M PRIMASK is used by default).
 * Returns false when the queue is full (the event is counted as dropped). */
bool mcs_hal_post(int type, int source, int32_t value);
uint32_t mcs_hal_dropped_events(void);

/* Module event handlers: events of `type` (MCS_HAL_EV_NET .. MCS_HAL_EV_USER - 1) go to fn
 * on the VM thread (return 1 = a callback ran, 0 = nothing to do, < 0 = -mcs_result_t).
 * Process-wide; fn NULL removes it. Used by the network module for Wifi.OnEvent. */
typedef int (*mcs_hal_event_fn)(mcs_vm_t* vm, const mcs_hal_event_t* ev);
void mcs_hal_set_event_handler(int type, mcs_hal_event_fn fn);
/* true when this VM runs interrupt / timer / network callbacks (false for OS-thread VMs
 * opened with MCS_HAL_NO_EVENTS, or without the HAL library) */
bool mcs_hal_events(mcs_vm_t* vm);

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
 *       Optional: an SSD1306 / SH1106 OLED (mcs_hal_sim_attach_oled, CLI --oled).
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
    struct mcs_sim_oled* oled;      /* optional I2C OLED (mcs_hal_sim_attach_oled) */
    /* v3: last configuration per peripheral, DMA bookkeeping */
    mcs_gpio_config_t gpio_cfg;     /* last GPIO.Configure (pin in gpio_cfg_pin) */
    int gpio_cfg_pin;
    mcs_uart_config_t uart_full[4];
    mcs_i2c_config_t i2c_cfg;
    mcs_spi_config_t spi_full;
    mcs_qspi_config_t qspi_cfg;
    mcs_pwm_config_t pwm_cfg;       /* last PWM.Configure / ADC.Configure */
    mcs_adc_config_t adc_cfg;
    uint32_t dma_started, dma_bytes;
    uint8_t* uart_rx_buf;           /* pending UART.ReadAsync */
    uint32_t uart_rx_len, uart_rx_got;
    int uart_rx_port, uart_rx_id;
} mcs_hal_sim_t;
void mcs_hal_sim_init(mcs_hal_t* hal, mcs_hal_sim_t* sim);

/* Optional simulated OLED on the I2C bus - an SSD1306 (128x64 / 128x32) or SH1106
 * (132-column RAM) controller as u8g2 / Adafruit / MicroPython drivers drive it:
 * command and data streams (control byte 0x00 / 0x40, Co bit), page, horizontal and
 * vertical addressing, A0/A1 C0/C8 flips, A6/A7 invert, AE/AF on/off. The host CLI
 * attaches one with --oled. Set addr / sh1106 / width / height (and on_frame), then
 * attach it after mcs_hal_sim_init(). on_frame runs when the last column of the last
 * page was written (one full screen update). */
typedef struct mcs_sim_oled {
    uint8_t addr;                   /* 7-bit I2C address, e.g. 0x3C */
    uint8_t sh1106;                 /* 1 = SH1106 (132 columns, the panel shows 2..129) */
    uint8_t width, height;          /* visible pixels: 128 x 64 or 128 x 32 */
    uint8_t ram[8][132];
    uint8_t col, page, col_lo, col_hi, page_lo, page_hi, mode;
    uint8_t on, invert, all_on, seg_remap, com_rev, contrast;
    uint8_t cmd[8], cmd_n, cmd_need;
    uint32_t frames, bytes;
    void (*on_frame)(void* ud, const struct mcs_sim_oled* oled);
    void* ud;
} mcs_sim_oled_t;
void mcs_hal_sim_attach_oled(mcs_hal_sim_t* sim, mcs_sim_oled_t* oled);
int mcs_sim_oled_pixel(const mcs_sim_oled_t* oled, int x, int y);   /* lit (1) or dark (0) */
/* The screen as text, two pixel rows per line (UTF-8 half blocks) in a frame;
 * returns the length (cap >= (width + 3) * 3 * (height / 2 + 2) + 64 always fits). */
size_t mcs_sim_oled_render(const mcs_sim_oled_t* oled, char* buf, size_t cap);
/* Drive a simulated input pin (posts a GPIO event when its interrupt is armed). */
void mcs_hal_sim_set_input(mcs_hal_sim_t* sim, int pin, int level);

#ifdef __cplusplus
}
#endif
#endif
