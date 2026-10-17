/*
 * MicroCS - C# peripheral API over mcs_hal_t.
 *
 * Static classes: GPIO UART I2C SPI ADC DAC PWM Timer I2S QSPI CAN Watchdog RTC Hal
 * Object classes: Pin I2cDevice SpiDevice CanFrame
 * Each class is registered only when the board provides the functions it needs.
 * Driver events (GPIO edges, timer ticks, ...) are queued from ISRs with
 * mcs_hal_post() and run as C# callbacks on the VM thread by mcs_hal_poll().
 */
#include "mcs_hal.h"
#include "mcs_driver.h"
#include "mcs_hal_internal.h"
#include "mcs_net.h"
#include "mcs_ble.h"
#if MCS_ENABLE_HAL
#include <string.h>
#include <stdio.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
/* ------------------------------------------------------------ state: mcs_hal_internal.h */
enum { CB_FREE = 0, CB_GPIO, CB_TIMER, CB_UART, CB_CAN, CB_USER };
typedef mcs__hal_state_t hal_state_t;
typedef mcs__hal_cb_t hal_cb_t;
#define hal_fail mcs__hal_fail
#define range_fail mcs__range_fail
#define to_bytes mcs__to_bytes
#define bytes_val mcs__bytes_val
#define pin_arg mcs__pin_arg
#define check_callable mcs__check_callable

static const char* g_err_detail;   /* mcs_hal_error(): explains the next failure */
void mcs_hal_error(const char* detail) { g_err_detail = detail; }
const char* mcs__hal_take_error(void) { const char* d = g_err_detail; g_err_detail = NULL; return d; }
mcs_value_t hal_fail(mcs_vm_t* vm, const char* op, int rc) {
    const char* why = rc == MCS_HAL_ENOTSUP ? "not supported" : rc == MCS_HAL_ETIMEOUT ? "timeout"
                    : rc == MCS_HAL_ENODEV ? "no device" : rc == MCS_HAL_EBUSY ? "busy"
                    : rc == MCS_HAL_EINVAL ? "invalid argument" : "failed";
    const char* d = mcs__hal_take_error();
    const char* sep = d ? " - " : "";
    if (!d) d = "";
    if (rc == MCS_HAL_ETIMEOUT) mcs_raise(vm, "TimeoutException", "%s: %s%s%s", op, why, sep, d);
    else if (rc == MCS_HAL_ENOTSUP) mcs_raise(vm, "NotSupportedException", "%s: %s%s%s", op, why, sep, d);
    else if (rc == MCS_HAL_EINVAL) mcs_raise(vm, "ArgumentException", "%s: %s%s%s", op, why, sep, d);
    else mcs_raise(vm, "IOException", "%s: %s (%d)%s%s", op, why, rc, sep, d);
    return mcs_null();
}
mcs_value_t mcs_hal_raise(mcs_vm_t* vm, const char* op, int rc) { return hal_fail(vm, op, rc); }
mcs_value_t range_fail(mcs_vm_t* vm, const char* what) {
    mcs_raise(vm, "ArgumentOutOfRangeException", "%s out of range", what);
    return mcs_null();
}

/* byte[] / List<byte> / string / null -> buffer */
bool to_bytes(mcs_vm_t* vm, mcs_value_t v, uint8_t* buf, size_t* n) {
    if (mcs_is_null(v)) { *n = 0; return true; }
    uint8_t* dd; size_t dl;
    if (mcs__dmabuf(v, &dd, &dl)) {
        if (dl > MCS_HAL_MAX_XFER) goto too_big;
        memcpy(buf, dd, dl); *n = dl;
        return true;
    }
    if (mcs_is_string(v)) {
        size_t l = mcs_strlen(v);
        if (l > MCS_HAL_MAX_XFER) goto too_big;
        memcpy(buf, mcs_cstr(v), l); *n = l;
        return true;
    }
    int k = mcs_obj_kind(v);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "expected byte[], DmaBuffer or string"); return false; }
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
/* data as a pointer: a DmaBuffer is used in place (any size), anything else is
 * copied into `stack` (MCS_HAL_MAX_XFER bytes) */
static bool bytes_ref(mcs_vm_t* vm, mcs_value_t v, uint8_t* stack, const uint8_t** p, size_t* n) {
    uint8_t* dd;
    if (mcs__dmabuf(v, &dd, n)) { *p = dd; return true; }
    *p = stack;
    return to_bytes(vm, v, stack, n);
}
mcs_value_t bytes_val(mcs_vm_t* vm, const uint8_t* b, size_t n) {
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
/* pin given as a number or a board name ("PA5", "GPIO13") */
int pin_arg(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_is_string(v)) {
        const mcs_hal_t* h = HAL();
        int p = h->pin_lookup ? h->pin_lookup(h->ctx, mcs_cstr(v)) : -1;
        if (p < 0) p = mcs_hal_parse_pin(mcs_cstr(v));
        if (p < 0) { mcs_raise(vm, "ArgumentException", "unknown pin '%s'", mcs_cstr(v)); return -1; }
        return p;
    }
    int p = (int)mcs_to_int(vm, v);
    return mcs_has_exception(vm) ? -1 : p;
}
/* GPIO level from bool / int / double: GPIO.Write(pin, 0) is low */
static int level_of(mcs_value_t v) {
    if (mcs_is_int(v) || v.type == MCS_T_CHAR) return v.as.i != 0;
#if MCS_ENABLE_FLOAT
    if (mcs_is_float(v)) return v.as.f != 0;
#endif
    return mcs_truthy(v);
}
int mcs_hal_pin_arg(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_hal_get(vm)) return pin_arg(vm, v);
    int p = mcs_is_string(v) ? mcs_hal_parse_pin(mcs_cstr(v)) : (int)mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (p < 0 && mcs_is_string(v)) mcs_raise(vm, "ArgumentException", "unknown pin '%s'", mcs_cstr(v));
    return p;
}
#define PIN_ARG(i, var) int var = pin_arg(vm, argv[i]); if (mcs_has_exception(vm)) return mcs_null()

/* ------------------------------------------------------------ pin names */
static int parse_uint(const char* s, const char** end) {
    int v = 0, any = 0;
    *end = s;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; if (v > 100000) return -1; }
    *end = s;
    return any ? v : -1;
}
static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int prefix(const char* s, const char* p) {   /* case-insensitive */
    size_t i = 0;
    for (; p[i]; i++) if (upper((unsigned char)s[i]) != p[i]) return 0;
    return (int)i;
}
int mcs_hal_parse_pin(const char* s) {
    if (!s || !*s) return -1;
    const char* e;
    int n;
    if (s[0] >= '0' && s[0] <= '9') { n = parse_uint(s, &e); return *e ? -1 : n; }
    /* P<port>.<pin>  (nRF, LPC) */
    if (upper((unsigned char)s[0]) == 'P' && s[1] >= '0' && s[1] <= '9') {
        int port = parse_uint(s + 1, &e);
        if (*e != '.') return -1;
        n = parse_uint(e + 1, &e);
        return (*e || n < 0 || n > 31) ? -1 : port * 32 + n;
    }
    /* P<letter><pin>  (STM32, GD32, CH32, AT32 ...) */
    if (upper((unsigned char)s[0]) == 'P' && upper((unsigned char)s[1]) >= 'A' && upper((unsigned char)s[1]) <= 'Z' &&
        s[2] >= '0' && s[2] <= '9') {
        n = parse_uint(s + 2, &e);
        return (*e || n < 0 || n > 15) ? -1 : (upper((unsigned char)s[1]) - 'A') * 16 + n;
    }
    static const char* const pre[] = { "GPIO", "GP", "IO", "D", "PIN" };
    for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) {
        int k = prefix(s, pre[i]);
        if (k && s[k] >= '0' && s[k] <= '9') { n = parse_uint(s + k, &e); return *e ? -1 : n; }
    }
    return -1;
}

/* ------------------------------------------------------------ event queue (ISR -> VM) */
#if !defined(MCS_HAL_CRITICAL_ENTER) && MCS_ENABLE_THREADS
/* OS threads: posts may come from both cores and from tasks as well as ISRs */
#include "mcs_os.h"
#define MCS_HAL_CRITICAL_ENTER() unsigned mcs__key = mcs_os_irq_lock()
#define MCS_HAL_CRITICAL_EXIT()  mcs_os_irq_unlock(mcs__key)
#endif
#if !defined(MCS_HAL_CRITICAL_ENTER)
#if defined(__GNUC__) && (defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8_1M_MAIN__))
#define MCS_HAL_CRITICAL_ENTER() uint32_t mcs__primask; __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mcs__primask) :: "memory")
#define MCS_HAL_CRITICAL_EXIT()  __asm volatile("msr primask, %0" :: "r"(mcs__primask) : "memory")
#else
#define MCS_HAL_CRITICAL_ENTER() ((void)0)
#define MCS_HAL_CRITICAL_EXIT()  ((void)0)
#endif
#endif
#if defined(__GNUC__)
#define MCS_HAL_BARRIER() __sync_synchronize()
#else
#define MCS_HAL_BARRIER() ((void)0)
#endif
#if (MCS_HAL_EVENT_QUEUE & (MCS_HAL_EVENT_QUEUE - 1)) != 0
#error "MCS_HAL_EVENT_QUEUE must be a power of two"
#endif
static volatile mcs_hal_event_t g_ring[MCS_HAL_EVENT_QUEUE];
static volatile uint32_t g_head, g_tail, g_dropped;

bool mcs_hal_post(int type, int source, int32_t value) {
    bool ok;
    MCS_HAL_CRITICAL_ENTER();
    uint32_t h = g_head;
    if (h - g_tail >= MCS_HAL_EVENT_QUEUE) { g_dropped++; ok = false; }
    else {
        volatile mcs_hal_event_t* e = &g_ring[h & (MCS_HAL_EVENT_QUEUE - 1)];
        e->type = (uint8_t)type; e->reserved = 0; e->source = (uint16_t)source; e->value = value;
        MCS_HAL_BARRIER();
        g_head = h + 1;
        ok = true;
    }
    MCS_HAL_CRITICAL_EXIT();
    return ok;
}
uint32_t mcs_hal_dropped_events(void) { return g_dropped; }
static bool ring_pop(mcs_hal_event_t* ev) {
    uint32_t t = g_tail;
    if (t == g_head) return false;
    MCS_HAL_BARRIER();
    volatile mcs_hal_event_t* e = &g_ring[t & (MCS_HAL_EVENT_QUEUE - 1)];
    ev->type = e->type; ev->reserved = 0; ev->source = e->source; ev->value = e->value;
    MCS_HAL_BARRIER();
    g_tail = t + 1;
    return true;
}

/* ------------------------------------------------------------ callbacks */
static int cb_find(hal_state_t* s, int kind, int source) {
    for (int i = 0; i < MCS_HAL_MAX_CALLBACKS; i++)
        if (s->cb[i].kind == kind && s->cb[i].source == (uint16_t)source) return i;
    return -1;
}
bool mcs__hal_pin_has_irq(mcs_vm_t* vm, int pin) {
    return ST() && cb_find(ST(), CB_GPIO, pin) >= 0;
}
/* fn null = remove. Returns false when the table is full. */
static bool cb_set(mcs_vm_t* vm, hal_state_t* s, int kind, int source, mcs_value_t fn) {
    mcs_value_t list = mcs_pinned(vm, s->cb_pin);
    int i = cb_find(s, kind, source);
    if (mcs_is_null(fn)) {
        if (i >= 0) { s->cb[i].kind = CB_FREE; mcs_set_index(list, (uint32_t)i, mcs_null()); }
        return true;
    }
    if (i < 0) for (i = 0; i < MCS_HAL_MAX_CALLBACKS && s->cb[i].kind != CB_FREE; i++) {}
    if (i >= MCS_HAL_MAX_CALLBACKS) return false;
    s->cb[i].kind = (uint8_t)kind; s->cb[i].source = (uint16_t)source;
    mcs_set_index(list, (uint32_t)i, fn);
    return true;
}
bool check_callable(mcs_vm_t* vm, mcs_value_t fn) {
    int k = mcs_obj_kind(fn);
    if (mcs_is_null(fn) || k == MCS_O_CLOSURE || k == MCS_O_NATIVE || k == MCS_O_BOUND || k == MCS_O_FUNCTION || k == MCS_O_OVERLOADS) return true;
    mcs_raise(vm, "ArgumentException", "expected a callback (lambda or method)");
    return false;
}
static mcs_value_t set_callback(mcs_vm_t* vm, int kind, int source, mcs_value_t fn) {
    if (!check_callable(vm, fn)) return mcs_null();
    if (ST()->no_events && !mcs_is_null(fn)) {
        mcs_raise(vm, "InvalidOperationException", "interrupt and timer callbacks run in the main script, not in a thread (send data to it with a Channel or Hal.Post)");
        return mcs_null();
    }
    if (!cb_set(vm, ST(), kind, source, fn))
        mcs_raise(vm, "InvalidOperationException", "too many callbacks (MCS_HAL_MAX_CALLBACKS = %d)", MCS_HAL_MAX_CALLBACKS);
    return mcs_null();
}

