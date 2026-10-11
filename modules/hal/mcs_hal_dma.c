/*
 * MicroCS - DMA buffers and asynchronous transfers (HAL API v3).
 *
 * C#:  var frame = new DmaBuffer(4096);                 // DMA-capable memory
 *      DmaTransfer t = SPI.WriteAsync(1, frame, cs: 5);  // returns at once
 *      t.OnComplete(n => Console.WriteLine($"{n} bytes sent"));
 *      ... other work ...
 *      t.Wait();
 * Boards with DMA run the transfer in the background (spi_transfer_async ...)
 * and report MCS_HAL_EV_DMA; everywhere else the same calls run synchronously
 * and complete immediately, so scripts stay portable. Transfers on one bus run
 * in order: a new transfer (async or not) waits for the previous one.
 */
#include "mcs_hal.h"
#include "mcs_hal_internal.h"
#if MCS_ENABLE_HAL && MCS_ENABLE_HAL_DMA
#include <string.h>
#include <stdio.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#if MCS_HAL_MAX_DMA < 1 || MCS_HAL_MAX_DMA > 8
#error "MCS_HAL_MAX_DMA must be 1..8"
#endif

#define MAX_BUFFER (16u * 1024u * 1024u)

/* ------------------------------------------------------------ DmaBuffer */
typedef struct {
    uint8_t* data;                    /* aligned start */
    uint32_t len;
    void* raw;                        /* allocation */
    size_t raw_size;                  /* VM heap: bytes to give back */
    void (*pfree)(void* ctx, void* p);/* port allocator (dma_alloc), NULL = VM heap */
    void* pctx;
    uint8_t busy;                     /* transfers using the buffer */
} dmabuf_t;
static const mcs_class_def_t dmabuf_def, xfer_def;

