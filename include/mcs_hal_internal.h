/*
 * MicroCS - private glue between the HAL library sources (modules/hal/).
 * Not part of the public API: names and layout change between versions.
 */
#ifndef MCS_HAL_INTERNAL_H
#define MCS_HAL_INTERNAL_H
#include "mcs_hal.h"
#include "mcs_mcp2515.h"
#if MCS_ENABLE_HAL

#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define ST() ((mcs__hal_state_t*)mcs_get_ext(vm, MCS_EXT_HAL))
#define HAL() (ST()->hal)
#define CTX() (HAL()->ctx)
#define INT_ARG(i, var) int var = (int)mcs_to_int(vm, argv[i]); if (mcs_has_exception(vm)) return mcs_null()
#define OPT_INT(i, var, def) int var = argc > (i) ? (int)mcs_to_int(vm, argv[i]) : (def); if (mcs_has_exception(vm)) return mcs_null()
#define NEED(fn, what) if (!HAL()->fn) return mcs__hal_fail(vm, what, MCS_HAL_ENOTSUP)
#define RC(what, expr) do { int _rc = (expr); if (_rc < 0) return mcs__hal_fail(vm, what, _rc); } while (0)

/* pinned reference list: callbacks first, then MCS__DMA_REFS per DMA slot */
#define MCS__DMA_REFS 4               /* transfer object, buffer a, buffer b, callback */
#define MCS__REF_DMA(slot, k) (MCS_HAL_MAX_CALLBACKS + (slot) * MCS__DMA_REFS + (k))
#define MCS__REF_COUNT (MCS_HAL_MAX_CALLBACKS + MCS_HAL_MAX_DMA * MCS__DMA_REFS)
#define MCS__DEFER 8                  /* events held back while a callback waits for DMA */

/* what a DMA slot occupies: a transfer waits for an earlier one on the same resource */
enum { MCS__DMA_FREE = 0, MCS__DMA_SPI, MCS__DMA_I2C, MCS__DMA_UART_TX, MCS__DMA_UART_RX, MCS__DMA_QSPI, MCS__DMA_MEM };
typedef struct {
    uint8_t kind;                     /* MCS__DMA_*, FREE = unused */
    uint8_t cs_high;                  /* chip select active level */
    int16_t bus;
    int16_t cs;                       /* chip select released at completion, -1 = none */
    uint16_t id;
    uint8_t polled;                   /* completed by the library polling (no async driver call) */
    uint8_t fill;                     /* Dma.Fill value (the DMA source byte) */
    uint32_t done;                    /* bytes so far (polled transfers) */
} mcs__dma_slot_t;

typedef struct {
    uint8_t kind;
    uint16_t source;
} mcs__hal_cb_t;

typedef struct {
    mcs_mcp2515_t mcp;
    mcs_can_frame_t pending;
    uint8_t backend;                  /* 0 closed, 1 native, 2 MCP2515 */
    uint8_t ready, notified, subscribed, mode;
} mcs__can_bus_t;

typedef struct {
    const mcs_hal_t* hal;
    int self_pin;                     /* pinned userdata holding this struct */
    int cb_pin;                       /* pinned List: MCS__REF_COUNT references */
    mcs__hal_cb_t cb[MCS_HAL_MAX_CALLBACKS];
    mcs_spi_cfg_t spi_cur[MCS_HAL_MAX_BUSES];
    uint8_t spi_valid[MCS_HAL_MAX_BUSES];
    int16_t spi_cs[MCS_HAL_MAX_BUSES];    /* default chip select from SpiConfig.Cs, -1 = none */
    uint8_t spi_cs_high[MCS_HAL_MAX_BUSES];
    uint8_t i2s_bits[MCS_HAL_MAX_BUSES];
    mcs__can_bus_t can[MCS_HAL_MAX_BUSES];
    int wake_pin;
    uint8_t wake_high;
    uint8_t polling;
    uint8_t no_events;                /* MCS_HAL_NO_EVENTS: OS-thread VM, callbacks belong to the main VM */
    uint8_t ndefer;
    mcs__dma_slot_t dma[MCS_HAL_MAX_DMA];
    mcs_hal_event_t defer[MCS__DEFER];
} mcs__hal_state_t;

mcs_value_t mcs__hal_fail(mcs_vm_t* vm, const char* op, int rc);
mcs_value_t mcs__range_fail(mcs_vm_t* vm, const char* what);
int mcs__pin_arg(mcs_vm_t* vm, mcs_value_t v);
bool mcs__to_bytes(mcs_vm_t* vm, mcs_value_t v, uint8_t* buf, size_t* n);
mcs_value_t mcs__bytes_val(mcs_vm_t* vm, const uint8_t* b, size_t n);
bool mcs__check_callable(mcs_vm_t* vm, mcs_value_t fn);
/* spi_transfer with NULL tx (send 0xFF) / rx (discard) allowed, any length */
extern uint32_t mcs__spi_half;
int mcs__spi_raw(const mcs_hal_t* h, int bus, const uint8_t* tx, uint8_t* rx, size_t n);