static mcs_hal_event_fn g_ev_handlers[MCS_HAL_EV_USER - MCS_HAL_EV_NET];
void mcs_hal_set_event_handler(int type, mcs_hal_event_fn fn) {
    if (type >= MCS_HAL_EV_NET && type < MCS_HAL_EV_USER) g_ev_handlers[type - MCS_HAL_EV_NET] = fn;
}
bool mcs_hal_events(mcs_vm_t* vm) {
    hal_state_t* s = (hal_state_t*)mcs_get_ext(vm, MCS_EXT_HAL);
    return s && s->hal && !s->no_events;
}
static int dispatch(mcs_vm_t* vm, hal_state_t* s, const mcs_hal_event_t* ev) {
    if (ev->type == MCS_HAL_EV_DMA) return mcs__dma_event(vm, ev);
    if (ev->type >= MCS_HAL_EV_NET && ev->type < MCS_HAL_EV_USER) {
        mcs_hal_event_fn fn = g_ev_handlers[ev->type - MCS_HAL_EV_NET];
        return fn ? fn(vm, ev) : 0;
    }
    int kind = ev->type == MCS_HAL_EV_GPIO ? CB_GPIO : ev->type == MCS_HAL_EV_TIMER ? CB_TIMER
             : ev->type == MCS_HAL_EV_UART ? CB_UART : ev->type == MCS_HAL_EV_CAN ? CB_CAN
             : ev->type >= MCS_HAL_EV_USER ? CB_USER : CB_FREE;
    if (kind == CB_FREE) return 0;
    int i = cb_find(s, kind, kind == CB_USER ? ev->type - MCS_HAL_EV_USER : ev->source);
    if (i < 0) return 0;
    mcs_value_t fn = mcs_index(mcs_pinned(vm, s->cb_pin), (uint32_t)i);
    mcs_value_t args[2];
    int full;
    switch (kind) {
    case CB_GPIO: args[0] = mcs_int(ev->source); args[1] = mcs_bool(ev->value != 0); full = 2; break;
    case CB_USER: args[0] = mcs_int(ev->source); args[1] = mcs_int(ev->value); full = 2; break;
    default:      args[0] = mcs_int(ev->value); full = 1; break;   /* timer: fire count; uart/can: pending */
    }
    int a = mcs_arity(fn);
    int argc = a < 0 || a >= full ? full : a;
    /* (level) for one-argument GPIO handlers, (value) for user events */
    const mcs_value_t* argp = (full == 2 && argc == 1) ? &args[1] : args;
    mcs_result_t r = mcs_call_value(vm, fn, argc, argp, NULL);
    return r == MCS_OK ? 1 : -(int)r;
}

static bool next_event(const mcs_hal_t* h, bool* driver_empty, mcs_hal_event_t* ev) {
    if (!*driver_empty) {              /* the driver's own queue first, once until empty */
        if (h->poll_event && h->poll_event(h->ctx, ev) == 1) return true;
        *driver_empty = true;
    }
    return ring_pop(ev);
}
int mcs_hal_poll(mcs_vm_t* vm) {
    hal_state_t* s = ST();
    if (!s || s->polling || s->no_events) return 0;
    s->polling = 1;
    const mcs_hal_t* h = s->hal;
    int ran = 0, err = 0;
    mcs_hal_event_t ev;
    /* CAN receive is polled once per subscribed bus, on every port. One-frame
     * lookahead leaves backpressure in the SDK/controller, with no heap growth. */
    for (int bus = 0; bus < MCS_HAL_MAX_BUSES; bus++) {
        int r = mcs__can_event(vm, bus, &ev);
        if (r > 0) r = dispatch(vm, s, &ev);
        if (r > 0) ran++;
        else if (r < 0) { err = r; break; }
    }
    /* events held back while a callback waited for a DMA transfer go first */
    mcs_hal_event_t held[MCS__DEFER];
    int nh = s->ndefer;
    memcpy(held, s->defer, sizeof(mcs_hal_event_t) * (size_t)nh);
    s->ndefer = 0;
    /* bounded: events posted by the callbacks themselves wait for the next poll */
    bool driver_empty = h->poll_event == NULL;
    for (int budget = MCS_HAL_EVENT_QUEUE * 2 + nh, i = 0; !err && budget > 0; budget--) {
        if (i < nh) ev = held[i++];
        else if (!next_event(h, &driver_empty, &ev)) break;
        if (ev.type == MCS_HAL_EV_CAN) continue; /* legacy ISR hints: handled above */
        int r = dispatch(vm, s, &ev);
        if (r > 0) ran++;
        else if (r < 0) {
            if (!err) err = r;
            if (mcs_has_exception(vm)) break;   /* nested in a script: let the exception propagate */
        }
    }
    if (!mcs_has_exception(vm)) mcs__dma_tick(vm);   /* UART.ReadAsync without a DMA driver */
    s->polling = 0;
    return err ? err : ran;
}
/* Wait loops (DmaTransfer.Wait ...): a normal poll, or - inside a callback, where
 * polls do not nest - only DMA completions, other events are kept for later */
void mcs__hal_pump(mcs_vm_t* vm, bool dma_only) {
    hal_state_t* s = ST();
    if (!s || s->no_events) return;
    if (!s->polling && !dma_only) { mcs_hal_poll(vm); return; }
    const mcs_hal_t* h = s->hal;
    bool driver_empty = h->poll_event == NULL;
    mcs_hal_event_t ev;
    for (int budget = MCS_HAL_EVENT_QUEUE; budget > 0 && next_event(h, &driver_empty, &ev); budget--) {
        if (ev.type == MCS_HAL_EV_DMA) { mcs__dma_event(vm, &ev); if (mcs_has_exception(vm)) return; }
        else if (s->ndefer < MCS__DEFER) s->defer[s->ndefer++] = ev;
        else g_dropped++;
    }
}
static int idle_poll(mcs_vm_t* vm, void* ud) { (void)ud; return mcs_hal_poll(vm); }

/* ------------------------------------------------------------- GPIO */
NATIVE(gpio_mode) {
    PIN_ARG(0, pin); INT_ARG(1, mode);
    NEED(gpio_mode, "GPIO.Mode");
    RC("GPIO.Mode", HAL()->gpio_mode(CTX(), pin, mode));
    return mcs_null();
}
NATIVE(gpio_write) {
    PIN_ARG(0, pin);
    RC("GPIO.Write", HAL()->gpio_write(CTX(), pin, level_of(argv[1])));
    return mcs_null();
}
NATIVE(gpio_read) {
    PIN_ARG(0, pin);
    NEED(gpio_read, "GPIO.Read");
    int rc = HAL()->gpio_read(CTX(), pin);
    return rc < 0 ? hal_fail(vm, "GPIO.Read", rc) : mcs_bool(rc != 0);
}
NATIVE(gpio_toggle) {
    PIN_ARG(0, pin);
    NEED(gpio_read, "GPIO.Toggle");
    int rc = HAL()->gpio_read(CTX(), pin);
    if (rc >= 0) rc = HAL()->gpio_write(CTX(), pin, !rc);
    return rc < 0 ? hal_fail(vm, "GPIO.Toggle", rc) : mcs_null();
}
NATIVE(gpio_pin) {
    int p = pin_arg(vm, argv[0]);
    return mcs_has_exception(vm) ? mcs_null() : mcs_int(p);
}
static mcs_value_t gpio_irq_common(mcs_vm_t* vm, int pin, int edge, mcs_value_t fn) {
    if (!HAL()->gpio_irq) return hal_fail(vm, "GPIO.OnChange", MCS_HAL_ENOTSUP);
    if (edge < 0 || edge > 3) return range_fail(vm, "edge");
    if (mcs_is_null(fn)) edge = 0;
    if (!check_callable(vm, fn)) return mcs_null();
    set_callback(vm, CB_GPIO, pin, edge ? fn : mcs_null());
    if (mcs_has_exception(vm)) return mcs_null();
    RC("GPIO.OnChange", HAL()->gpio_irq(CTX(), pin, edge));
    return mcs_null();
}
/* GPIO.OnChange(pin, edge, (pin, level) => ...) */
NATIVE(gpio_onchange) {
    PIN_ARG(0, pin); INT_ARG(1, edge);
    return gpio_irq_common(vm, pin, edge, argv[2]);
}
NATIVE(gpio_off) {
    PIN_ARG(0, pin);
    return gpio_irq_common(vm, pin, 0, mcs_null());
}
/* pulse length in microseconds on `pin` at `level`, 0 on timeout (busy-wait, needs micros) */
NATIVE(gpio_pulsein) {
    PIN_ARG(0, pin);
    int level = level_of(argv[1]);
    OPT_INT(2, timeout_us, 1000000);
    const mcs_hal_t* h = HAL();
    if (!h->micros || !h->gpio_read) return hal_fail(vm, "GPIO.PulseIn", MCS_HAL_ENOTSUP);
    uint32_t t0 = h->micros(h->ctx), start;
    while (h->gpio_read(h->ctx, pin) == level) if (h->micros(h->ctx) - t0 > (uint32_t)timeout_us) return mcs_int(0);
    while (h->gpio_read(h->ctx, pin) != level) if (h->micros(h->ctx) - t0 > (uint32_t)timeout_us) return mcs_int(0);
    start = h->micros(h->ctx);
    while (h->gpio_read(h->ctx, pin) == level) if (h->micros(h->ctx) - t0 > (uint32_t)timeout_us) return mcs_int(0);
    return mcs_int((mcs_int_t)(h->micros(h->ctx) - start));
}
/* fields a v1/v2 board can express through the legacy calls */
static mcs_value_t cfg_unsupported(mcs_vm_t* vm, const char* what, const char* field) {
    mcs_raise(vm, "NotSupportedException", "%s: %s is not supported by this board", what, field);
    return mcs_null();
}
static mcs_value_t gpio_apply(mcs_vm_t* vm, int pin, const mcs_gpio_config_t* c, const char* what) {
    const mcs_hal_t* h = HAL();
    if (h->gpio_configure) { RC(what, h->gpio_configure(h->ctx, pin, c)); return mcs_null(); }
    static const char* const ok[] = { "Mode", "Pull", "Initial", NULL };
    const char* nd = mcs__cfg_nondefault(c, MCS__CFG_GPIO, ok);
    if (nd) return cfg_unsupported(vm, what, nd);
    int mode = c->mode;
    if (c->pull == MCS_PULL_UP || c->pull == MCS_PULL_DOWN) {
        if (mode == MCS_GPIO_INPUT || mode == MCS_GPIO_INPUT_PULLUP || mode == MCS_GPIO_INPUT_PULLDOWN)
            mode = c->pull == MCS_PULL_UP ? MCS_GPIO_INPUT_PULLUP : MCS_GPIO_INPUT_PULLDOWN;
        else return cfg_unsupported(vm, what, "Pull with this mode");
    } else if (c->pull == MCS_PULL_BOTH) return cfg_unsupported(vm, what, "Pull = Both");
    else if (c->pull == MCS_PULL_NONE && (mode == MCS_GPIO_INPUT_PULLUP || mode == MCS_GPIO_INPUT_PULLDOWN)) mode = MCS_GPIO_INPUT;
    if (!h->gpio_mode) return hal_fail(vm, what, MCS_HAL_ENOTSUP);
    bool out = mode == MCS_GPIO_OUTPUT || mode == MCS_GPIO_OPEN_DRAIN;
    if (out && c->initial >= 0) h->gpio_write(h->ctx, pin, c->initial);   /* before the driver turns on, where the port allows */
    RC(what, h->gpio_mode(h->ctx, pin, mode));
    if (out && c->initial >= 0) RC(what, h->gpio_write(h->ctx, pin, c->initial));
    return mcs_null();
}
static const mcs_gpio_config_t* gpio_cfg_arg(mcs_vm_t* vm, mcs_value_t v) {
    const mcs_gpio_config_t* c = (const mcs_gpio_config_t*)mcs__cfg_get(v, MCS__CFG_GPIO);
    if (!c) mcs_raise(vm, "ArgumentException", "GpioConfig expected");
    return c;
}
/* GPIO.Configure(pin, new GpioConfig { Mode = GPIO.Output, DriveMilliamps = 12 }) */
NATIVE(gpio_configure) {
    PIN_ARG(0, pin);
    const mcs_gpio_config_t* c = gpio_cfg_arg(vm, argv[1]);
    return c ? gpio_apply(vm, pin, c, "GPIO.Configure") : mcs_null();
}
static const mcs_reg_t gpio_fns[] = {
    MCS_FN("Configure", gpio_configure, 2),
    MCS_FN("Mode", gpio_mode, 2), MCS_FN("Write", gpio_write, 2), MCS_FN("Read", gpio_read, 1),
    MCS_FN("Toggle", gpio_toggle, 1), MCS_FN("Pin", gpio_pin, 1), MCS_FN("OnChange", gpio_onchange, 3),
    MCS_FN("Off", gpio_off, 1), MCS_FN("PulseIn", gpio_pulsein, -1), MCS_REG_END
};