bool mcs__dmabuf(mcs_value_t v, uint8_t** data, size_t* len) {
    dmabuf_t* b = (dmabuf_t*)mcs_userdata_of(v, &dmabuf_def);
    if (!b) return false;
    *data = b->data; *len = b->len;
    return true;
}
static void dmabuf_free(mcs_vm_t* vm, void* data) {
    dmabuf_t* b = (dmabuf_t*)data;
    if (!b->raw) return;
    if (b->pfree) b->pfree(b->pctx, b->raw);
    else mcs_mem_realloc(vm, b->raw, b->raw_size, 0);
    b->raw = NULL; b->data = NULL; b->len = 0;
}
static bool dmabuf_alloc(mcs_vm_t* vm, dmabuf_t* b, size_t n) {
    const mcs_hal_t* h = ST() ? HAL() : NULL;
    size_t align = h && h->dma_align ? h->dma_align : 4;
    if (align & (align - 1)) align = 4;
    size_t rounded = (n + align - 1) & ~(align - 1);     /* whole cache lines: no neighbour shares them */
    if (!rounded) rounded = align;
    if (h && h->dma_alloc) {
        b->raw = h->dma_alloc(h->ctx, rounded);
        if (!b->raw) { mcs_raise(vm, "OutOfMemoryException", "no DMA-capable memory for %d bytes", (int)n); return false; }
        b->pfree = h->dma_free; b->pctx = h->ctx;
        b->data = (uint8_t*)(((uintptr_t)b->raw + align - 1) & ~(uintptr_t)(align - 1));
    } else {
        b->raw_size = rounded + align - 1;
        b->raw = mcs_mem_realloc(vm, NULL, 0, b->raw_size);
        if (!b->raw) return false;
        b->data = (uint8_t*)(((uintptr_t)b->raw + align - 1) & ~(uintptr_t)(align - 1));
    }
    memset(b->data, 0, rounded);
    b->len = (uint32_t)n;
    return true;
}
/* DmaBuffer(size) or DmaBuffer(byte[] / string / DmaBuffer) */
static void dmabuf_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    dmabuf_t* b = (dmabuf_t*)mcs_userdata(self);
    if (argc != 1) { mcs_raise(vm, "ArgumentException", "DmaBuffer(size) or DmaBuffer(bytes) expected"); return; }
    if (mcs_is_number(argv[0])) {
        mcs_int_t n = mcs_to_int(vm, argv[0]);
        if (mcs_has_exception(vm)) return;
        if (n < 0 || n > (mcs_int_t)MAX_BUFFER) { mcs__range_fail(vm, "DmaBuffer size"); return; }
        dmabuf_alloc(vm, b, (size_t)n);
        return;
    }
    uint8_t* src; size_t n;
    if (mcs__dmabuf(argv[0], &src, &n)) { if (dmabuf_alloc(vm, b, n)) memcpy(b->data, src, n); return; }
    if (mcs_is_string(argv[0])) { n = mcs_strlen(argv[0]); if (dmabuf_alloc(vm, b, n)) memcpy(b->data, mcs_cstr(argv[0]), n); return; }
    int k = mcs_obj_kind(argv[0]);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "DmaBuffer(size) or DmaBuffer(byte[]) expected"); return; }
    n = mcs_len(argv[0]);
    if (n > MAX_BUFFER) { mcs__range_fail(vm, "DmaBuffer size"); return; }
    if (!dmabuf_alloc(vm, b, n)) return;
    for (size_t i = 0; i < n; i++) {
        b->data[i] = (uint8_t)mcs_to_int(vm, mcs_index(argv[0], (uint32_t)i));
        if (mcs_has_exception(vm)) return;
    }
}
#define BUF() dmabuf_t* b = (dmabuf_t*)mcs_check_userdata(vm, self, &dmabuf_def); if (!b) return mcs_null()
static bool writable(mcs_vm_t* vm, dmabuf_t* b) {
    if (!b->busy) return true;
    mcs_raise(vm, "InvalidOperationException", "DmaBuffer is in use by a DMA transfer (Wait() for it first)");
    return false;
}
/* optional (offset, count) at argv[i], argv[i + 1] within len */
static bool span(mcs_vm_t* vm, int argc, mcs_value_t* argv, int i, uint32_t len, uint32_t* off, uint32_t* cnt) {
    mcs_int_t o = argc > i ? mcs_to_int(vm, argv[i]) : 0;
    if (mcs_has_exception(vm)) return false;
    if (o < 0 || (uint64_t)o > len) { mcs__range_fail(vm, "offset"); return false; }
    mcs_int_t c = argc > i + 1 ? mcs_to_int(vm, argv[i + 1]) : (mcs_int_t)(len - (uint32_t)o);
    if (mcs_has_exception(vm)) return false;
    if (c < 0 || (uint64_t)o + (uint64_t)c > len) { mcs__range_fail(vm, "count"); return false; }
    *off = (uint32_t)o; *cnt = (uint32_t)c;
    return true;
}
NATIVE(buf_length) { BUF(); return mcs_int((mcs_int_t)b->len); }
NATIVE(buf_busy) { BUF(); return mcs_bool(b->busy != 0); }
NATIVE(buf_get) {
    BUF();
    mcs_int_t i = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (i < 0 || (uint64_t)i >= b->len) { mcs_raise(vm, "IndexOutOfRangeException", "index %d outside DmaBuffer[%d]", (int)i, (int)b->len); return mcs_null(); }
    return mcs_int(b->data[i]);
}
NATIVE(buf_set) {
    BUF();
    mcs_int_t i = mcs_to_int(vm, argv[0]), v = mcs_to_int(vm, argv[1]);
    if (mcs_has_exception(vm) || !writable(vm, b)) return mcs_null();
    if (i < 0 || (uint64_t)i >= b->len) { mcs_raise(vm, "IndexOutOfRangeException", "index %d outside DmaBuffer[%d]", (int)i, (int)b->len); return mcs_null(); }
    b->data[i] = (uint8_t)v;
    return mcs_null();
}
NATIVE(buf_fill) {    /* Fill(value[, offset[, count]]) */
    BUF();
    mcs_int_t v = mcs_to_int(vm, argv[0]);
    uint32_t off, cnt;
    if (mcs_has_exception(vm) || !span(vm, argc, argv, 1, b->len, &off, &cnt) || !writable(vm, b)) return mcs_null();
    memset(b->data + off, (int)(uint8_t)v, cnt);
    return mcs_null();
}
NATIVE(buf_copyfrom) {    /* CopyFrom(byte[] / string / DmaBuffer[, offset]) -> bytes copied */
    BUF();
    mcs_int_t off = argc > 1 ? mcs_to_int(vm, argv[1]) : 0;
    if (mcs_has_exception(vm) || !writable(vm, b)) return mcs_null();
    if (off < 0 || (uint64_t)off > b->len) return mcs__range_fail(vm, "offset");
    uint8_t* src; size_t n;
    if (mcs__dmabuf(argv[0], &src, &n) || (mcs_is_string(argv[0]) && ((src = (uint8_t*)mcs_cstr(argv[0])), (n = mcs_strlen(argv[0])), 1))) {
        if ((uint64_t)off + n > b->len) return mcs__range_fail(vm, "source length");
        memmove(b->data + off, src, n);
        return mcs_int((mcs_int_t)n);
    }
    int k = mcs_obj_kind(argv[0]);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "byte[], string or DmaBuffer expected"); return mcs_null(); }
    n = mcs_len(argv[0]);
    if ((uint64_t)off + n > b->len) return mcs__range_fail(vm, "source length");
    for (size_t i = 0; i < n; i++) {
        mcs_int_t x = mcs_to_int(vm, mcs_index(argv[0], (uint32_t)i));
        if (mcs_has_exception(vm)) return mcs_null();
        b->data[off + i] = (uint8_t)x;
    }
    return mcs_int((mcs_int_t)n);
}
NATIVE(buf_toarray) {     /* ToArray([offset[, count]]) -> byte[] */
    BUF();
    uint32_t off, cnt;
    if (!span(vm, argc, argv, 0, b->len, &off, &cnt)) return mcs_null();
    return mcs__bytes_val(vm, b->data + off, cnt);
}
NATIVE(buf_getstring) {   /* GetString([offset[, count]]) - the bytes as UTF-8 text */
    BUF();
    uint32_t off, cnt;
    if (!span(vm, argc, argv, 0, b->len, &off, &cnt)) return mcs_null();
    return mcs_string_n(vm, (const char*)b->data + off, cnt);
}
NATIVE(buf_tostring) {
    BUF();
    char s[40];
    snprintf(s, sizeof s, "DmaBuffer[%lu]", (unsigned long)b->len);
    return mcs_string(vm, s);
}
static const mcs_reg_t dmabuf_members[] = {
    MCS_GET("Length", buf_length), MCS_GET("Busy", buf_busy),
    MCS_FN("get_Item", buf_get, 1), MCS_FN("set_Item", buf_set, 2),
    MCS_FN("Fill", buf_fill, -1), MCS_FN("CopyFrom", buf_copyfrom, -1), MCS_FN("ToArray", buf_toarray, -1),
    MCS_FN("GetString", buf_getstring, -1), MCS_FN("ToString", buf_tostring, 0), MCS_REG_END
};
static const mcs_class_def_t dmabuf_def = { "DmaBuffer", sizeof(dmabuf_t), dmabuf_ctor, dmabuf_free, dmabuf_members, NULL };

/* ------------------------------------------------------------ transfers */
typedef struct {
    uint16_t id;
    int8_t slot;                      /* -1 once finished */
    uint8_t done;
    uint8_t kind;                     /* MCS__DMA_*: names the operation in errors */
    int32_t result;                   /* bytes, or MCS_HAL_E* */
} xfer_t;

static uint16_t g_seq;
static uint8_t g_closing;
static mcs_value_t refs(mcs_vm_t* vm) { return mcs_pinned(vm, ST()->cb_pin); }
static void set_busy(mcs_value_t v, int d) {
    dmabuf_t* b = (dmabuf_t*)mcs_userdata_of(v, &dmabuf_def);
    if (b) b->busy = (uint8_t)(b->busy + d);
}
static bool serial_kind(int k) { return k != MCS__DMA_MEM; }