void mcs__can_open(mcs_vm_t* vm);
void mcs__can_close_all(mcs_vm_t* vm);
int mcs__can_event(mcs_vm_t* vm, int bus, mcs_hal_event_t* ev);
void mcs__can_subscribe(mcs_vm_t* vm, int bus, bool enabled);
mcs_value_t mcs__can_onreceive(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);

void mcs__power_open(mcs_vm_t* vm);
bool mcs__hal_pin_has_irq(mcs_vm_t* vm, int pin);

/* config classes (mcs_hal_cfg.c) */
enum { MCS__CFG_GPIO = 1, MCS__CFG_UART, MCS__CFG_I2C, MCS__CFG_SPI, MCS__CFG_QSPI, MCS__CFG_PWM, MCS__CFG_ADC, MCS__CFG_CAN };
/* the C struct inside a config object of `kind`, or NULL (no exception) */
void* mcs__cfg_get(mcs_value_t v, int kind);
void mcs__cfg_open(mcs_vm_t* vm, const mcs_hal_t* h);
/* the "a field the board cannot honour" check for fallbacks; NULL = all default */
const char* mcs__cfg_nondefault(const void* cfg, int kind, const char* const* allowed);

/* DMA (mcs_hal_dma.c) */
#if MCS_ENABLE_HAL_DMA
void mcs__dma_open(mcs_vm_t* vm, const mcs_hal_t* h);
/* a DmaBuffer's memory, or false (no exception) when v is not one */
bool mcs__dmabuf(mcs_value_t v, uint8_t** data, size_t* len);
/* wait until no transfer of `kind` on `bus` is in flight (bus < 0: any of kind). false = aborted */
bool mcs__dma_idle(mcs_vm_t* vm, int kind, int bus);
/* MCS_HAL_EV_DMA arrived; returns 1 when a C# callback ran, < 0 on error */
int mcs__dma_event(mcs_vm_t* vm, const mcs_hal_event_t* ev);
/* events (DMA completions, deferred ones) for Wait loops; dma_only inside callbacks */
void mcs__hal_pump(mcs_vm_t* vm, bool dma_only);
void mcs__dma_close(mcs_vm_t* vm);
/* complete polled transfers (called by mcs_hal_poll) */
void mcs__dma_tick(mcs_vm_t* vm);
/* start asynchronous transfers: tx / rx are DmaBuffers (tx may also be byte[] / string,
 * copied to a new buffer) or null; returns the DmaTransfer */
mcs_value_t mcs__spi_async(mcs_vm_t* vm, int bus, int cs, int cs_high, mcs_value_t tx, mcs_value_t rx);
mcs_value_t mcs__i2c_async(mcs_vm_t* vm, int bus, int addr, mcs_value_t tx, mcs_value_t rx);
#else
/* MCS_ENABLE_HAL_DMA 0: no DmaBuffer / DmaTransfer / Dma classes, the ...Async members raise */
void mcs__hal_pump(mcs_vm_t* vm, bool dma_only);
static inline void mcs__dma_open(mcs_vm_t* vm, const mcs_hal_t* h) { (void)vm; (void)h; }
static inline bool mcs__dmabuf(mcs_value_t v, uint8_t** data, size_t* len) { (void)v; (void)data; (void)len; return false; }
static inline bool mcs__dma_idle(mcs_vm_t* vm, int kind, int bus) { (void)vm; (void)kind; (void)bus; return true; }
static inline int mcs__dma_event(mcs_vm_t* vm, const mcs_hal_event_t* ev) { (void)vm; (void)ev; return 0; }
static inline void mcs__dma_close(mcs_vm_t* vm) { (void)vm; }
static inline void mcs__dma_tick(mcs_vm_t* vm) { (void)vm; }
static inline mcs_value_t mcs__dma_off(mcs_vm_t* vm) {
    mcs_raise(vm, "NotSupportedException", "DMA transfers are not compiled in (MCS_ENABLE_HAL_DMA 0)");
    return mcs_null();
}
static inline mcs_value_t mcs__spi_async(mcs_vm_t* vm, int bus, int cs, int cs_high, mcs_value_t tx, mcs_value_t rx) {
    (void)bus; (void)cs; (void)cs_high; (void)tx; (void)rx; return mcs__dma_off(vm);
}
static inline mcs_value_t mcs__i2c_async(mcs_vm_t* vm, int bus, int addr, mcs_value_t tx, mcs_value_t rx) {
    (void)bus; (void)addr; (void)tx; (void)rx; return mcs__dma_off(vm);
}
#endif
/* last mcs_hal_error() text (cleared by the read) */
const char* mcs__hal_take_error(void);

#endif
#endif