/* ------------------------------------------------------------- Pin (object) */
typedef struct { int pin; int mode; } pin_t;
static const mcs_class_def_t pin_def;
static void pin_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    pin_t* p = (pin_t*)mcs_userdata(self);
    if (argc < 1 || argc > 2) { mcs_raise(vm, "ArgumentException", "Pin(pin[, mode | GpioConfig]) expected"); return; }
    p->pin = pin_arg(vm, argv[0]);
    if (mcs_has_exception(vm)) return;
    const mcs_gpio_config_t* gc = argc > 1 ? (const mcs_gpio_config_t*)mcs__cfg_get(argv[1], MCS__CFG_GPIO) : NULL;
    if (gc) { p->mode = gc->mode; gpio_apply(vm, p->pin, gc, "Pin"); return; }
    p->mode = argc > 1 ? (int)mcs_to_int(vm, argv[1]) : -1;
    if (mcs_has_exception(vm) || p->mode < 0) return;
    if (!HAL()->gpio_mode) { hal_fail(vm, "Pin", MCS_HAL_ENOTSUP); return; }
    int rc = HAL()->gpio_mode(CTX(), p->pin, p->mode);
    if (rc < 0) hal_fail(vm, "Pin", rc);
}
#define PIN() pin_t* p = (pin_t*)mcs_check_userdata(vm, self, &pin_def); if (!p) return mcs_null()
NATIVE(pin_write) {
    PIN();
    RC("Pin.Write", HAL()->gpio_write(CTX(), p->pin, level_of(argv[0])));
    return mcs_null();
}
NATIVE(pin_high) { PIN(); RC("Pin.High", HAL()->gpio_write(CTX(), p->pin, 1)); return mcs_null(); }
NATIVE(pin_low) { PIN(); RC("Pin.Low", HAL()->gpio_write(CTX(), p->pin, 0)); return mcs_null(); }
NATIVE(pin_read) {
    PIN(); NEED(gpio_read, "Pin.Read");
    int rc = HAL()->gpio_read(CTX(), p->pin);
    return rc < 0 ? hal_fail(vm, "Pin.Read", rc) : mcs_bool(rc != 0);
}
NATIVE(pin_toggle) {
    PIN(); NEED(gpio_read, "Pin.Toggle");
    int rc = HAL()->gpio_read(CTX(), p->pin);
    if (rc >= 0) rc = HAL()->gpio_write(CTX(), p->pin, !rc);
    return rc < 0 ? hal_fail(vm, "Pin.Toggle", rc) : mcs_null();
}
NATIVE(pin_setmode) {
    PIN(); INT_ARG(0, mode); NEED(gpio_mode, "Pin.Mode");
    RC("Pin.Mode", HAL()->gpio_mode(CTX(), p->pin, mode));
    p->mode = mode;
    return mcs_null();
}
NATIVE(pin_number) { PIN(); return mcs_int(p->pin); }
NATIVE(pin_configure) {
    PIN();
    const mcs_gpio_config_t* c = gpio_cfg_arg(vm, argv[0]);
    if (!c) return mcs_null();
    gpio_apply(vm, p->pin, c, "Pin.Configure");
    if (!mcs_has_exception(vm)) p->mode = c->mode;
    return mcs_null();
}
NATIVE(pin_onchange) { PIN(); INT_ARG(0, edge); return gpio_irq_common(vm, p->pin, edge, argv[1]); }
static const mcs_reg_t pin_members[] = {
    MCS_FN("Write", pin_write, 1), MCS_FN("Read", pin_read, 0), MCS_FN("Toggle", pin_toggle, 0),
    MCS_FN("High", pin_high, 0), MCS_FN("Low", pin_low, 0), MCS_FN("SetMode", pin_setmode, 1),
    MCS_FN("OnChange", pin_onchange, 2), MCS_FN("Configure", pin_configure, 1),
    MCS_GET("Value", pin_read), MCS_SET("Value", pin_write), MCS_GET("Number", pin_number), MCS_REG_END
};
static const mcs_class_def_t pin_def = { "Pin", sizeof(pin_t), pin_ctor, NULL, pin_members, NULL };

/* ------------------------------------------------------------- UART */
static mcs_value_t uart_open_cfg(mcs_vm_t* vm, int port, const mcs_uart_config_t* c) {
    const mcs_hal_t* h = HAL();
    if (h->uart_configure) { RC("UART.Open", h->uart_configure(h->ctx, port, c)); return mcs_null(); }
    static const char* const ok[] = { "Baud", "DataBits", "Parity", "StopBits", NULL };
    const char* nd = mcs__cfg_nondefault(c, MCS__CFG_UART, ok);
    if (nd) return cfg_unsupported(vm, "UART.Open", nd);
    if (h->uart_config) RC("UART.Open", h->uart_config(h->ctx, port, &c->frame));
    else if (h->uart_open) {
        if (c->frame.data_bits != 8 || c->frame.parity || c->frame.stop_bits != 1) return hal_fail(vm, "UART.Open", MCS_HAL_ENOTSUP);
        RC("UART.Open", h->uart_open(h->ctx, port, c->frame.baud));
    }
    return mcs_null();
}
NATIVE(uart_open) {
    if (argc == 2) {
        const mcs_uart_config_t* c = (const mcs_uart_config_t*)mcs__cfg_get(argv[1], MCS__CFG_UART);
        if (c) { INT_ARG(0, p); return uart_open_cfg(vm, p, c); }
    }
    INT_ARG(0, port); INT_ARG(1, baud);
    OPT_INT(2, bits, 8); OPT_INT(3, parity, MCS_UART_PARITY_NONE); OPT_INT(4, stop, 1);
    if (baud <= 0) return range_fail(vm, "baud rate");
    if (bits < 5 || bits > 9 || parity < 0 || parity > 2 || stop < 1 || stop > 2) return range_fail(vm, "frame format");
    const mcs_hal_t* h = HAL();
    if (h->uart_config) {
        mcs_uart_cfg_t c = { (uint32_t)baud, (uint8_t)bits, (uint8_t)parity, (uint8_t)stop, 0 };
        RC("UART.Open", h->uart_config(h->ctx, port, &c));
    } else if (h->uart_open) {
        if (argc > 2 && (bits != 8 || parity || stop != 1)) return hal_fail(vm, "UART.Open", MCS_HAL_ENOTSUP);
        RC("UART.Open", h->uart_open(h->ctx, port, (uint32_t)baud));
    }
    return mcs_null();
}
NATIVE(uart_close) {
    INT_ARG(0, port);
    if (HAL()->uart_close) RC("UART.Close", HAL()->uart_close(CTX(), port));
    return mcs_null();
}
static mcs_value_t uart_write_common(mcs_vm_t* vm, int port, mcs_value_t data, bool nl) {
    uint8_t buf[MCS_HAL_MAX_XFER + 2]; size_t n;
    uint8_t* db;
    if (!nl && mcs__dmabuf(data, &db, &n)) {      /* DmaBuffer: in place, any length */
        if (!mcs__dma_idle(vm, MCS__DMA_UART_TX, port)) return mcs_null();
        int rc = HAL()->uart_write(CTX(), port, db, n);
        return rc < 0 ? hal_fail(vm, "UART.Write", rc) : mcs_int(rc);
    }
    if (!to_bytes(vm, data, buf, &n)) return mcs_null();
    if (!mcs__dma_idle(vm, MCS__DMA_UART_TX, port)) return mcs_null();
    if (nl) { buf[n++] = '\r'; buf[n++] = '\n'; }
    int rc = HAL()->uart_write(CTX(), port, buf, n);
    return rc < 0 ? hal_fail(vm, "UART.Write", rc) : mcs_int(rc);
}
NATIVE(uart_write) { INT_ARG(0, port); return uart_write_common(vm, port, argv[1], false); }
NATIVE(uart_writeline) { INT_ARG(0, port); return uart_write_common(vm, port, argc > 1 ? argv[1] : mcs_null(), true); }
static int uart_read_common(mcs_vm_t* vm, int argc, mcs_value_t* argv, uint8_t* buf) {
    int port = (int)mcs_to_int(vm, argv[0]);
    int n = count_arg(vm, argv[1]);
    uint32_t timeout = argc > 2 ? (uint32_t)mcs_to_int(vm, argv[2]) : 0;
    if (mcs_has_exception(vm) || n < 0) return -100;
    if (!HAL()->uart_read) { hal_fail(vm, "UART.Read", MCS_HAL_ENOTSUP); return -100; }
    int rc = HAL()->uart_read(CTX(), port, buf, (size_t)n, timeout);
    if (rc < 0) { hal_fail(vm, "UART.Read", rc); return -100; }
    return rc;
}
NATIVE(uart_read) {
    uint8_t buf[MCS_HAL_MAX_XFER];
    uint8_t* db; size_t dl;
    if (argc >= 2 && mcs__dmabuf(argv[1], &db, &dl)) {   /* Read(port, DmaBuffer[, timeoutMs]) -> bytes read */
        INT_ARG(0, port); OPT_INT(2, timeout, 0);
        NEED(uart_read, "UART.Read");
        if (!mcs__dma_idle(vm, MCS__DMA_UART_RX, port)) return mcs_null();
        int rc = HAL()->uart_read(CTX(), port, db, dl, (uint32_t)timeout);
        return rc < 0 ? hal_fail(vm, "UART.Read", rc) : mcs_int(rc);
    }
    int rc = uart_read_common(vm, argc, argv, buf);
    return rc < 0 ? mcs_null() : bytes_val(vm, buf, (size_t)rc);
}
NATIVE(uart_read_string) {
    uint8_t buf[MCS_HAL_MAX_XFER];
    int rc = uart_read_common(vm, argc, argv, buf);
    return rc < 0 ? mcs_null() : mcs_string_n(vm, (const char*)buf, (size_t)rc);
}
/* UART.ReadLine(port, timeoutMs): text up to '\n' ('\r' dropped), null on timeout with no data */
NATIVE(uart_readline) {
    INT_ARG(0, port); OPT_INT(1, timeout, 1000);
    NEED(uart_read, "UART.ReadLine");
    char buf[MCS_HAL_MAX_XFER];
    size_t n = 0;
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        uint8_t c;
        int rc = HAL()->uart_read(CTX(), port, &c, 1, 10);
        if (rc == 1) {
            if (c == '\n') break;
            if (c != '\r' && n < sizeof buf) buf[n++] = (char)c;
            continue;
        }
        if (rc < 0 && rc != MCS_HAL_ETIMEOUT) return hal_fail(vm, "UART.ReadLine", rc);
        if ((uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) { if (!n) return mcs_null(); break; }
        if (mcs_safepoint(vm)) return mcs_null();
    }
    return mcs_string_n(vm, buf, n);
}
NATIVE(uart_available) {
    INT_ARG(0, port);
    if (!HAL()->uart_available) return mcs_int(0);
    int rc = HAL()->uart_available(CTX(), port);
    return rc < 0 ? hal_fail(vm, "UART.Available", rc) : mcs_int(rc);
}
NATIVE(uart_onreceive) { INT_ARG(0, port); return set_callback(vm, CB_UART, port, argv[1]); }
static const mcs_reg_t uart_fns[] = {
    MCS_FN("Open", uart_open, -1), MCS_FN("Close", uart_close, 1), MCS_FN("Write", uart_write, 2),
    MCS_FN("WriteLine", uart_writeline, -1), MCS_FN("Read", uart_read, -1), MCS_FN("ReadString", uart_read_string, -1),
    MCS_FN("ReadLine", uart_readline, -1), MCS_FN("Available", uart_available, 1),
    MCS_FN("OnReceive", uart_onreceive, 2), MCS_REG_END
};