/* finish slot i with `result`; runs the OnComplete callback. Returns 1 when one ran. */
static int finish(mcs_vm_t* vm, int i, int32_t result) {
    mcs__hal_state_t* s = ST();
    mcs__dma_slot_t* d = &s->dma[i];
    const mcs_hal_t* h = s->hal;
    if (d->cs >= 0 && h->gpio_write) h->gpio_write(h->ctx, d->cs, !d->cs_high);
    mcs_value_t list = refs(vm);
    mcs_value_t obj = mcs_index(list, (uint32_t)MCS__REF_DMA(i, 0));
    mcs_value_t cb = mcs_index(list, (uint32_t)MCS__REF_DMA(i, 3));
    xfer_t* x = (xfer_t*)mcs_userdata_of(obj, &xfer_def);
    if (x) { x->done = 1; x->result = result; x->slot = -1; }
    set_busy(mcs_index(list, (uint32_t)MCS__REF_DMA(i, 1)), -1);
    set_busy(mcs_index(list, (uint32_t)MCS__REF_DMA(i, 2)), -1);
    for (int k = 0; k < MCS__DMA_REFS; k++) mcs_set_index(list, (uint32_t)MCS__REF_DMA(i, k), mcs_null());
    memset(d, 0, sizeof *d);
    d->cs = -1;
    if (mcs_is_null(cb) || g_closing) return 0;
    mcs_push_root(vm, cb);
    mcs_value_t arg = mcs_int(result);
    int a = mcs_arity(cb);
    mcs_result_t r = mcs_call_value(vm, cb, a == 0 ? 0 : 1, &arg, NULL);
    mcs_pop_root(vm, 1);
    return r == MCS_OK ? 1 : -(int)r;
}
int mcs__dma_event(mcs_vm_t* vm, const mcs_hal_event_t* ev) {
    mcs__hal_state_t* s = ST();
    int i = ev->source & 7;
    if (!s || i >= MCS_HAL_MAX_DMA || s->dma[i].kind == MCS__DMA_FREE || s->dma[i].id != ev->source) return 0;
    if (s->dma[i].polled) return 0;               /* completed by dma_tick, not by an event */
    return finish(vm, i, ev->value);
}
/* transfers the library completes itself by polling (UART reads without uart_read_async) */
static void dma_tick(mcs_vm_t* vm) {
    mcs__hal_state_t* s = ST();
    if (!s) return;
    const mcs_hal_t* h = s->hal;
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) {
        mcs__dma_slot_t* d = &s->dma[i];
        if (d->kind != MCS__DMA_UART_RX || !d->polled) continue;
        uint8_t* p; size_t n;
        if (!mcs__dmabuf(mcs_index(refs(vm), (uint32_t)MCS__REF_DMA(i, 2)), &p, &n)) { finish(vm, i, MCS_HAL_ERR); continue; }
        while (d->done < n) {
            int rc = h->uart_read(h->ctx, d->bus, p + d->done, n - d->done, 0);
            if (rc == MCS_HAL_ETIMEOUT || rc == 0) break;
            if (rc < 0) { finish(vm, i, rc); break; }
            d->done += (uint32_t)rc;
        }
        if (d->kind && d->done >= n) finish(vm, i, (int32_t)n);
        if (mcs_has_exception(vm)) return;
    }
}
void mcs__dma_tick(mcs_vm_t* vm) { dma_tick(vm); }
static void pump(mcs_vm_t* vm) {
    mcs__hal_state_t* s = ST();
    mcs__hal_pump(vm, s->polling != 0);
    if (!mcs_has_exception(vm)) dma_tick(vm);
}
/* wait for `cond` (slot busy ...) with events running; false on timeout / abort / exception */
typedef bool (*wait_fn)(mcs_vm_t* vm, void* ud);
static bool wait_for(mcs_vm_t* vm, wait_fn done, void* ud, int32_t timeout_ms) {
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        if (done(vm, ud)) return true;
        pump(vm);
        if (mcs_has_exception(vm)) return false;
        if (done(vm, ud)) return true;
        uint32_t el = (uint32_t)(mcs_ticks(vm) - t0);
        if (timeout_ms >= 0 && el >= (uint32_t)timeout_ms) return false;
        if (mcs_safepoint(vm)) return false;
        if (el >= 2) { mcs_sleep(vm, 1); if (mcs_has_exception(vm)) return false; }
    }
}
typedef struct { int kind, bus; } busy_q_t;
static bool bus_free(mcs_vm_t* vm, void* ud) {
    busy_q_t* q = (busy_q_t*)ud;
    mcs__hal_state_t* s = ST();
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++)
        if (s->dma[i].kind && (q->kind < 0 || s->dma[i].kind == q->kind) && (q->bus < 0 || s->dma[i].bus == q->bus)) return false;
    return true;
}
bool mcs__dma_idle(mcs_vm_t* vm, int kind, int bus) {
    mcs__hal_state_t* s = ST();
    if (!s) return true;
    busy_q_t q = { kind, bus };
    if (bus_free(vm, &q)) return true;
    if (wait_for(vm, bus_free, &q, 10000)) return true;
    if (!mcs_has_exception(vm)) mcs_raise(vm, "TimeoutException", "an earlier DMA transfer on this bus did not finish");
    return false;
}
static bool slot_free(mcs_vm_t* vm, void* ud) {
    mcs__hal_state_t* s = ST();
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) if (!s->dma[i].kind) { *(int*)ud = i; return true; }
    return false;
}

/* one transfer to start */
typedef struct {
    int kind, bus, addr, cs, cs_high, polled;
    const uint8_t* tx; size_t tn;
    uint8_t* rx; size_t rn;
    mcs_qspi_cmd_t cmd;
    unsigned flags;
    uint8_t fill;
} job_t;