/* ------------------------------------------------------------- I2C */
static int i2c_wr(const mcs_hal_t* h, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    if (h->i2c_write_read) return h->i2c_write_read(h->ctx, bus, addr, tx, tn, rx, rn);
    if (!h->i2c_read) return MCS_HAL_ENOTSUP;
    int rc = h->i2c_write(h->ctx, bus, addr, tx, tn);
    return rc < 0 ? rc : h->i2c_read(h->ctx, bus, addr, rx, rn);
}
static mcs_value_t i2c_write_impl(mcs_vm_t* vm, int bus, int addr, mcs_value_t data) {
    uint8_t buf[MCS_HAL_MAX_XFER]; size_t n; const uint8_t* p;
    if (addr < 0 || addr > 0x3FF) return range_fail(vm, "I2C address");
    if (!bytes_ref(vm, data, buf, &p, &n)) return mcs_null();
    if (!mcs__dma_idle(vm, MCS__DMA_I2C, bus)) return mcs_null();
    RC("I2C.Write", HAL()->i2c_write(CTX(), bus, addr, p, n));
    return mcs_null();
}
static mcs_value_t i2c_read_impl(mcs_vm_t* vm, int bus, int addr, mcs_value_t count) {
    uint8_t* db; size_t dl;
    if (!mcs__dma_idle(vm, MCS__DMA_I2C, bus)) return mcs_null();
    if (mcs__dmabuf(count, &db, &dl)) {            /* read into a DmaBuffer, any length */
        NEED(i2c_read, "I2C.Read");
        RC("I2C.Read", HAL()->i2c_read(CTX(), bus, addr, db, dl));
        return count;
    }
    int n = count_arg(vm, count);
    if (n < 0) return mcs_null();
    NEED(i2c_read, "I2C.Read");
    uint8_t buf[MCS_HAL_MAX_XFER];
    RC("I2C.Read", HAL()->i2c_read(CTX(), bus, addr, buf, (size_t)n));
    return bytes_val(vm, buf, (size_t)n);
}
static mcs_value_t i2c_wr_impl(mcs_vm_t* vm, int bus, int addr, mcs_value_t data, mcs_value_t count) {
    uint8_t tx[MCS_HAL_MAX_XFER], rx[MCS_HAL_MAX_XFER]; size_t tn;
    const uint8_t* tp; uint8_t* db; size_t dl;
    if (!bytes_ref(vm, data, tx, &tp, &tn)) return mcs_null();
    if (!mcs__dma_idle(vm, MCS__DMA_I2C, bus)) return mcs_null();
    if (mcs__dmabuf(count, &db, &dl)) {
        RC("I2C.WriteRead", i2c_wr(HAL(), bus, addr, tp, tn, db, dl));
        return count;
    }
    if (tp != tx) { if (tn > MCS_HAL_MAX_XFER) return range_fail(vm, "I2C.WriteRead size"); memcpy(tx, tp, tn); }
    int n = count_arg(vm, count);
    if (n < 0) return mcs_null();
    RC("I2C.WriteRead", i2c_wr(HAL(), bus, addr, tx, tn, rx, (size_t)n));
    return bytes_val(vm, rx, (size_t)n);
}
static mcs_value_t i2c_readreg_impl(mcs_vm_t* vm, int bus, int addr, int reg, int n, bool as_int) {
    if (reg < 0 || reg > 0xFF) return range_fail(vm, "register");
    if (n < 0 || n > MCS_HAL_MAX_XFER) return range_fail(vm, "count");
    uint8_t r = (uint8_t)reg, rx[MCS_HAL_MAX_XFER];
    if (!mcs__dma_idle(vm, MCS__DMA_I2C, bus)) return mcs_null();
    RC("I2C.ReadRegister", i2c_wr(HAL(), bus, addr, &r, 1, rx, (size_t)n));
    return as_int ? mcs_int(rx[0]) : bytes_val(vm, rx, (size_t)n);
}
static mcs_value_t i2c_writereg_impl(mcs_vm_t* vm, int bus, int addr, int reg, mcs_value_t val) {
    if (reg < 0 || reg > 0xFF) return range_fail(vm, "register");
    uint8_t buf[MCS_HAL_MAX_XFER + 1]; size_t n;
    if (mcs_is_number(val)) { buf[1] = (uint8_t)mcs_to_int(vm, val); n = 1; }
    else if (!to_bytes(vm, val, buf + 1, &n)) return mcs_null();
    buf[0] = (uint8_t)reg;
    if (!mcs__dma_idle(vm, MCS__DMA_I2C, bus)) return mcs_null();
    RC("I2C.WriteRegister", HAL()->i2c_write(CTX(), bus, addr, buf, n + 1));
    return mcs_null();
}
NATIVE(i2c_open) {
    const mcs_i2c_config_t* c = argc == 2 ? (const mcs_i2c_config_t*)mcs__cfg_get(argv[1], MCS__CFG_I2C) : NULL;
    if (c) {
        INT_ARG(0, b);
        const mcs_hal_t* h = HAL();
        if (h->i2c_configure) { RC("I2C.Open", h->i2c_configure(h->ctx, b, c)); return mcs_null(); }
        static const char* const ok[] = { "Frequency", NULL };
        const char* nd = mcs__cfg_nondefault(c, MCS__CFG_I2C, ok);
        if (nd) return cfg_unsupported(vm, "I2C.Open", nd);
        if (h->i2c_open) RC("I2C.Open", h->i2c_open(h->ctx, b, c->freq_hz ? c->freq_hz : 100000));
        return mcs_null();
    }
    INT_ARG(0, bus); OPT_INT(1, freq, 100000);
    if (freq <= 0) return range_fail(vm, "frequency");
    if (HAL()->i2c_open) RC("I2C.Open", HAL()->i2c_open(CTX(), bus, (uint32_t)freq));
    return mcs_null();
}
NATIVE(i2c_write) { INT_ARG(0, bus); INT_ARG(1, addr); return i2c_write_impl(vm, bus, addr, argv[2]); }
NATIVE(i2c_read) { INT_ARG(0, bus); INT_ARG(1, addr); return i2c_read_impl(vm, bus, addr, argv[2]); }
NATIVE(i2c_write_read) { INT_ARG(0, bus); INT_ARG(1, addr); return i2c_wr_impl(vm, bus, addr, argv[2], argv[3]); }
NATIVE(i2c_readreg) { INT_ARG(0, bus); INT_ARG(1, addr); INT_ARG(2, reg); return i2c_readreg_impl(vm, bus, addr, reg, 1, true); }
NATIVE(i2c_readregs) { INT_ARG(0, bus); INT_ARG(1, addr); INT_ARG(2, reg); INT_ARG(3, n); return i2c_readreg_impl(vm, bus, addr, reg, n, false); }
NATIVE(i2c_writereg) { INT_ARG(0, bus); INT_ARG(1, addr); INT_ARG(2, reg); return i2c_writereg_impl(vm, bus, addr, reg, argv[3]); }
NATIVE(i2c_scan) {
    INT_ARG(0, bus);
    const mcs_hal_t* h = HAL();
    mcs_value_t list = mcs_new_list(vm);
    mcs_push_root(vm, list);
    for (int a = 0x08; a < 0x78; a++) {
        uint8_t b;
        int rc = h->i2c_probe ? h->i2c_probe(h->ctx, bus, a) : h->i2c_read ? h->i2c_read(h->ctx, bus, a, &b, 1) : MCS_HAL_ENOTSUP;
        if (rc == MCS_HAL_ENOTSUP) { mcs_pop_root(vm, 1); return hal_fail(vm, "I2C.Scan", rc); }
        if (rc >= 0) mcs_list_add(vm, list, mcs_int(a));
    }
    mcs_pop_root(vm, 1);
    return list;
}
static const mcs_reg_t i2c_fns[] = {
    MCS_FN("Open", i2c_open, -1), MCS_FN("Write", i2c_write, 3), MCS_FN("Read", i2c_read, 3),
    MCS_FN("WriteRead", i2c_write_read, 4), MCS_FN("ReadRegister", i2c_readreg, 3),
    MCS_FN("ReadRegisters", i2c_readregs, 4), MCS_FN("WriteRegister", i2c_writereg, 4),
    MCS_FN("Scan", i2c_scan, 1), MCS_REG_END
};

/* I2cDevice(bus, address) */
typedef struct { int bus, addr; } i2cdev_t;
static const mcs_class_def_t i2cdev_def;
static void i2cdev_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    i2cdev_t* d = (i2cdev_t*)mcs_userdata(self);
    if (argc != 2) { mcs_raise(vm, "ArgumentException", "I2cDevice(bus, address) expected"); return; }
    d->bus = (int)mcs_to_int(vm, argv[0]);
    d->addr = (int)mcs_to_int(vm, argv[1]);
    if (!mcs_has_exception(vm) && (d->addr < 0 || d->addr > 0x3FF)) range_fail(vm, "I2C address");
}
#define DEV() i2cdev_t* d = (i2cdev_t*)mcs_check_userdata(vm, self, &i2cdev_def); if (!d) return mcs_null()
NATIVE(dev_write) { DEV(); return i2c_write_impl(vm, d->bus, d->addr, argv[0]); }
NATIVE(dev_read) { DEV(); return i2c_read_impl(vm, d->bus, d->addr, argv[0]); }
NATIVE(dev_write_read) { DEV(); return i2c_wr_impl(vm, d->bus, d->addr, argv[0], argv[1]); }
NATIVE(dev_readreg) { DEV(); INT_ARG(0, reg); return i2c_readreg_impl(vm, d->bus, d->addr, reg, 1, true); }
NATIVE(dev_readregs) { DEV(); INT_ARG(0, reg); INT_ARG(1, n); return i2c_readreg_impl(vm, d->bus, d->addr, reg, n, false); }
NATIVE(dev_writereg) { DEV(); INT_ARG(0, reg); return i2c_writereg_impl(vm, d->bus, d->addr, reg, argv[1]); }
NATIVE(dev_write_async) { DEV(); return mcs__i2c_async(vm, d->bus, d->addr, argv[0], mcs_null()); }
NATIVE(dev_read_async) { DEV(); return mcs__i2c_async(vm, d->bus, d->addr, mcs_null(), argv[0]); }
NATIVE(dev_write_read_async) { DEV(); return mcs__i2c_async(vm, d->bus, d->addr, argv[0], argv[1]); }
NATIVE(dev_addr) { DEV(); return mcs_int(d->addr); }
NATIVE(dev_bus) { DEV(); return mcs_int(d->bus); }
static const mcs_reg_t i2cdev_members[] = {
    MCS_FN("Write", dev_write, 1), MCS_FN("Read", dev_read, 1), MCS_FN("WriteRead", dev_write_read, 2),
    MCS_FN("ReadRegister", dev_readreg, 1), MCS_FN("ReadRegisters", dev_readregs, 2),
    MCS_FN("WriteRegister", dev_writereg, 2),
    MCS_FN("WriteAsync", dev_write_async, 1), MCS_FN("ReadAsync", dev_read_async, 1), MCS_FN("WriteReadAsync", dev_write_read_async, 2),
    MCS_GET("Address", dev_addr), MCS_GET("Bus", dev_bus), MCS_REG_END
};
static const mcs_class_def_t i2cdev_def = { "I2cDevice", sizeof(i2cdev_t), i2cdev_ctor, NULL, i2cdev_members, NULL };

/* ------------------------------------------------------------- SPI */
static int spi_apply(mcs_vm_t* vm, int bus, const mcs_spi_cfg_t* c) {
    hal_state_t* s = ST();
    const mcs_hal_t* h = s->hal;
    if (!h->spi_open) return 0;
    if (bus >= 0 && bus < MCS_HAL_MAX_BUSES && s->spi_valid[bus] && !memcmp(&s->spi_cur[bus], c, sizeof *c)) return 0;
    int rc = h->spi_open(h->ctx, bus, c);
    if (rc >= 0 && bus >= 0 && bus < MCS_HAL_MAX_BUSES) { s->spi_cur[bus] = *c; s->spi_valid[bus] = 1; }
    return rc;
}
int mcs_hal_spi_config(mcs_vm_t* vm, int bus, const mcs_spi_cfg_t* cfg) {
    if (!vm || !cfg || !ST() || !HAL()) return MCS_HAL_ENOTSUP;
    return spi_apply(vm, bus, cfg);
}
/* chip select: explicit pin (active low), else the bus default from SpiConfig.Cs */
typedef struct { int pin; int active; } spi_cs_t;
static spi_cs_t spi_cs_of(mcs_vm_t* vm, int bus, int cs, int cs_high) {
    spi_cs_t r = { cs, cs_high ? 1 : 0 };
    hal_state_t* s = ST();
    if (cs < 0 && bus >= 0 && bus < MCS_HAL_MAX_BUSES && s->spi_cs[bus] >= 0) { r.pin = s->spi_cs[bus]; r.active = s->spi_cs_high[bus]; }
    return r;
}
/* spi_transfer never sees NULL buffers (the v1/v2 contract): one-way transfers of
 * any size go through the stack in MCS_HAL_MAX_XFER pieces, chip select held */
uint32_t mcs__spi_half;     /* buses configured with SpiConfig.ThreeWire (bit per bus) */
int mcs__spi_raw(const mcs_hal_t* h, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (bus >= 0 && bus < 32 && (mcs__spi_half >> bus & 1u)) {
        /* half duplex: the port gets exactly one of tx / rx, any size */
        if (tx && rx) { mcs_hal_error("ThreeWire: half duplex - use SPI.Write / SPI.Read"); return MCS_HAL_ENOTSUP; }
        return h->spi_transfer(h->ctx, bus, tx, rx, n);
    }
    if (tx && rx) return h->spi_transfer(h->ctx, bus, tx, rx, n);
    uint8_t t[MCS_HAL_MAX_XFER], r[MCS_HAL_MAX_XFER];
    if (!tx) memset(t, 0xFF, sizeof t);
    for (size_t off = 0; off < n;) {
        size_t k = n - off < sizeof t ? n - off : sizeof t;
        int rc = h->spi_transfer(h->ctx, bus, tx ? tx + off : t, rx ? rx + off : r, k);
        if (rc < 0) return rc;
        off += k;
    }
    return 0;
}
static int spi_run(const mcs_hal_t* h, int bus, spi_cs_t cs, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (cs.pin >= 0) { int rc = h->gpio_write ? h->gpio_write(h->ctx, cs.pin, cs.active) : MCS_HAL_ENOTSUP; if (rc < 0) return rc; }
    int rc = mcs__spi_raw(h, bus, tx, rx, n);
    if (cs.pin >= 0) h->gpio_write(h->ctx, cs.pin, !cs.active);
    return rc;
}
/* mode: 0 transfer, 1 write only, 2 read only (count or DmaBuffer in `data`).
 * DmaBuffers are used in place and may be larger than MCS_HAL_MAX_XFER. */