static int run_sync(const mcs_hal_t* h, job_t* j) {
    int rc;
    switch (j->kind) {
    case MCS__DMA_SPI: {
        if (j->cs >= 0 && (rc = h->gpio_write(h->ctx, j->cs, j->cs_high)) < 0) return rc;
        rc = mcs__spi_raw(h, j->bus, j->tx, j->rx, j->tn ? j->tn : j->rn);
        if (j->cs >= 0) h->gpio_write(h->ctx, j->cs, !j->cs_high);
        return rc < 0 ? rc : (int)(j->tn ? j->tn : j->rn);
    }
    case MCS__DMA_I2C:
        if (j->tn && j->rn) {
            if (h->i2c_write_read) rc = h->i2c_write_read(h->ctx, j->bus, j->addr, j->tx, j->tn, j->rx, j->rn);
            else if (!h->i2c_read) return MCS_HAL_ENOTSUP;
            else { rc = h->i2c_write(h->ctx, j->bus, j->addr, j->tx, j->tn); if (rc >= 0) rc = h->i2c_read(h->ctx, j->bus, j->addr, j->rx, j->rn); }
        } else if (j->rn) rc = h->i2c_read ? h->i2c_read(h->ctx, j->bus, j->addr, j->rx, j->rn) : MCS_HAL_ENOTSUP;
        else rc = h->i2c_write(h->ctx, j->bus, j->addr, j->tx, j->tn);
        return rc < 0 ? rc : (int)(j->tn + j->rn);
    case MCS__DMA_UART_TX: return h->uart_write(h->ctx, j->bus, j->tx, j->tn);
    case MCS__DMA_QSPI:
        rc = h->qspi_command(h->ctx, j->bus, &j->cmd, j->tx, j->rx, j->tn ? j->tn : j->rn);
        return rc < 0 ? rc : (int)(j->tn ? j->tn : j->rn);
    case MCS__DMA_MEM:
        if (j->flags & MCS_DMA_SRC_FIXED) memset(j->rx, j->fill, j->rn);
        else memmove(j->rx, j->tx, j->rn);
        return (int)j->rn;
    default: return MCS_HAL_ENOTSUP;
    }
}
static int run_async(const mcs_hal_t* h, job_t* j, int id, const uint8_t* fillp) {
    switch (j->kind) {
    case MCS__DMA_SPI: return h->spi_transfer_async ? h->spi_transfer_async(h->ctx, j->bus, j->tx, j->rx, j->tn ? j->tn : j->rn, id) : MCS_HAL_ENOTSUP;
    case MCS__DMA_I2C: return h->i2c_transfer_async ? h->i2c_transfer_async(h->ctx, j->bus, j->addr, j->tx, j->tn, j->rx, j->rn, id) : MCS_HAL_ENOTSUP;
    case MCS__DMA_UART_TX: return h->uart_write_async ? h->uart_write_async(h->ctx, j->bus, j->tx, j->tn, id) : MCS_HAL_ENOTSUP;
    case MCS__DMA_UART_RX: return h->uart_read_async ? h->uart_read_async(h->ctx, j->bus, j->rx, j->rn, id) : MCS_HAL_ENOTSUP;
    case MCS__DMA_QSPI: return h->qspi_command_async ? h->qspi_command_async(h->ctx, j->bus, &j->cmd, j->tx, j->rx, j->tn ? j->tn : j->rn, id) : MCS_HAL_ENOTSUP;
    case MCS__DMA_MEM: return h->dma_copy ? h->dma_copy(h->ctx, j->rx, (j->flags & MCS_DMA_SRC_FIXED) ? fillp : j->tx, j->rn, j->flags, id) : MCS_HAL_ENOTSUP;
    default: return MCS_HAL_ENOTSUP;
    }
}
static bool has_async(const mcs_hal_t* h, int kind) {
    switch (kind) {
    case MCS__DMA_SPI: return h->spi_transfer_async != NULL;
    case MCS__DMA_I2C: return h->i2c_transfer_async != NULL;
    case MCS__DMA_UART_TX: return h->uart_write_async != NULL;
    case MCS__DMA_UART_RX: return h->uart_read_async != NULL;
    case MCS__DMA_QSPI: return h->qspi_command_async != NULL;
    case MCS__DMA_MEM: return h->dma_copy != NULL;
    default: return false;
    }
}
static const char* op_name(int kind) {
    switch (kind) {
    case 0: return "DmaTransfer";
    case MCS__DMA_SPI: return "SPI (DMA)";
    case MCS__DMA_I2C: return "I2C (DMA)";
    case MCS__DMA_UART_TX: return "UART.WriteAsync";
    case MCS__DMA_UART_RX: return "UART.ReadAsync";
    case MCS__DMA_QSPI: return "QSPI (DMA)";
    case MCS__DMA_MEM: return "Dma.Copy";
    default: return "DmaTransfer";
    }
}
/* start job `j` keeping buffers a / b alive; returns the DmaTransfer */
static mcs_value_t start(mcs_vm_t* vm, job_t* j, mcs_value_t a, mcs_value_t b) {
    mcs__hal_state_t* s = ST();
    const mcs_hal_t* h = s->hal;
    if (serial_kind(j->kind) && !mcs__dma_idle(vm, j->kind, j->bus)) return mcs_null();
    int slot = -1;
    if (!slot_free(vm, &slot) && !wait_for(vm, slot_free, &slot, 10000)) {
        if (!mcs_has_exception(vm)) mcs_raise(vm, "InvalidOperationException", "too many DMA transfers in flight (MCS_HAL_MAX_DMA = %d)", MCS_HAL_MAX_DMA);
        return mcs_null();
    }
    mcs_push_root(vm, a); mcs_push_root(vm, b);
    mcs_value_t obj;
    if (mcs_new_object(vm, "DmaTransfer", 0, NULL, &obj) != MCS_OK) { mcs_pop_root(vm, 2); return mcs_null(); }
    mcs_pop_root(vm, 2);
    xfer_t* x = (xfer_t*)mcs_userdata(obj);
    g_seq = (uint16_t)((g_seq + 1) & 0x1FFF);
    if (!g_seq) g_seq = 1;
    uint16_t id = (uint16_t)((g_seq << 3) | (unsigned)slot);
    x->id = id; x->slot = (int8_t)slot; x->done = 0; x->kind = (uint8_t)j->kind;
    mcs__dma_slot_t* d = &s->dma[slot];
    d->kind = (uint8_t)j->kind; d->bus = (int16_t)j->bus; d->cs = (int16_t)j->cs; d->cs_high = (uint8_t)(j->cs_high != 0);
    d->id = id; d->done = 0; d->fill = j->fill; d->polled = 0;
    mcs_value_t list = refs(vm);
    mcs_set_index(list, (uint32_t)MCS__REF_DMA(slot, 0), obj);
    mcs_set_index(list, (uint32_t)MCS__REF_DMA(slot, 1), a);
    mcs_set_index(list, (uint32_t)MCS__REF_DMA(slot, 2), b);
    set_busy(a, 1); set_busy(b, 1);
    int rc;
    if (!s->no_events && has_async(h, j->kind)) {
        if (j->cs >= 0) {
            rc = h->gpio_write ? h->gpio_write(h->ctx, j->cs, j->cs_high) : MCS_HAL_ENOTSUP;
            if (rc < 0) { d->cs = -1; finish(vm, slot, rc); return mcs__hal_fail(vm, op_name(j->kind), rc); }
        }
        rc = run_async(h, j, id, &d->fill);
        if (rc >= 0) return obj;                     /* MCS_HAL_EV_DMA finishes it */
        if (rc != MCS_HAL_ENOTSUP) { finish(vm, slot, rc); return mcs__hal_fail(vm, op_name(j->kind), rc); }
        /* the port declined this one (fill without a DMA engine ...): run it synchronously */
        if (j->cs >= 0) h->gpio_write(h->ctx, j->cs, !j->cs_high);
    }
    if (j->kind == MCS__DMA_UART_RX) { d->polled = 1; d->cs = -1; dma_tick(vm); return obj; }   /* completes from Poll */
    d->cs = -1;                                      /* run_sync drives chip select itself */
    rc = run_sync(h, j);
    finish(vm, slot, rc);
    if (rc < 0) return mcs__hal_fail(vm, op_name(j->kind), rc);
    return obj;
}