static mcs_value_t spi_xfer(mcs_vm_t* vm, int bus, mcs_value_t data, spi_cs_t cs, int mode) {
    uint8_t tx[MCS_HAL_MAX_XFER], rx[MCS_HAL_MAX_XFER]; size_t n;
    const uint8_t* tp = tx; uint8_t* rp = rx;
    uint8_t* db;
    bool into = false;
    if (mode == 2 && mcs__dmabuf(data, &db, &n)) {
        into = true; rp = db;
        if (n <= MCS_HAL_MAX_XFER) memset(tx, 0xFF, n); else tp = NULL;
    } else if (mode == 2) {
        int c = count_arg(vm, data);
        if (c < 0) return mcs_null();
        n = (size_t)c; memset(tx, 0xFF, n);
    } else {
        if (!bytes_ref(vm, data, tx, &tp, &n)) return mcs_null();
        if (n > MCS_HAL_MAX_XFER) { if (mode == 0) return range_fail(vm, "SPI.Transfer size (use Transfer(bus, tx, rx) with DmaBuffers)"); rp = NULL; }
    }
    if (!mcs__dma_idle(vm, MCS__DMA_SPI, bus)) return mcs_null();
    int rc = spi_run(HAL(), bus, cs, tp, rp, n);
    if (rc < 0) return hal_fail(vm, mode == 1 ? "SPI.Write" : mode == 2 ? "SPI.Read" : "SPI.Transfer", rc);
    if (into) return data;
    return mode == 1 ? mcs_null() : bytes_val(vm, rx, n);
}
/* full duplex between two DmaBuffers of the same length */
static mcs_value_t spi_xfer2(mcs_vm_t* vm, int bus, mcs_value_t txv, mcs_value_t rxv, spi_cs_t cs) {
    uint8_t stack[MCS_HAL_MAX_XFER]; const uint8_t* tp; size_t tn, rn; uint8_t* rp;
    if (!mcs__dmabuf(rxv, &rp, &rn)) { mcs_raise(vm, "ArgumentException", "rx must be a DmaBuffer"); return mcs_null(); }
    if (!bytes_ref(vm, txv, stack, &tp, &tn)) return mcs_null();
    if (tn != rn) { mcs_raise(vm, "ArgumentException", "tx and rx lengths differ (%d / %d)", (int)tn, (int)rn); return mcs_null(); }
    if (!mcs__dma_idle(vm, MCS__DMA_SPI, bus)) return mcs_null();
    RC("SPI.Transfer", spi_run(HAL(), bus, cs, tp, rp, tn));
    return rxv;
}
static mcs_value_t spi_open_cfg(mcs_vm_t* vm, int bus, const mcs_spi_config_t* c) {
    hal_state_t* s = ST();
    const mcs_hal_t* h = s->hal;
    if (h->spi_configure) {
        RC("SPI.Open", h->spi_configure(h->ctx, bus, c));
        if (bus >= 0 && bus < 32) {
            if (c->three_wire) mcs__spi_half |= 1u << bus; else mcs__spi_half &= ~(1u << bus);
        }
        if (bus >= 0 && bus < MCS_HAL_MAX_BUSES) { s->spi_cur[bus] = c->bus; s->spi_valid[bus] = 1; }
    } else {
        static const char* const ok[] = { "Frequency", "Mode", "Bits", "LsbFirst", "Cs", "CsActiveHigh", NULL };
        const char* nd = mcs__cfg_nondefault(c, MCS__CFG_SPI, ok);
        if (nd) return cfg_unsupported(vm, "SPI.Open", nd);
        if (bus >= 0 && bus < MCS_HAL_MAX_BUSES) s->spi_valid[bus] = 0;   /* apply even when cached */
        if (bus >= 0 && bus < 32) mcs__spi_half &= ~(1u << bus);
        RC("SPI.Open", spi_apply(vm, bus, &c->bus));
    }
    if (bus >= 0 && bus < MCS_HAL_MAX_BUSES) {
        s->spi_cs[bus] = c->cs; s->spi_cs_high[bus] = c->cs_active_high ? 1 : 0;
        if (c->cs >= 0 && h->gpio_mode) { h->gpio_mode(h->ctx, c->cs, MCS_GPIO_OUTPUT); h->gpio_write(h->ctx, c->cs, !s->spi_cs_high[bus]); }
    }
    return mcs_null();
}
NATIVE(spi_open) {
    if (argc == 2) {
        const mcs_spi_config_t* c = (const mcs_spi_config_t*)mcs__cfg_get(argv[1], MCS__CFG_SPI);
        if (c) { INT_ARG(0, b); return spi_open_cfg(vm, b, c); }
    }
    INT_ARG(0, bus); OPT_INT(1, freq, 1000000); OPT_INT(2, mode, 0); OPT_INT(3, lsb, 0);
    if (freq <= 0) return range_fail(vm, "frequency");
    if (mode < 0 || mode > 3) return range_fail(vm, "SPI mode");
    mcs_spi_cfg_t c = { (uint32_t)freq, (uint8_t)mode, 8, (uint8_t)(lsb != 0), 0 };
    RC("SPI.Open", spi_apply(vm, bus, &c));
    return mcs_null();
}
static int opt_pin(mcs_vm_t* vm, int argc, mcs_value_t* argv, int i) {
    return argc > i && !mcs_is_null(argv[i]) ? pin_arg(vm, argv[i]) : -1;
}
/* Transfer(bus, data[, cs]) or Transfer(bus, txBuffer, rxBuffer[, cs]) */
NATIVE(spi_transfer) {
    if (argc < 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "SPI.Transfer(bus, data[, cs]) or (bus, tx, rx[, cs]) expected"); return mcs_null(); }
    INT_ARG(0, bus);
    uint8_t* d; size_t l;
    if (argc > 2 && mcs__dmabuf(argv[2], &d, &l)) {
        int cs = opt_pin(vm, argc, argv, 3);
        if (mcs_has_exception(vm)) return mcs_null();
        return spi_xfer2(vm, bus, argv[1], argv[2], spi_cs_of(vm, bus, cs, 0));
    }
    int cs = opt_pin(vm, argc, argv, 2);
    if (mcs_has_exception(vm)) return mcs_null();
    return spi_xfer(vm, bus, argv[1], spi_cs_of(vm, bus, cs, 0), 0);
}
NATIVE(spi_write) { INT_ARG(0, bus); int cs = opt_pin(vm, argc, argv, 2); if (mcs_has_exception(vm)) return mcs_null(); return spi_xfer(vm, bus, argv[1], spi_cs_of(vm, bus, cs, 0), 1); }
NATIVE(spi_read) { INT_ARG(0, bus); int cs = opt_pin(vm, argc, argv, 2); if (mcs_has_exception(vm)) return mcs_null(); return spi_xfer(vm, bus, argv[1], spi_cs_of(vm, bus, cs, 0), 2); }
static const mcs_reg_t spi_fns[] = {
    MCS_FN("Open", spi_open, -1), MCS_FN("Transfer", spi_transfer, -1), MCS_FN("Write", spi_write, -1),
    MCS_FN("Read", spi_read, -1), MCS_REG_END
};

/* SpiDevice(bus, csPin[, freqHz[, mode]]) or SpiDevice(bus, csPin, SpiConfig) - chip
 * select is driven around every call; the bus is reconfigured only when another
 * device used different settings */
typedef struct { int bus, cs, cs_high; mcs_spi_cfg_t cfg; } spidev_t;
static const mcs_class_def_t spidev_def;
static void spidev_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    spidev_t* d = (spidev_t*)mcs_userdata(self);
    if (argc < 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "SpiDevice(bus, csPin[, freqHz[, mode]]) or SpiDevice(bus, csPin, SpiConfig) expected"); return; }
    d->bus = (int)mcs_to_int(vm, argv[0]);
    d->cs = mcs_is_null(argv[1]) ? -1 : pin_arg(vm, argv[1]);
    if (mcs_has_exception(vm)) return;
    const mcs_spi_config_t* c = argc == 3 ? (const mcs_spi_config_t*)mcs__cfg_get(argv[2], MCS__CFG_SPI) : NULL;
    const mcs_hal_t* h = HAL();
    if (c) {
        d->cfg = c->bus;
        d->cs_high = c->cs_active_high ? 1 : 0;
        if (d->cs < 0) d->cs = c->cs;
        static const char* const dev_only[] = { "Frequency", "Mode", "Bits", "LsbFirst", "Cs", "CsActiveHigh", NULL };
        if (mcs__cfg_nondefault(c, MCS__CFG_SPI, dev_only)) {      /* pins, DMA ...: configure the bus now */
            mcs__hal_state_t* s = ST();
            int saved_cs = d->bus >= 0 && d->bus < MCS_HAL_MAX_BUSES ? s->spi_cs[d->bus] : -1;
            uint8_t saved_hi = d->bus >= 0 && d->bus < MCS_HAL_MAX_BUSES ? s->spi_cs_high[d->bus] : 0;
            spi_open_cfg(vm, d->bus, c);
            if (d->bus >= 0 && d->bus < MCS_HAL_MAX_BUSES) { s->spi_cs[d->bus] = (int16_t)saved_cs; s->spi_cs_high[d->bus] = saved_hi; }
            if (mcs_has_exception(vm)) return;
        }
    } else {
        d->cfg.freq_hz = argc > 2 ? (uint32_t)mcs_to_int(vm, argv[2]) : 1000000;
        d->cfg.mode = (uint8_t)(argc > 3 ? mcs_to_int(vm, argv[3]) : 0);
        d->cfg.bits = 8;
        if (mcs_has_exception(vm)) return;
        if (d->cfg.mode > 3) { range_fail(vm, "SPI mode"); return; }
    }
    if (d->cs >= 0 && h->gpio_mode) { h->gpio_mode(h->ctx, d->cs, MCS_GPIO_OUTPUT); h->gpio_write(h->ctx, d->cs, !d->cs_high); }
}
#define SDEV() spidev_t* d = (spidev_t*)mcs_check_userdata(vm, self, &spidev_def); if (!d) return mcs_null(); \
    if (!mcs__dma_idle(vm, MCS__DMA_SPI, d->bus)) return mcs_null(); \
    RC("SpiDevice", spi_apply(vm, d->bus, &d->cfg)); spi_cs_t dcs = { d->cs, d->cs_high }
NATIVE(sdev_transfer) {
    SDEV();
    uint8_t* b; size_t l;
    if (argc == 2 && mcs__dmabuf(argv[1], &b, &l)) return spi_xfer2(vm, d->bus, argv[0], argv[1], dcs);
    if (argc != 1) { mcs_raise(vm, "ArgumentException", "Transfer(data) or Transfer(tx, rx) expected"); return mcs_null(); }
    return spi_xfer(vm, d->bus, argv[0], dcs, 0);
}
NATIVE(sdev_write) { SDEV(); return spi_xfer(vm, d->bus, argv[0], dcs, 1); }
NATIVE(sdev_read) { SDEV(); return spi_xfer(vm, d->bus, argv[0], dcs, 2); }
/* write a command, then read n bytes in the same chip-select window (flash, displays) */
NATIVE(sdev_write_read) {
    SDEV();
    uint8_t tx[MCS_HAL_MAX_XFER], rx[MCS_HAL_MAX_XFER]; size_t tn;
    if (!to_bytes(vm, argv[0], tx, &tn)) return mcs_null();
    int n = count_arg(vm, argv[1]);
    if (n < 0) return mcs_null();
    if (tn + (size_t)n > MCS_HAL_MAX_XFER) return range_fail(vm, "transfer");
    memset(tx + tn, 0xFF, (size_t)n);
    int rc = spi_run(HAL(), d->bus, dcs, tx, rx, tn + (size_t)n);
    return rc < 0 ? hal_fail(vm, "SpiDevice.WriteRead", rc) : bytes_val(vm, rx + tn, (size_t)n);
}
/* asynchronous (DMA) transfers: mcs_hal_dma.c */
#define SDEV_A() spidev_t* d = (spidev_t*)mcs_check_userdata(vm, self, &spidev_def); if (!d) return mcs_null(); \
    if (!mcs__dma_idle(vm, MCS__DMA_SPI, d->bus)) return mcs_null(); \
    RC("SpiDevice", spi_apply(vm, d->bus, &d->cfg))
NATIVE(sdev_transfer_async) { SDEV_A(); return mcs__spi_async(vm, d->bus, d->cs, d->cs_high, argv[0], argv[1]); }
NATIVE(sdev_write_async) { SDEV_A(); return mcs__spi_async(vm, d->bus, d->cs, d->cs_high, argv[0], mcs_null()); }
NATIVE(sdev_read_async) { SDEV_A(); return mcs__spi_async(vm, d->bus, d->cs, d->cs_high, mcs_null(), argv[0]); }
NATIVE(sdev_bus) { spidev_t* d = (spidev_t*)mcs_check_userdata(vm, self, &spidev_def); return d ? mcs_int(d->bus) : mcs_null(); }
NATIVE(sdev_cs) { spidev_t* d = (spidev_t*)mcs_check_userdata(vm, self, &spidev_def); return d ? mcs_int(d->cs) : mcs_null(); }
static const mcs_reg_t spidev_members[] = {
    MCS_FN("Transfer", sdev_transfer, -1), MCS_FN("Write", sdev_write, 1), MCS_FN("Read", sdev_read, 1),
    MCS_FN("WriteRead", sdev_write_read, 2),
    MCS_FN("TransferAsync", sdev_transfer_async, 2), MCS_FN("WriteAsync", sdev_write_async, 1), MCS_FN("ReadAsync", sdev_read_async, 1),
    MCS_GET("Bus", sdev_bus), MCS_GET("ChipSelect", sdev_cs), MCS_REG_END
};
static const mcs_class_def_t spidev_def = { "SpiDevice", sizeof(spidev_t), spidev_ctor, NULL, spidev_members, NULL };