/* a DmaBuffer argument (rx), or a byte[] / string copied into a new one (tx) */
static bool buf_arg(mcs_vm_t* vm, mcs_value_t* v, uint8_t** p, size_t* n, bool may_copy, const char* what) {
    if (mcs__dmabuf(*v, p, n)) return true;
    if (!may_copy) { mcs_raise(vm, "ArgumentException", "%s must be a DmaBuffer", what); return false; }
    mcs_value_t nb;
    if (mcs_new_object(vm, "DmaBuffer", 1, v, &nb) != MCS_OK) return false;
    *v = nb;
    return mcs__dmabuf(nb, p, n);
}
static int cs_arg(mcs_vm_t* vm, int argc, mcs_value_t* argv, int i) {
    return argc > i && !mcs_is_null(argv[i]) ? mcs__pin_arg(vm, argv[i]) : -1;
}

mcs_value_t mcs__spi_async(mcs_vm_t* vm, int bus, int cs, int cs_high, mcs_value_t tx, mcs_value_t rx) {
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_SPI; j.bus = bus; j.cs = cs; j.cs_high = cs_high;
    mcs__hal_state_t* s = ST();
    if (cs == -1 && bus >= 0 && bus < MCS_HAL_MAX_BUSES && s->spi_cs[bus] >= 0) { j.cs = s->spi_cs[bus]; j.cs_high = s->spi_cs_high[bus]; }
    if (j.cs < 0) j.cs = -1;
    uint8_t* p; size_t n;
    mcs_push_root(vm, tx); mcs_push_root(vm, rx);
    if (!mcs_is_null(tx)) { if (!buf_arg(vm, &tx, &p, &n, true, "tx")) goto fail; j.tx = p; j.tn = n; }
    if (!mcs_is_null(rx)) { if (!buf_arg(vm, &rx, &p, &n, false, "rx")) goto fail; j.rx = p; j.rn = n; }
    if (j.tx && j.rx && j.tn != j.rn) { mcs_raise(vm, "ArgumentException", "tx and rx lengths differ (%d / %d)", (int)j.tn, (int)j.rn); goto fail; }
    if (!j.tx && !j.rx) { mcs_raise(vm, "ArgumentException", "nothing to transfer"); goto fail; }
    if (j.tx && j.rx) j.rn = 0;      /* length from tn */
    else if (j.rx) { j.rn = n; }
    {
        mcs_value_t r = start(vm, &j, tx, rx);
        mcs_pop_root(vm, 2);
        return r;
    }
fail:
    mcs_pop_root(vm, 2);
    return mcs_null();
}
mcs_value_t mcs__i2c_async(mcs_vm_t* vm, int bus, int addr, mcs_value_t tx, mcs_value_t rx) {
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_I2C; j.bus = bus; j.addr = addr; j.cs = -1;
    if (addr < 0 || addr > 0x3FF) return mcs__range_fail(vm, "I2C address");
    uint8_t* p; size_t n;
    mcs_push_root(vm, tx); mcs_push_root(vm, rx);
    if (!mcs_is_null(tx)) { if (!buf_arg(vm, &tx, &p, &n, true, "data")) goto fail; j.tx = p; j.tn = n; }
    if (!mcs_is_null(rx)) { if (!buf_arg(vm, &rx, &p, &n, false, "rx")) goto fail; j.rx = p; j.rn = n; }
    {
        mcs_value_t r = start(vm, &j, tx, rx);
        mcs_pop_root(vm, 2);
        return r;
    }
fail:
    mcs_pop_root(vm, 2);
    return mcs_null();
}

/* SPI.TransferAsync(bus, tx, rx[, cs]) / WriteAsync(bus, data[, cs]) / ReadAsync(bus, rx[, cs]) */
NATIVE(spi_transfer_async) {
    INT_ARG(0, bus);
    if (argc < 3 || argc > 4) { mcs_raise(vm, "ArgumentException", "SPI.TransferAsync(bus, tx, rx[, cs]) expected"); return mcs_null(); }
    int cs = cs_arg(vm, argc, argv, 3);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs__spi_async(vm, bus, cs, 0, argv[1], argv[2]);
}
NATIVE(spi_write_async) {
    INT_ARG(0, bus);
    int cs = cs_arg(vm, argc, argv, 2);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs__spi_async(vm, bus, cs, 0, argv[1], mcs_null());
}
NATIVE(spi_read_async) {
    INT_ARG(0, bus);
    int cs = cs_arg(vm, argc, argv, 2);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs__spi_async(vm, bus, cs, 0, mcs_null(), argv[1]);
}
static const mcs_reg_t spi_async_fns[] = {
    MCS_FN("TransferAsync", spi_transfer_async, -1), MCS_FN("WriteAsync", spi_write_async, -1),
    MCS_FN("ReadAsync", spi_read_async, -1), MCS_REG_END
};
/* I2C.WriteAsync(bus, addr, data) / ReadAsync(bus, addr, rx) / WriteReadAsync(bus, addr, tx, rx) */
NATIVE(i2c_write_async) { INT_ARG(0, bus); INT_ARG(1, addr); return mcs__i2c_async(vm, bus, addr, argv[2], mcs_null()); }
NATIVE(i2c_read_async) { INT_ARG(0, bus); INT_ARG(1, addr); return mcs__i2c_async(vm, bus, addr, mcs_null(), argv[2]); }
NATIVE(i2c_write_read_async) { INT_ARG(0, bus); INT_ARG(1, addr); return mcs__i2c_async(vm, bus, addr, argv[2], argv[3]); }
static const mcs_reg_t i2c_async_fns[] = {
    MCS_FN("WriteAsync", i2c_write_async, 3), MCS_FN("ReadAsync", i2c_read_async, 3),
    MCS_FN("WriteReadAsync", i2c_write_read_async, 4), MCS_REG_END
};
/* UART.WriteAsync(port, data) / ReadAsync(port, rx): completes when rx is full */
NATIVE(uart_write_async) {
    INT_ARG(0, port);
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_UART_TX; j.bus = port; j.cs = -1;
    mcs_value_t tx = argv[1];
    uint8_t* p; size_t n;
    if (!buf_arg(vm, &tx, &p, &n, true, "data")) return mcs_null();
    j.tx = p; j.tn = n;
    mcs_push_root(vm, tx);
    mcs_value_t r = start(vm, &j, tx, mcs_null());
    mcs_pop_root(vm, 1);
    return r;
}
NATIVE(uart_read_async) {
    INT_ARG(0, port);
    if (!HAL()->uart_read) return mcs__hal_fail(vm, "UART.ReadAsync", MCS_HAL_ENOTSUP);
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_UART_RX; j.bus = port; j.cs = -1;
    mcs_value_t rx = argv[1];
    uint8_t* p; size_t n;
    if (!buf_arg(vm, &rx, &p, &n, false, "rx")) return mcs_null();
    j.rx = p; j.rn = n;
    return start(vm, &j, mcs_null(), rx);
}
static const mcs_reg_t uart_async_fns[] = { MCS_FN("WriteAsync", uart_write_async, 2), MCS_FN("ReadAsync", uart_read_async, 2), MCS_REG_END };
/* QSPI.ReadAsync(bus, instr, address, rx[, dummy[, lines[, addrBytes]]]) / WriteAsync(bus, instr, address, data[, lines[, addrBytes]]) */
static bool lines_ok(int l) { return l == 1 || l == 2 || l == 4 || l == 8; }
static mcs_value_t qspi_async(mcs_vm_t* vm, int argc, mcs_value_t* argv, bool rd) {
    INT_ARG(0, bus); INT_ARG(1, instr); INT_ARG(2, addr);
    int dummy = 0, li = rd ? 5 : 4;
    if (rd) { OPT_INT(4, dm, 0); dummy = dm; }
    OPT_INT(li, lines, 1); OPT_INT(li + 1, ab, 3);
    if (ab < 1 || ab > 4 || !lines_ok(lines) || dummy < 0 || dummy > 255) return mcs__range_fail(vm, "QSPI phase");
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_QSPI; j.bus = bus; j.cs = -1;
    j.cmd.instruction = (uint8_t)instr; j.cmd.instr_lines = 1;
    if (addr >= 0) {
        j.cmd.address = (uint32_t)addr; j.cmd.addr_bytes = (uint8_t)ab;
        j.cmd.addr_lines = lines == 4 && (instr == 0xEB || instr == 0xBB || instr == 0x38) ? (uint8_t)lines : 1;
    }
    j.cmd.dummy_cycles = (uint8_t)dummy; j.cmd.data_lines = (uint8_t)lines;
    mcs_value_t v = argv[3];
    uint8_t* p; size_t n;
    if (!buf_arg(vm, &v, &p, &n, !rd, rd ? "rx" : "data")) return mcs_null();
    if (rd) { j.rx = p; j.rn = n; } else { j.tx = p; j.tn = n; }
    mcs_push_root(vm, v);
    mcs_value_t r = start(vm, &j, rd ? mcs_null() : v, rd ? v : mcs_null());
    mcs_pop_root(vm, 1);
    return r;
}
NATIVE(qspi_read_async) { return qspi_async(vm, argc, argv, true); }
NATIVE(qspi_write_async) { return qspi_async(vm, argc, argv, false); }
static const mcs_reg_t qspi_async_fns[] = { MCS_FN("ReadAsync", qspi_read_async, -1), MCS_FN("WriteAsync", qspi_write_async, -1), MCS_REG_END };