/* ------------------------------------------------------------- ADC / DAC */
static int full_scale(int bits) { return bits >= 31 ? 0x7FFFFFFF : (1 << bits) - 1; }
static int vref(const mcs_hal_t* h) { return h->adc_vref_mv ? h->adc_vref_mv : 3300; }
NATIVE(adc_read) {
    INT_ARG(0, ch);
    int rc = HAL()->adc_read(CTX(), ch);
    return rc < 0 ? hal_fail(vm, "ADC.Read", rc) : mcs_int(rc);
}
static int adc_mv(const mcs_hal_t* h, int ch) {
    if (h->adc_read_mv) return h->adc_read_mv(h->ctx, ch);
    int raw = h->adc_read(h->ctx, ch);
    return raw < 0 ? raw : (int)(((int64_t)raw * vref(h) + full_scale(h->adc_bits ? h->adc_bits : 12) / 2) / full_scale(h->adc_bits ? h->adc_bits : 12));
}
NATIVE(adc_read_mv) {
    INT_ARG(0, ch);
    int rc = adc_mv(HAL(), ch);
    return rc < 0 ? hal_fail(vm, "ADC.ReadMillivolts", rc) : mcs_int(rc);
}
#if MCS_ENABLE_FLOAT
NATIVE(adc_read_volts) {
    INT_ARG(0, ch);
    int rc = adc_mv(HAL(), ch);
    return rc < 0 ? hal_fail(vm, "ADC.ReadVoltage", rc) : mcs_float((mcs_float_t)rc / 1000);
}
#endif
NATIVE(adc_average) {
    INT_ARG(0, ch); OPT_INT(1, n, 16);
    if (n < 1 || n > 4096) return range_fail(vm, "sample count");
    int64_t sum = 0;
    for (int i = 0; i < n; i++) {
        int rc = HAL()->adc_read(CTX(), ch);
        if (rc < 0) return hal_fail(vm, "ADC.ReadAverage", rc);
        sum += rc;
    }
    return mcs_int((mcs_int_t)((sum + n / 2) / n));
}
NATIVE(adc_resolution) { return mcs_int(HAL()->adc_bits ? HAL()->adc_bits : 12); }
NATIVE(adc_vref) { return mcs_int(vref(HAL())); }
/* ADC.Configure(channel, new AdcConfig { Attenuation = AdcConfig.Atten12dB }) */
NATIVE(adc_configure) {
    INT_ARG(0, ch);
    const mcs_adc_config_t* c = (const mcs_adc_config_t*)mcs__cfg_get(argv[1], MCS__CFG_ADC);
    if (!c) { mcs_raise(vm, "ArgumentException", "AdcConfig expected"); return mcs_null(); }
    const mcs_hal_t* h = HAL();
    if (h->adc_configure) { RC("ADC.Configure", h->adc_configure(h->ctx, ch, c)); return mcs_null(); }
    const char* nd = mcs__cfg_nondefault(c, MCS__CFG_ADC, NULL);
    return nd ? cfg_unsupported(vm, "ADC.Configure", nd) : mcs_null();
}
static const mcs_reg_t adc_fns[] = {
    MCS_FN("Configure", adc_configure, 2),
    MCS_GET("Resolution", adc_resolution), MCS_GET("ReferenceMillivolts", adc_vref),
    MCS_FN("Read", adc_read, 1), MCS_FN("ReadMillivolts", adc_read_mv, 1),
#if MCS_ENABLE_FLOAT
    MCS_FN("ReadVoltage", adc_read_volts, 1),
#endif
    MCS_FN("ReadAverage", adc_average, -1), MCS_REG_END
};

NATIVE(dac_write) {
    INT_ARG(0, ch); INT_ARG(1, v);
    int bits = HAL()->dac_bits ? HAL()->dac_bits : 8;
    if (v < 0 || v > full_scale(bits)) return range_fail(vm, "DAC value");
    RC("DAC.Write", HAL()->dac_write(CTX(), ch, (uint32_t)v));
    return mcs_null();
}
NATIVE(dac_write_mv) {
    INT_ARG(0, ch); INT_ARG(1, mv);
    const mcs_hal_t* h = HAL();
    int fs = full_scale(h->dac_bits ? h->dac_bits : 8);
    if (mv < 0 || mv > vref(h)) return range_fail(vm, "millivolts");
    RC("DAC.WriteMillivolts", h->dac_write(h->ctx, ch, (uint32_t)(((int64_t)mv * fs + vref(h) / 2) / vref(h))));
    return mcs_null();
}
NATIVE(dac_resolution) { return mcs_int(HAL()->dac_bits ? HAL()->dac_bits : 8); }
static const mcs_reg_t dac_fns[] = { MCS_GET("Resolution", dac_resolution), MCS_FN("Write", dac_write, 2), MCS_FN("WriteMillivolts", dac_write_mv, 2), MCS_REG_END };

/* ------------------------------------------------------------- PWM */
/* duty in 1/65535 */
static mcs_value_t pwm_apply(mcs_vm_t* vm, int ch, int freq, uint32_t duty16, const char* what) {
    const mcs_hal_t* h = HAL();
    if (freq <= 0) return range_fail(vm, "frequency");
    if (duty16 > 65535) return range_fail(vm, "duty cycle");
    int rc = h->pwm_set16 ? h->pwm_set16(h->ctx, ch, (uint32_t)freq, (uint16_t)duty16)
                          : h->pwm_set(h->ctx, ch, (uint32_t)freq, (uint16_t)((duty16 * 1000u + 32767u) / 65535u));
    return rc < 0 ? hal_fail(vm, what, rc) : mcs_null();
}
#if MCS_ENABLE_FLOAT
NATIVE(pwm_set) {
    INT_ARG(0, ch); INT_ARG(1, freq);
    mcs_float_t d = mcs_to_float(vm, argv[2]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (!(d >= 0 && d <= 1)) return range_fail(vm, "duty cycle");
    return pwm_apply(vm, ch, freq, (uint32_t)(d * 65535 + (mcs_float_t)0.5), "PWM.Set");
}
#endif
NATIVE(pwm_set_permille) {
    INT_ARG(0, ch); INT_ARG(1, freq); INT_ARG(2, pm);
    if (pm < 0 || pm > 1000) return range_fail(vm, "duty cycle");
    const mcs_hal_t* h = HAL();
    if (!h->pwm_set16) {   /* exact v1 behaviour */
        if (freq <= 0) return range_fail(vm, "frequency");
        RC("PWM.Set", h->pwm_set(h->ctx, ch, (uint32_t)freq, (uint16_t)pm));
        return mcs_null();
    }
    return pwm_apply(vm, ch, freq, ((uint32_t)pm * 65535u + 500u) / 1000u, "PWM.Set");
}
NATIVE(pwm_set_pulse) {   /* (ch, freqHz, pulseUs) */
    INT_ARG(0, ch); INT_ARG(1, freq); INT_ARG(2, us);
    if (freq <= 0) return range_fail(vm, "frequency");
    uint64_t period = 1000000u / (uint32_t)freq;
    if (us < 0 || (uint64_t)us > period) return range_fail(vm, "pulse width");
    return pwm_apply(vm, ch, freq, (uint32_t)(((uint64_t)us * 65535u + period / 2) / period), "PWM.SetPulse");
}
NATIVE(pwm_servo) {       /* (ch, angle 0..180[, minUs = 500, maxUs = 2500]) at 50 Hz */
    INT_ARG(0, ch); INT_ARG(1, angle); OPT_INT(2, lo, 500); OPT_INT(3, hi, 2500);
    if (angle < 0 || angle > 180) return range_fail(vm, "angle");
    if (lo < 0 || hi > 20000 || lo > hi) return range_fail(vm, "pulse range");
    uint32_t us = (uint32_t)lo + (uint32_t)(hi - lo) * (uint32_t)angle / 180u;
    return pwm_apply(vm, ch, 50, (us * 65535u + 10000u) / 20000u, "PWM.Servo");
}
NATIVE(pwm_tone) { INT_ARG(0, ch); INT_ARG(1, freq); return pwm_apply(vm, ch, freq, 32768, "PWM.Tone"); }
NATIVE(pwm_stop) {
    INT_ARG(0, ch);
    const mcs_hal_t* h = HAL();
    int rc = h->pwm_stop ? h->pwm_stop(h->ctx, ch) : h->pwm_set16 ? h->pwm_set16(h->ctx, ch, 1000, 0) : h->pwm_set(h->ctx, ch, 1000, 0);
    return rc < 0 ? hal_fail(vm, "PWM.Stop", rc) : mcs_null();
}
/* PWM.Configure(channel, new PwmConfig { Pin = 5, ResolutionBits = 12, Inverted = true }) */
NATIVE(pwm_configure) {
    INT_ARG(0, ch);
    const mcs_pwm_config_t* c = (const mcs_pwm_config_t*)mcs__cfg_get(argv[1], MCS__CFG_PWM);
    if (!c) { mcs_raise(vm, "ArgumentException", "PwmConfig expected"); return mcs_null(); }
    const mcs_hal_t* h = HAL();
    if (h->pwm_configure) { RC("PWM.Configure", h->pwm_configure(h->ctx, ch, c)); return mcs_null(); }
    static const char* const ok[] = { "Frequency", NULL };     /* the frequency comes with every PWM.Set */
    const char* nd = mcs__cfg_nondefault(c, MCS__CFG_PWM, ok);
    return nd ? cfg_unsupported(vm, "PWM.Configure", nd) : mcs_null();
}
static const mcs_reg_t pwm_fns[] = {
    MCS_FN("Configure", pwm_configure, 2),
#if MCS_ENABLE_FLOAT
    MCS_FN("Set", pwm_set, 3),           /* duty cycle 0.0 .. 1.0 */
#endif
    MCS_FN("SetPermille", pwm_set_permille, 3), MCS_FN("SetPulse", pwm_set_pulse, 3), MCS_FN("Servo", pwm_servo, -1),
    MCS_FN("Tone", pwm_tone, 2), MCS_FN("Stop", pwm_stop, 1), MCS_REG_END
};

/* ------------------------------------------------------------- Timer */
static mcs_value_t timer_common(mcs_vm_t* vm, mcs_value_t* argv, int periodic) {
    INT_ARG(0, id); INT_ARG(1, us);
    if (us <= 0) return range_fail(vm, "period");
    if (id < 0 || id > 0xFFFF) return range_fail(vm, "timer id");
    set_callback(vm, CB_TIMER, id, argv[2]);
    if (mcs_has_exception(vm)) return mcs_null();
    int rc = HAL()->timer_start(CTX(), id, (uint32_t)us, periodic);
    if (rc < 0) { cb_set(vm, ST(), CB_TIMER, id, mcs_null()); return hal_fail(vm, "Timer.Start", rc); }
    return mcs_null();
}
NATIVE(timer_start) { return timer_common(vm, argv, 1); }   /* (id, periodUs, callback) */
NATIVE(timer_once) { return timer_common(vm, argv, 0); }    /* (id, delayUs, callback) */
NATIVE(timer_stop) {
    INT_ARG(0, id);
    if (HAL()->timer_stop) RC("Timer.Stop", HAL()->timer_stop(CTX(), id));
    cb_set(vm, ST(), CB_TIMER, id, mcs_null());
    return mcs_null();
}
static const mcs_reg_t timer_fns[] = {
    MCS_FN("Start", timer_start, 3), MCS_FN("Once", timer_once, 3), MCS_FN("Stop", timer_stop, 1), MCS_REG_END
};

/* ------------------------------------------------------------- I2S */
static int i2s_width(mcs_vm_t* vm, int bus) {
    int b = bus >= 0 && bus < MCS_HAL_MAX_BUSES ? ST()->i2s_bits[bus] : 0;
    return b == 16 ? 2 : b ? 4 : 2;
}
NATIVE(i2s_open) {   /* (bus, sampleRate, bits, channels[, direction[, format]]) */
    INT_ARG(0, bus); INT_ARG(1, rate); INT_ARG(2, bits); INT_ARG(3, ch);
    OPT_INT(4, dir, MCS_I2S_TX); OPT_INT(5, fmt, MCS_I2S_PHILIPS);
    if (rate <= 0) return range_fail(vm, "sample rate");
    if (bits != 16 && bits != 24 && bits != 32) return range_fail(vm, "bits per sample");
    if (ch < 1 || ch > 2 || dir < 1 || dir > 3 || fmt < 0 || fmt > 2) return range_fail(vm, "I2S format");
    mcs_i2s_cfg_t c = { (uint32_t)rate, (uint8_t)bits, (uint8_t)ch, (uint8_t)dir, (uint8_t)fmt };
    RC("I2S.Open", HAL()->i2s_open(CTX(), bus, &c));
    if (bus >= 0 && bus < MCS_HAL_MAX_BUSES) ST()->i2s_bits[bus] = (uint8_t)bits;
    return mcs_null();
}
NATIVE(i2s_write) {
    INT_ARG(0, bus); OPT_INT(2, timeout, 1000);
    uint8_t buf[MCS_HAL_MAX_XFER]; size_t n;
    if (!to_bytes(vm, argv[1], buf, &n)) return mcs_null();
    NEED(i2s_write, "I2S.Write");
    int rc = HAL()->i2s_write(CTX(), bus, buf, n, (uint32_t)timeout);
    return rc < 0 ? hal_fail(vm, "I2S.Write", rc) : mcs_int(rc);
}
NATIVE(i2s_read) {
    INT_ARG(0, bus); OPT_INT(2, timeout, 1000);
    int n = count_arg(vm, argv[1]);
    if (n < 0) return mcs_null();
    NEED(i2s_read, "I2S.Read");
    uint8_t buf[MCS_HAL_MAX_XFER];
    int rc = HAL()->i2s_read(CTX(), bus, buf, (size_t)n, (uint32_t)timeout);
    return rc < 0 ? hal_fail(vm, "I2S.Read", rc) : bytes_val(vm, buf, (size_t)rc);
}
/* int[] samples, little endian, 2 bytes per sample for 16-bit streams, else 4 */
NATIVE(i2s_write_samples) {
    INT_ARG(0, bus); OPT_INT(2, timeout, 1000);
    int k = mcs_obj_kind(argv[1]);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "expected int[]"); return mcs_null(); }
    NEED(i2s_write, "I2S.WriteSamples");
    int w = i2s_width(vm, bus);
    uint32_t count = mcs_len(argv[1]);
    if ((size_t)count * (size_t)w > MCS_HAL_MAX_XFER) return range_fail(vm, "sample count");
    uint8_t buf[MCS_HAL_MAX_XFER];
    for (uint32_t i = 0; i < count; i++) {
        int32_t v = (int32_t)mcs_to_int(vm, mcs_index(argv[1], i));
        if (mcs_has_exception(vm)) return mcs_null();
        for (int b = 0; b < w; b++) buf[i * (uint32_t)w + (uint32_t)b] = (uint8_t)((uint32_t)v >> (8 * b));
    }
    int rc = HAL()->i2s_write(CTX(), bus, buf, (size_t)count * (size_t)w, (uint32_t)timeout);
    return rc < 0 ? hal_fail(vm, "I2S.WriteSamples", rc) : mcs_int(rc / w);
}
NATIVE(i2s_read_samples) {
    INT_ARG(0, bus); INT_ARG(1, count); OPT_INT(2, timeout, 1000);
    NEED(i2s_read, "I2S.ReadSamples");
    int w = i2s_width(vm, bus);
    if (count < 0 || (size_t)count * (size_t)w > MCS_HAL_MAX_XFER) return range_fail(vm, "sample count");
    uint8_t buf[MCS_HAL_MAX_XFER];
    int rc = HAL()->i2s_read(CTX(), bus, buf, (size_t)count * (size_t)w, (uint32_t)timeout);
    if (rc < 0) return hal_fail(vm, "I2S.ReadSamples", rc);
    int got = rc / w;
    mcs_value_t a = mcs_new_array(vm, (uint32_t)got);
    for (int i = 0; i < got; i++) {
        uint32_t v = 0;
        for (int b = 0; b < w; b++) v |= (uint32_t)buf[i * w + b] << (8 * b);
        int32_t s = w == 2 ? (int16_t)v : (int32_t)v;
        mcs_set_index(a, (uint32_t)i, mcs_int(s));
    }
    return a;
}
NATIVE(i2s_close) { INT_ARG(0, bus); if (HAL()->i2s_close) RC("I2S.Close", HAL()->i2s_close(CTX(), bus)); return mcs_null(); }
static const mcs_reg_t i2s_fns[] = {
    MCS_FN("Open", i2s_open, -1), MCS_FN("Write", i2s_write, -1), MCS_FN("Read", i2s_read, -1),
    MCS_FN("WriteSamples", i2s_write_samples, -1), MCS_FN("ReadSamples", i2s_read_samples, -1),
    MCS_FN("Close", i2s_close, 1), MCS_REG_END
};

/* ------------------------------------------------------------- QSPI */
NATIVE(qspi_open) {
    const mcs_qspi_config_t* c = argc == 2 ? (const mcs_qspi_config_t*)mcs__cfg_get(argv[1], MCS__CFG_QSPI) : NULL;
    if (c) {
        INT_ARG(0, b);
        const mcs_hal_t* h = HAL();
        if (h->qspi_configure) { RC("QSPI.Open", h->qspi_configure(h->ctx, b, c)); return mcs_null(); }
        static const char* const ok[] = { "Frequency", NULL };
        const char* nd = mcs__cfg_nondefault(c, MCS__CFG_QSPI, ok);
        if (nd) return cfg_unsupported(vm, "QSPI.Open", nd);
        if (h->qspi_open) RC("QSPI.Open", h->qspi_open(h->ctx, b, c->freq_hz ? c->freq_hz : 10000000));
        return mcs_null();
    }
    INT_ARG(0, bus); OPT_INT(1, freq, 10000000);
    if (freq <= 0) return range_fail(vm, "frequency");
    if (HAL()->qspi_open) RC("QSPI.Open", HAL()->qspi_open(CTX(), bus, (uint32_t)freq));
    return mcs_null();
}
static bool lines_ok(int l) { return l == 0 || l == 1 || l == 2 || l == 4 || l == 8; }
/* data: byte[] to write, an int count to read, or null */
static mcs_value_t qspi_run(mcs_vm_t* vm, int bus, mcs_qspi_cmd_t* c, mcs_value_t data, const char* what, bool read_into) {
    if (!lines_ok(c->instr_lines) || !lines_ok(c->addr_lines) || !lines_ok(c->data_lines) || c->addr_bytes > 4)
        return range_fail(vm, "QSPI phase");
    uint8_t buf[MCS_HAL_MAX_XFER]; size_t n = 0;
    const uint8_t* tp = buf; uint8_t* rp = buf;
    bool rd = mcs_is_number(data), into = false;
    if (rd) { int k = count_arg(vm, data); if (k < 0) return mcs_null(); n = (size_t)k; }
    else if (read_into && mcs__dmabuf(data, &rp, &n)) rd = into = true;   /* read into a DmaBuffer */
    else if (!bytes_ref(vm, data, buf, &tp, &n)) return mcs_null();
    if (n && !c->data_lines) c->data_lines = 1;
    if (!mcs__dma_idle(vm, MCS__DMA_QSPI, bus)) return mcs_null();
    int rc = HAL()->qspi_command(CTX(), bus, c, rd ? NULL : tp, rd ? rp : NULL, n);
    if (rc < 0) return hal_fail(vm, what, rc);
    if (into) return data;
    return rd ? bytes_val(vm, buf, n) : mcs_null();
}
static mcs_qspi_cmd_t qcmd(int instr, int addr, int addr_bytes, int lines, int dummy) {
    mcs_qspi_cmd_t c;
    memset(&c, 0, sizeof c);
    c.instruction = (uint8_t)instr; c.instr_lines = 1;
    if (addr >= 0 || addr_bytes == 4) { c.address = (uint32_t)addr; c.addr_bytes = (uint8_t)addr_bytes; c.addr_lines = lines == 4 && (instr == 0xEB || instr == 0xBB || instr == 0x38) ? (uint8_t)lines : 1; }
    c.dummy_cycles = (uint8_t)dummy; c.data_lines = (uint8_t)lines;
    return c;
}
NATIVE(qspi_command) {   /* (bus, instruction[, address]) */
    INT_ARG(0, bus); INT_ARG(1, instr); OPT_INT(2, addr, -1);
    mcs_qspi_cmd_t c = qcmd(instr, addr, addr >= 0 ? 3 : 0, 0, 0);
    return qspi_run(vm, bus, &c, mcs_null(), "QSPI.Command", false);
}
NATIVE(qspi_read) {      /* (bus, instruction, address, count[, dummyCycles[, dataLines[, addrBytes]]]) address -1 = none */
    INT_ARG(0, bus); INT_ARG(1, instr); INT_ARG(2, addr);
    OPT_INT(4, dummy, 0); OPT_INT(5, lines, 1); OPT_INT(6, ab, 3);
    if (ab < 1 || ab > 4) return range_fail(vm, "address bytes");
    mcs_qspi_cmd_t c = qcmd(instr, addr, addr >= 0 ? ab : 0, lines, dummy);
    uint8_t* db; size_t dl;
    if (!mcs_is_number(argv[3]) && !mcs__dmabuf(argv[3], &db, &dl)) { mcs_raise(vm, "ArgumentException", "count or DmaBuffer expected"); return mcs_null(); }
    return qspi_run(vm, bus, &c, argv[3], "QSPI.Read", true);
}
NATIVE(qspi_write) {     /* (bus, instruction, address, data[, dataLines[, addrBytes]]) */
    INT_ARG(0, bus); INT_ARG(1, instr); INT_ARG(2, addr);
    OPT_INT(4, lines, 1); OPT_INT(5, ab, 3);
    if (ab < 1 || ab > 4) return range_fail(vm, "address bytes");
    mcs_qspi_cmd_t c = qcmd(instr, addr, addr >= 0 ? ab : 0, lines, 0);
    if (mcs_is_number(argv[3])) { mcs_raise(vm, "ArgumentException", "byte[] expected"); return mcs_null(); }
    return qspi_run(vm, bus, &c, argv[3], "QSPI.Write", false);
}
/* full control: (bus, instr, instrLines, address, addrBytes, addrLines, dummy, dataLines, dataOrCount) */
NATIVE(qspi_transfer) {
    INT_ARG(0, bus); INT_ARG(1, instr); INT_ARG(2, il); INT_ARG(3, addr); INT_ARG(4, ab);
    INT_ARG(5, al); INT_ARG(6, dummy); INT_ARG(7, dl);
    if (ab < 0 || ab > 4 || dummy < 0 || dummy > 255) return range_fail(vm, "QSPI phase");
    mcs_qspi_cmd_t c;
    memset(&c, 0, sizeof c);
    c.instruction = (uint8_t)instr; c.instr_lines = (uint8_t)il; c.address = (uint32_t)addr;
    c.addr_bytes = (uint8_t)ab; c.addr_lines = (uint8_t)(ab ? al : 0); c.dummy_cycles = (uint8_t)dummy; c.data_lines = (uint8_t)dl;
    return qspi_run(vm, bus, &c, argv[8], "QSPI.Transfer", false);
}
static const mcs_reg_t qspi_fns[] = {
    MCS_FN("Open", qspi_open, -1), MCS_FN("Command", qspi_command, -1), MCS_FN("Read", qspi_read, -1),
    MCS_FN("Write", qspi_write, -1), MCS_FN("Transfer", qspi_transfer, 9), MCS_REG_END
};

/* CAN lives in mcs_hal_can.c; callbacks share this library's pinned list. */
mcs_value_t mcs__can_onreceive(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    mcs_int_t bus = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (bus < 0 || bus >= MCS_HAL_MAX_BUSES) return range_fail(vm, "CAN bus");
    mcs_value_t r = set_callback(vm, CB_CAN, (int)bus, argv[1]);
    if (!mcs_has_exception(vm)) mcs__can_subscribe(vm, (int)bus, !mcs_is_null(argv[1]));
    return r;
}

/* ------------------------------------------------------------- Watchdog / RTC */
NATIVE(wdt_start) {
    INT_ARG(0, ms);
    if (ms <= 0) return range_fail(vm, "timeout");
    RC("Watchdog.Start", HAL()->wdt_start(CTX(), (uint32_t)ms));
    return mcs_null();
}
NATIVE(wdt_feed) { if (HAL()->wdt_feed) RC("Watchdog.Feed", HAL()->wdt_feed(CTX())); return mcs_null(); }
static const mcs_reg_t wdt_fns[] = { MCS_FN("Start", wdt_start, 1), MCS_FN("Feed", wdt_feed, 0), MCS_REG_END };

NATIVE(rtc_now) {
    uint32_t t = 0;
    RC("RTC.Now", HAL()->rtc_get(CTX(), &t));
    return mcs_int((mcs_int_t)t);
}
NATIVE(rtc_set) {
    mcs_int_t t = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    NEED(rtc_set, "RTC.Set");
    RC("RTC.Set", HAL()->rtc_set(CTX(), (uint32_t)t));
    return mcs_null();
}
static const mcs_reg_t rtc_fns[] = { MCS_GET("Now", rtc_now), MCS_FN("Set", rtc_set, 1), MCS_REG_END };