/* Dma.Copy(dst, src[, count[, dstOffset[, srcOffset]]]) and friends */
static mcs_value_t copy_common(mcs_vm_t* vm, int argc, mcs_value_t* argv, bool async) {
    uint8_t *dp, *sp; size_t dn, sn;
    if (!mcs__dmabuf(argv[0], &dp, &dn)) { mcs_raise(vm, "ArgumentException", "destination must be a DmaBuffer"); return mcs_null(); }
    mcs_value_t src = argv[1];
    mcs_push_root(vm, src);
    if (!buf_arg(vm, &src, &sp, &sn, true, "source")) { mcs_pop_root(vm, 1); return mcs_null(); }
    mcs_pop_root(vm, 1);
    mcs_int_t doff = argc > 3 ? mcs_to_int(vm, argv[3]) : 0, soff = argc > 4 ? mcs_to_int(vm, argv[4]) : 0;
    if (mcs_has_exception(vm)) return mcs_null();
    if (doff < 0 || (uint64_t)doff > dn || soff < 0 || (uint64_t)soff > sn) return mcs__range_fail(vm, "offset");
    mcs_int_t cnt = argc > 2 && !mcs_is_null(argv[2]) ? mcs_to_int(vm, argv[2])
                  : (mcs_int_t)((sn - (size_t)soff) < (dn - (size_t)doff) ? sn - (size_t)soff : dn - (size_t)doff);
    if (mcs_has_exception(vm)) return mcs_null();
    if (cnt < 0 || (uint64_t)doff + (uint64_t)cnt > dn || (uint64_t)soff + (uint64_t)cnt > sn) return mcs__range_fail(vm, "count");
    dmabuf_t* db = (dmabuf_t*)mcs_userdata_of(argv[0], &dmabuf_def);
    if (!writable(vm, db)) return mcs_null();
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_MEM; j.bus = -1; j.cs = -1;
    j.rx = dp + doff; j.rn = (size_t)cnt; j.tx = sp + soff;
    mcs_push_root(vm, src);
    mcs_value_t r = start(vm, &j, argv[0], src);
    mcs_pop_root(vm, 1);
    if (async || mcs_is_null(r)) return r;
    xfer_t* x = (xfer_t*)mcs_userdata(r);
    return x && x->done ? mcs_int(x->result) : mcs_null();
}
static mcs_value_t fill_common(mcs_vm_t* vm, int argc, mcs_value_t* argv, bool async) {
    uint8_t* dp; size_t dn;
    if (!mcs__dmabuf(argv[0], &dp, &dn)) { mcs_raise(vm, "ArgumentException", "destination must be a DmaBuffer"); return mcs_null(); }
    mcs_int_t v = mcs_to_int(vm, argv[1]);
    uint32_t off = 0, cnt = (uint32_t)dn;
    if (mcs_has_exception(vm) || !span(vm, argc, argv, 2, (uint32_t)dn, &off, &cnt)) return mcs_null();
    dmabuf_t* db = (dmabuf_t*)mcs_userdata_of(argv[0], &dmabuf_def);
    if (!writable(vm, db)) return mcs_null();
    job_t j; memset(&j, 0, sizeof j);
    j.kind = MCS__DMA_MEM; j.bus = -1; j.cs = -1; j.flags = MCS_DMA_SRC_FIXED; j.fill = (uint8_t)v;
    j.rx = dp + off; j.rn = cnt;
    mcs_value_t r = start(vm, &j, argv[0], mcs_null());
    if (async || mcs_is_null(r)) return r;
    xfer_t* x = (xfer_t*)mcs_userdata(r);
    return x && x->done ? mcs_null() : mcs_null();
}
static bool xfer_done(mcs_vm_t* vm, void* ud) { return ((xfer_t*)ud)->done != 0; }
NATIVE(dma_copy) {
    mcs_value_t r = copy_common(vm, argc, argv, true);
    xfer_t* x = (xfer_t*)mcs_userdata_of(r, &xfer_def);
    if (!x) return mcs_null();
    mcs_push_root(vm, r);
    bool ok = wait_for(vm, xfer_done, x, -1);
    mcs_pop_root(vm, 1);
    if (!ok) return mcs_null();
    if (x->result < 0) return mcs__hal_fail(vm, "Dma.Copy", x->result);
    return mcs_int(x->result);
}
NATIVE(dma_copy_async) { return copy_common(vm, argc, argv, true); }
NATIVE(dma_fill) {
    mcs_value_t r = fill_common(vm, argc, argv, true);
    xfer_t* x = (xfer_t*)mcs_userdata_of(r, &xfer_def);
    if (!x) return mcs_null();
    mcs_push_root(vm, r);
    bool ok = wait_for(vm, xfer_done, x, -1);
    mcs_pop_root(vm, 1);
    if (ok && x->result < 0) return mcs__hal_fail(vm, "Dma.Fill", x->result);
    return mcs_null();
}
NATIVE(dma_fill_async) { return fill_common(vm, argc, argv, true); }
static bool all_done(mcs_vm_t* vm, void* ud) { busy_q_t q = { -1, -1 }; return bus_free(vm, &q); }
NATIVE(dma_wait_all) {
    OPT_INT(0, ms, -1);
    return mcs_bool(wait_for(vm, all_done, NULL, ms));
}
NATIVE(dma_pending) {
    int n = 0;
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) n += ST()->dma[i].kind != 0;
    return mcs_int(n);
}
NATIVE(dma_available) { const mcs_hal_t* h = HAL(); return mcs_bool(h->dma_channels > 0 || h->dma_copy != NULL); }
NATIVE(dma_channels) { return mcs_int(HAL()->dma_channels); }
NATIVE(dma_alignment) { return mcs_int(HAL()->dma_align ? HAL()->dma_align : 4); }
NATIVE(dma_max) { return mcs_int(MCS_HAL_MAX_DMA); }
static const mcs_reg_t dma_fns[] = {
    MCS_FN("Copy", dma_copy, -1), MCS_FN("CopyAsync", dma_copy_async, -1), MCS_FN("Fill", dma_fill, -1),
    MCS_FN("FillAsync", dma_fill_async, -1), MCS_FN("WaitAll", dma_wait_all, -1),
    MCS_GET("Pending", dma_pending), MCS_GET("Available", dma_available), MCS_GET("Channels", dma_channels),
    MCS_GET("Alignment", dma_alignment), MCS_GET("MaxTransfers", dma_max), MCS_REG_END
};