/* ------------------------------------------------------------- Hal */
NATIVE(hal_has) {
    const char* n = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs_bool(mcs_capability_has(vm, n));
}
NATIVE(hal_poll) {
    int r = mcs_hal_poll(vm);
    return mcs_int(r < 0 ? 0 : r);
}
/* Hal.Run([ms]): dispatch events (and sleep) for ms, or until the run is aborted */
NATIVE(hal_run) {
    OPT_INT(0, ms, -1);
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        mcs_hal_poll(vm);
        if (mcs_has_exception(vm) || mcs_safepoint(vm)) return mcs_null();
        if (ms >= 0 && (uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)ms) return mcs_null();
        mcs_sleep(vm, 1);
        if (mcs_has_exception(vm)) return mcs_null();
    }
}
NATIVE(hal_micros) {
    const mcs_hal_t* h = HAL();
    return mcs_int((mcs_int_t)(h->micros ? h->micros(h->ctx) : mcs_ticks(vm) * 1000u));
}
NATIVE(hal_delay_us) {
    INT_ARG(0, us);
    const mcs_hal_t* h = HAL();
    if (us <= 0) return mcs_null();
    if (h->delay_us) h->delay_us(h->ctx, (uint32_t)us);
    else if (h->micros) { uint32_t t0 = h->micros(h->ctx); while ((uint32_t)(h->micros(h->ctx) - t0) < (uint32_t)us) {} }
    else mcs_sleep(vm, ((uint32_t)us + 999u) / 1000u);
    return mcs_null();
}
NATIVE(hal_reset) { NEED(reset, "Hal.Reset"); RC("Hal.Reset", HAL()->reset(CTX())); return mcs_null(); }
NATIVE(hal_uid) {
    const mcs_hal_t* h = HAL();
    uint8_t id[32];
    int n = h->unique_id ? h->unique_id(h->ctx, id, sizeof id) : 0;
    if (n < 0) return hal_fail(vm, "Hal.UniqueId", n);
    char hex[65];
    for (int i = 0; i < n && i < 32; i++) snprintf(hex + 2 * i, 3, "%02X", id[i]);
    hex[n > 0 ? 2 * (n < 32 ? n : 32) : 0] = 0;
    return mcs_string(vm, hex);
}
NATIVE(hal_cpuhz) { return mcs_int((mcs_int_t)HAL()->cpu_hz); }
NATIVE(hal_dropped) { return mcs_int((mcs_int_t)mcs_hal_dropped_events()); }
NATIVE(hal_onevent) { INT_ARG(0, n); if (n < 0 || n > 255 - MCS_HAL_EV_USER) return range_fail(vm, "event id"); return set_callback(vm, CB_USER, n, argv[1]); }
NATIVE(hal_post) {       /* raise a user event from C# (handled at the next poll) */
    INT_ARG(0, n); OPT_INT(1, src, 0); OPT_INT(2, val, 0);
    if (n < 0 || n > 255 - MCS_HAL_EV_USER) return range_fail(vm, "event id");
    return mcs_bool(mcs_hal_post(MCS_HAL_EV_USER + n, src, val));
}
NATIVE(hal_board) { return mcs_string(vm, HAL()->board ? HAL()->board : "unknown"); }
static const mcs_reg_t hal_fns[] = {
    MCS_GET("Board", hal_board),
    MCS_FN("Has", hal_has, 1), MCS_FN("Poll", hal_poll, 0), MCS_FN("Run", hal_run, -1),
    MCS_GET("Micros", hal_micros), MCS_FN("DelayMicroseconds", hal_delay_us, 1), MCS_FN("Reset", hal_reset, 0),
    MCS_GET("UniqueId", hal_uid), MCS_GET("CpuHz", hal_cpuhz), MCS_GET("DroppedEvents", hal_dropped),
    MCS_FN("OnEvent", hal_onevent, 2), MCS_FN("Post", hal_post, -1), MCS_REG_END
};

/* ------------------------------------------------------------- registration */
static const mcs_class_def_t state_def = { "__HalState", sizeof(hal_state_t), NULL, NULL, NULL, NULL };

const mcs_hal_t* mcs_hal_get(mcs_vm_t* vm) {
    hal_state_t* s = ST();
    return s ? s->hal : NULL;
}

void mcs_hal_close_lib(mcs_vm_t* vm) {
    hal_state_t* s = ST();
    if (!s) return;
    mcs__can_close_all(vm);
    mcs__dma_close(vm);                           /* stop / finish transfers still in flight */
    mcs_set_ext(vm, MCS_EXT_HAL, NULL);
    mcs_set_idle(vm, NULL, NULL);
    int a = s->cb_pin, b = s->self_pin;
    if (a >= 0) mcs_unpin(vm, a);
    if (b >= 0) mcs_unpin(vm, b);
}

/* Encoding / BitConverter / Convert.ToBase64String... live in the core library (src/mcs_lib_bytes.c) */
static const mcs_const_t hal_consts[] = { MCS_CONST("ApiVersion", MCS_HAL_API_VERSION), MCS_CONST_END };
static const mcs_const_t gpio_consts[] = {
    MCS_CONST("Input", MCS_GPIO_INPUT), MCS_CONST("Output", MCS_GPIO_OUTPUT),
    MCS_CONST("InputPullUp", MCS_GPIO_INPUT_PULLUP), MCS_CONST("InputPullDown", MCS_GPIO_INPUT_PULLDOWN),
    MCS_CONST("OpenDrain", MCS_GPIO_OPEN_DRAIN), MCS_CONST("Analog", MCS_GPIO_ANALOG),
    MCS_CONST("Rising", MCS_GPIO_EDGE_RISING), MCS_CONST("Falling", MCS_GPIO_EDGE_FALLING),
    MCS_CONST("Both", MCS_GPIO_EDGE_BOTH), MCS_CONST_END
};
NATIVE(gpio_high) { return mcs_bool(true); }
NATIVE(gpio_low) { return mcs_bool(false); }
static const mcs_reg_t gpio_props[] = { MCS_GET("High", gpio_high), MCS_GET("Low", gpio_low), MCS_REG_END };
static const mcs_const_t uart_consts[] = {
    MCS_CONST("ParityNone", MCS_UART_PARITY_NONE), MCS_CONST("ParityOdd", MCS_UART_PARITY_ODD),
    MCS_CONST("ParityEven", MCS_UART_PARITY_EVEN), MCS_CONST_END
};
static const mcs_const_t i2s_consts[] = {
    MCS_CONST("Transmit", MCS_I2S_TX), MCS_CONST("Receive", MCS_I2S_RX), MCS_CONST("Duplex", MCS_I2S_DUPLEX),
    MCS_CONST("Philips", MCS_I2S_PHILIPS), MCS_CONST("Msb", MCS_I2S_MSB), MCS_CONST("Pcm", MCS_I2S_PCM), MCS_CONST_END
};

void mcs_hal_open_lib(mcs_vm_t* vm, const mcs_hal_t* h) { mcs_hal_open_lib_ex(vm, h, 0); }

void mcs_hal_open_lib_ex(mcs_vm_t* vm, const mcs_hal_t* h, unsigned flags) {
    hal_state_t* s = ST();
    if (!s) {
        mcs_register_class(vm, &state_def);
        mcs_value_t obj;
        if (mcs_new_object(vm, "__HalState", 0, NULL, &obj) != MCS_OK) return;
        s = (hal_state_t*)mcs_userdata(obj);
        memset(s, 0, sizeof *s);
        s->self_pin = mcs_pin(vm, obj);
        mcs_value_t list = mcs_new_list(vm);
        mcs_push_root(vm, list);
        for (int i = 0; i < MCS__REF_COUNT; i++) mcs_list_add(vm, list, mcs_null());
        s->cb_pin = mcs_pin(vm, list);
        mcs_pop_root(vm, 1);
        if (s->self_pin < 0 || s->cb_pin < 0) {
            if (s->self_pin >= 0) mcs_unpin(vm, s->self_pin);
            if (s->cb_pin >= 0) mcs_unpin(vm, s->cb_pin);
            mcs_fail(vm, MCS_ERR_MEMORY, "HAL: no free pin handles (MCS_MAX_PINS)");
            return;
        }
        mcs_set_ext(vm, MCS_EXT_HAL, s);
    }
    if (s->hal) mcs__can_close_all(vm);
    s->hal = h;
    s->wake_pin = -1;
    s->wake_high = 0;
    memset(s->spi_valid, 0, sizeof s->spi_valid);
    for (int i = 0; i < MCS_HAL_MAX_BUSES; i++) { s->spi_cs[i] = -1; s->spi_cs_high[i] = 0; }
    s->no_events = (flags & MCS_HAL_NO_EVENTS) != 0;
    if (!s->no_events) mcs_set_idle(vm, idle_poll, NULL);

    mcs_register_module(vm, "Hal", hal_fns);
    mcs__power_open(vm);
    mcs_register_consts(vm, "Hal", hal_consts);
    if (h && (h->gpio_write != NULL)) {
        mcs_register_module(vm, "GPIO", gpio_fns);
        mcs_register_module(vm, "GPIO", gpio_props);
        mcs_register_consts(vm, "GPIO", gpio_consts);
        mcs_register_class(vm, &pin_def);
    }
    if (h && (h->uart_write != NULL)) {
        mcs_register_module(vm, "UART", uart_fns);
        mcs_register_consts(vm, "UART", uart_consts);
    }
    if (h && (h->i2c_write != NULL)) { mcs_register_module(vm, "I2C", i2c_fns); mcs_register_class(vm, &i2cdev_def); }
    if (h && (h->spi_transfer != NULL)) { mcs_register_module(vm, "SPI", spi_fns); mcs_register_class(vm, &spidev_def); }
    if (h && (h->adc_read != NULL)) mcs_register_module(vm, "ADC", adc_fns);
    if (h && (h->dac_write != NULL)) mcs_register_module(vm, "DAC", dac_fns);
    if (h && (h->pwm_set != NULL || h->pwm_set16 != NULL)) mcs_register_module(vm, "PWM", pwm_fns);
    if (h && (h->timer_start != NULL)) mcs_register_module(vm, "Timer", timer_fns);
    if (h && (h->i2s_open != NULL)) {
        mcs_register_module(vm, "I2S", i2s_fns);
        mcs_register_consts(vm, "I2S", i2s_consts);
    }
    if (h && (h->qspi_command != NULL)) mcs_register_module(vm, "QSPI", qspi_fns);
    mcs__can_open(vm);
    if (h && (h->wdt_start != NULL)) mcs_register_module(vm, "Watchdog", wdt_fns);
    if (h && (h->rtc_get != NULL)) mcs_register_module(vm, "RTC", rtc_fns);
    mcs__cfg_open(vm, h);                         /* GpioConfig, SpiConfig ... + Hal.Options */
    mcs__dma_open(vm, h);                         /* DmaBuffer, DmaTransfer, Dma + the *Async methods */
#if MCS_ENABLE_DRIVERS
#if MCS_ENABLE_U8G2
    mcs_u8g2_use_hal(h);                          /* "u8g2" driver: C# U8g2 / U8x8 displays */
#endif
#if MCS_ENABLE_SERVO
    mcs_servo_use_hal(h);                         /* default "servo" backend: this board's PWM */
#endif
    mcs_drivers_open(vm);                         /* Drivers class + every registered driver */
#endif
#if MCS_ENABLE_NET
    mcs_net_open_lib(vm, mcs_net_get());          /* Wifi, EspNow, sockets, Http, Ntp ... (mcs_net.h) */
#endif
#if MCS_ENABLE_BLE
    mcs_ble_open_lib(vm, mcs_ble_get());          /* Ble, BleService, BleConnection ... (mcs_ble.h) */
#endif
}
#endif

/* Capability reporting is also available in builds without the HAL library. */
#include <string.h>
static bool capability_net(mcs_vm_t* vm, const char* name) {
#if MCS_ENABLE_NET
    if (!mcs_global_exists(vm, "Net")) return false;
    const mcs_net_t* net = mcs_net_get();
    if (!strcmp(name, "Wifi.Save")) return net && (net->creds || (MCS_ENABLE_FS && mcs_get_ext(vm, MCS_EXT_VFS) && net->wifi_set_mode));
    return mcs_net_has(net, name);
#else
    (void)vm; (void)name; return false;
#endif
}
static bool capability_ble(mcs_vm_t* vm, const char* name) {
#if MCS_ENABLE_BLE
    return mcs_global_exists(vm, "Ble") && mcs_ble_has(mcs_ble_get(), name);
#else
    (void)vm; (void)name; return false;
#endif
}
/* One shared string table / lookup instead of a strcmp chain per capability.
 * Grouped switch cases let tiny MCUs share the HAL/NET/BLE predicate code. */
enum {
#define MCS_CAP(name, predicate, id) id,
#include "mcs_capabilities.def"
#undef MCS_CAP
    MCS_CAP_COUNT
};
static const char* const capability_names[MCS_CAP_COUNT] = {
#define MCS_CAP(name, predicate, id) name,
#include "mcs_capabilities.def"
#undef MCS_CAP
};
bool mcs_capability_has(mcs_vm_t* vm, const char* feature) {
    if (!vm || !feature) return false;
#if MCS_ENABLE_HAL
    const mcs_hal_t* h = mcs_hal_get(vm);
#else
    const mcs_hal_t* h = NULL;
#endif
    unsigned i;
    for (i = 0; i < MCS_CAP_COUNT; i++) if (!strcmp(feature, capability_names[i])) break;
#define CAP_GLOBAL(name) mcs_global_exists(vm, name)
#define CAP_HW(predicate) (h != NULL && (predicate))
#define CAP_NET(name) capability_net(vm, feature)
#define CAP_BLE(name) capability_ble(vm, feature)
    switch (i) {
#define MCS_CAP(name, predicate, id) case id: return (predicate);
#include "mcs_capabilities.def"
#undef MCS_CAP
    default: break;
    }
#undef CAP_GLOBAL
#undef CAP_HW
#undef CAP_NET
#undef CAP_BLE
    /* Preserve driver / backend aliases outside the canonical Studio vocabulary. */
#if MCS_ENABLE_DRIVERS
    if (h && mcs_global_exists(vm, "Drivers") && mcs_driver_provides(feature)) return true;
#endif
    return capability_net(vm, feature) || capability_ble(vm, feature);
}
const char* const* mcs_capability_names(size_t* count) {
    if (count) *count = MCS_CAP_COUNT;
    return capability_names;
}