/* ------------------------------------------------------------ DmaTransfer */
#define XF() xfer_t* x = (xfer_t*)mcs_check_userdata(vm, self, &xfer_def); if (!x) return mcs_null()
static void xfer_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    xfer_t* x = (xfer_t*)mcs_userdata(self);
    x->slot = -1; x->done = 1; x->result = 0;    /* new DmaTransfer() from a script: an already completed no-op */
}
NATIVE(xf_completed) {
    XF();
    if (!x->done) { pump(vm); }
    return mcs_bool(x->done != 0);
}
NATIVE(xf_succeeded) { XF(); return mcs_bool(x->done && x->result >= 0); }
NATIVE(xf_error) {
    XF();
    if (!x->done || x->result >= 0) return mcs_null();
    const char* why = x->result == MCS_HAL_ENOTSUP ? "not supported" : x->result == MCS_HAL_ETIMEOUT ? "timeout"
                    : x->result == MCS_HAL_ENODEV ? "no device" : x->result == MCS_HAL_EBUSY ? "busy"
                    : x->result == MCS_HAL_EINVAL ? "invalid argument" : "failed";
    char s[48];
    snprintf(s, sizeof s, "%s (%d)", why, (int)x->result);
    return mcs_string(vm, s);
}
/* Wait([timeoutMs]): true when finished (raises IOException ... if it failed), false on timeout */
NATIVE(xf_wait) {
    XF(); OPT_INT(0, ms, -1);
    if (!wait_for(vm, xfer_done, x, ms)) return mcs_has_exception(vm) ? mcs_null() : mcs_bool(false);
    if (x->result < 0) return mcs__hal_fail(vm, op_name(x->kind), x->result);
    return mcs_bool(true);
}
/* Result: bytes transferred (waits for the end) */
NATIVE(xf_result) {
    XF();
    if (!wait_for(vm, xfer_done, x, -1)) return mcs_null();
    if (x->result < 0) return mcs__hal_fail(vm, op_name(x->kind), x->result);
    return mcs_int(x->result);
}
/* OnComplete(bytes => ...) - runs on the VM thread when the transfer ends (at once if it has) */
NATIVE(xf_oncomplete) {
    XF();
    if (!mcs__check_callable(vm, argv[0])) return mcs_null();
    if (x->done) {
        if (mcs_is_null(argv[0])) return self;
        mcs_value_t arg = mcs_int(x->result);
        int a = mcs_arity(argv[0]);
        mcs_call_value(vm, argv[0], a == 0 ? 0 : 1, &arg, NULL);
        return self;
    }
    mcs_set_index(refs(vm), (uint32_t)MCS__REF_DMA(x->slot, 3), argv[0]);
    return self;
}
/* Cancel(): stop a transfer in flight (best effort); true when it was stopped */
NATIVE(xf_cancel) {
    XF();
    if (x->done) return mcs_bool(false);
    const mcs_hal_t* h = HAL();
    mcs__hal_state_t* s = ST();
    int i = x->slot;
    if (!s->dma[i].polled) {
        if (!h->dma_abort || h->dma_abort(h->ctx, x->id) < 0) return mcs_bool(false);
    }
    finish(vm, i, MCS_HAL_ERR);
    return mcs_bool(true);
}
NATIVE(xf_id) { XF(); return mcs_int(x->id); }
static const mcs_reg_t xfer_members[] = {
    MCS_GET("IsCompleted", xf_completed), MCS_GET("Succeeded", xf_succeeded), MCS_GET("Error", xf_error),
    MCS_GET("Result", xf_result), MCS_GET("Id", xf_id),
    MCS_FN("Wait", xf_wait, -1), MCS_FN("OnComplete", xf_oncomplete, 1), MCS_FN("Cancel", xf_cancel, 0), MCS_REG_END
};
static const mcs_class_def_t xfer_def = { "DmaTransfer", sizeof(xfer_t), xfer_ctor, NULL, xfer_members, NULL };

/* ------------------------------------------------------------ setup */
void mcs__dma_close(mcs_vm_t* vm) {
    mcs__hal_state_t* s = ST();
    if (!s) return;
    g_closing = 1;
    busy_q_t q = { -1, -1 };
    uint32_t t0 = mcs_ticks(vm);
    while (!bus_free(vm, &q) && (uint32_t)(mcs_ticks(vm) - t0) < 100) {   /* let short transfers end */
        mcs__hal_pump(vm, true);
        dma_tick(vm);
    }
    const mcs_hal_t* h = s->hal;
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) {
        if (!s->dma[i].kind) continue;
        if (!s->dma[i].polled && h->dma_abort) h->dma_abort(h->ctx, s->dma[i].id);
        finish(vm, i, MCS_HAL_ERR);
    }
    g_closing = 0;
}
void mcs__dma_open(mcs_vm_t* vm, const mcs_hal_t* h) {
    mcs__hal_state_t* s = ST();
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) if (!s->dma[i].kind) s->dma[i].cs = -1;
    mcs_register_class(vm, &dmabuf_def);
    mcs_register_class(vm, &xfer_def);
    mcs_register_module(vm, "Dma", dma_fns);
    if (h->spi_transfer) mcs_register_module(vm, "SPI", spi_async_fns);
    if (h->i2c_write) mcs_register_module(vm, "I2C", i2c_async_fns);
    if (h->uart_write) mcs_register_module(vm, "UART", uart_async_fns);
    if (h->qspi_command) mcs_register_module(vm, "QSPI", qspi_async_fns);
}
#endif
