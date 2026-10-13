/* Optional actual TFT_eSPI adapter, not a mock. OFF requires no dependency.
 * Synchronous only; no DMA or retained native pixel/string pointers.
 * The firmware owns TFT_eSPI's compile-time setup; never rewrite it here. */
#include "mcs_tft.h"
#if MCS_ENABLE_TFT_ESPI
#include <TFT_eSPI.h>
#include "mcs_tft_sprite_budget.h"
#include "mcs_tft_sprite_native.h"
#include <new>
#include <string.h>
#if MCS_ENABLE_LVGL
#include "mcs_lvgl.h"
#include "mcs_display_rgb565.h"
#endif
#if MCS_ENABLE_THREADS
#error "MicroCS TFT_eSPI WIP requires a single-threaded owner VM"
#endif
#if MCS_TFT_SPRITE_COUNT < 1 || MCS_TFT_SPRITE_COUNT > 128
#error "MCS_TFT_SPRITE_COUNT must be 1..128"
#endif
#if MCS_TFT_SPRITE_BYTES < 1 || MCS_TFT_SPRITE_BYTES > 16777216
#error "MCS_TFT_SPRITE_BYTES must be 1..16777216"
#endif
namespace {
struct sprite_t;
struct tft_t {
    TFT_eSPI *display;
    mcs_vm_t *vm;
    uint64_t serial;
    sprite_t *sprites;
    bool begun, calibrated;
    int sprite_pin;
    unsigned sprite_count;
    size_t sprite_bytes;
#if MCS_ENABLE_LVGL
    int pin;
    mcs_lvgl_backend_t backend;
#endif
};
struct sprite_t {
    mcs_tft_safe_sprite *display;
    tft_t *owner;
    sprite_t *previous, *next;
    size_t bytes;
};
static tft_t *active_tft;
static uint64_t owner_serial;
static void sprite_release(mcs_vm_t *vm, sprite_t *s);
extern const mcs_class_def_t sprite_def;
extern const mcs_class_def_t tft_def;
#define NATIVE(n) static mcs_value_t n(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv)
static tft_t *checked(mcs_vm_t *vm, mcs_value_t self, bool begun = true) {
    tft_t *d = (tft_t *)mcs_check_userdata(vm, self, &tft_def);
    if (!d)
        return NULL;
    if (!d->display) {
        mcs_raise(vm, "ObjectDisposedException", "Tft");
        return NULL;
    }
    if (begun && !d->begun) {
        mcs_raise(vm, "InvalidOperationException", "Call Tft.Begin first");
        return NULL;
    }
    return d;
}
static sprite_t *sprite_checked(mcs_vm_t *vm, mcs_value_t self) {
    sprite_t *s = (sprite_t *)mcs_check_userdata(vm, self, &sprite_def);
    if (s && (!s->display || !s->owner || !s->owner->display)) {
        mcs_raise(vm, "ObjectDisposedException", "TftSprite");
        return NULL;
    }
    return s;
}
/* Only operations with upstream virtual drawing dispatch are shared. Never call
 * a physical TFT-only operation (init/rotation/pushImage) via this base pointer. */
static TFT_eSPI *drawing_checked(mcs_vm_t *vm, mcs_value_t self) {
    if (mcs_userdata_of(self, &sprite_def)) {
        sprite_t *s = sprite_checked(vm, self);
        return s ? s->display : NULL;
    }
    tft_t *d = checked(vm, self);
    return d ? d->display : NULL;
}
#define DRAW_CHECK()                                                                               \
    TFT_eSPI *display = drawing_checked(vm, self);                                                 \
    if (!display)                                                                                  \
        return mcs_null();                                                                         \
    (void)argc;                                                                                    \
    (void)argv
static bool integer(mcs_vm_t *vm, mcs_value_t value, int32_t low, int32_t high, int32_t *out) {
    if (!mcs_is_int(value)) {
        mcs_raise(vm, "ArgumentException", "Tft expects an integer");
        return false;
    }
    mcs_int_t n = value.as.i;
    if (n < low || n > high) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "Tft integer range %ld..%ld", (long)low,
                  (long)high);
        return false;
    }
    *out = (int32_t)n;
    return true;
}
/* Low-depth upstream sprites consume indices, not silently narrowed RGB565. */
static bool color_value(mcs_vm_t *vm, mcs_value_t self, mcs_value_t value, int32_t *out) {
    sprite_t *s = (sprite_t *)mcs_userdata_of(self, &sprite_def);
    int32_t maximum = 65535;
    if (s && s->display) {
        int depth = s->display->getColorDepth();
        if (depth == 1)
            maximum = 1;
        else if (depth == 4)
            maximum = 15;
    }
    return integer(vm, value, 0, maximum, out);
}
#define TFT_CHECK()                                                                                \
    tft_t *d = checked(vm, self);                                                                  \
    if (!d)                                                                                        \
        return mcs_null();                                                                         \
    (void)argc;                                                                                    \
    (void)argv
static void ctor(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)argv;
    tft_t *d = (tft_t *)mcs_userdata(self);
    d->sprite_pin = -1;
#if MCS_ENABLE_LVGL
    d->pin = -1;
#endif
    if (argc) {
        mcs_raise(vm, "ArgumentException", "new Tft() uses the firmware's explicit TFT setup");
        return;
    }
    if (active_tft) {
        mcs_raise(vm, "InvalidOperationException", "One MicroCS-owned TFT can be live");
        return;
    }
    if (owner_serial == UINT64_MAX) {
        mcs_raise(vm, "InvalidOperationException", "TFT ownership generation exhausted");
        return;
    }
    void *memory = mcs_mem_realloc(vm, NULL, 0, sizeof(TFT_eSPI));
    d->display = new (memory) TFT_eSPI();
    d->vm = vm;
    d->serial = ++owner_serial;
    active_tft = d;
}
static void release(mcs_vm_t *vm, tft_t *d) {
    while (d->sprites)
        sprite_release(vm, d->sprites);
    if (!d->display)
        return;
#if MCS_ENABLE_LVGL
    if (d->pin >= 0) {
        mcs_lvgl_set_backend(vm, NULL);
        mcs_unpin(vm, d->pin);
        d->pin = -1;
    }
#endif
    if (d->begun)
        d->display->endWrite();
    d->display->~TFT_eSPI();
    mcs_mem_realloc(vm, d->display, sizeof(TFT_eSPI), 0);
    d->display = NULL;
    d->begun = false;
    if (active_tft == d)
        active_tft = NULL;
}
static void finalizer(mcs_vm_t *vm, void *data) {
    release(vm, (tft_t *)data);
}
NATIVE(tft_dispose) {
    (void)argc;
    (void)argv;
    tft_t *d = (tft_t *)mcs_check_userdata(vm, self, &tft_def);
    if (!d || !d->display)
        return mcs_null();
    if (d->sprite_count) {
        mcs_raise(vm, "InvalidOperationException", "Dispose TftSprite children before Tft");
        return mcs_null();
    }
#if MCS_ENABLE_LVGL
    if (d->pin >= 0 && mcs_lvgl_set_backend(vm, NULL)) {
        mcs_raise(vm, "InvalidOperationException",
                  "Dispose LvGui before detaching/disposing its TFT");
        return mcs_null();
    }
#endif
    release(vm, d);
    return mcs_null();
}
NATIVE(tft_begin) {
    (void)argc;
    (void)argv;
    tft_t *d = checked(vm, self, false);
    if (d && !d->begun) {
        d->display->init();
        d->begun = true;
    }
    return mcs_null();
}
NATIVE(tft_width) {
    DRAW_CHECK();
    return mcs_int(display->width());
}
NATIVE(tft_height) {
    DRAW_CHECK();
    return mcs_int(display->height());
}
NATIVE(tft_rotation_get) {
    TFT_CHECK();
    return mcs_int(d->display->getRotation());
}
NATIVE(tft_rotation_set) {
    TFT_CHECK();
    int32_t n;
    if (!integer(vm, argv[0], 0, 3, &n))
        return mcs_null();
#if MCS_ENABLE_LVGL
    if (d->pin >= 0) {
        mcs_raise(vm, "InvalidOperationException", "Detach LVGL before changing display rotation");
        return mcs_null();
    }
#endif
    d->display->setRotation((uint8_t)n);
    return mcs_null();
}
NATIVE(tft_fill) {
    TFT_CHECK();
    int32_t color;
    if (color_value(vm, self, argv[0], &color))
        d->display->fillScreen((uint16_t)color);
    return mcs_null();
}
NATIVE(tft_pixel) {
    DRAW_CHECK();
    int32_t x, y, c;
    if (integer(vm, argv[0], -32767, 32767, &x) && integer(vm, argv[1], -32767, 32767, &y) &&
        color_value(vm, self, argv[2], &c))
        display->drawPixel(x, y, (uint16_t)c);
    return mcs_null();
}
/* Checked row-major script image, no retained pointers or native heap. Keep a
 * fixed 4096-pixel limit: validation and transfer do not invoke script callbacks
 * or safepoints, so a collection cannot mutate between validation and drawing.
 * This is bounded synchronous work, not a measured hardware Stop-latency claim.
 * pushImage is NOT virtual: dispatch through the exact native derived type. */
NATIVE(tft_image) {
    (void)argc;
    sprite_t *sprite = NULL;
    tft_t *owner = NULL;
    TFT_eSPI *display = NULL;
    if (mcs_userdata_of(self, &sprite_def)) {
        sprite = sprite_checked(vm, self);
        if (!sprite)
            return mcs_null();
        display = sprite->display;
    } else {
        owner = checked(vm, self);
        if (!owner)
            return mcs_null();
        display = owner->display;
#if MCS_ENABLE_LVGL
        if (owner->pin >= 0) {
            mcs_raise(vm, "InvalidOperationException", "Detach LVGL before drawing an image");
            return mcs_null();
        }
#endif
    }
    int32_t x, y, w, h;
    if (!integer(vm, argv[0], 0, 4095, &x) || !integer(vm, argv[1], 0, 4095, &y) ||
        !integer(vm, argv[2], 1, 4096, &w) || !integer(vm, argv[3], 1, 4096, &h))
        return mcs_null();
    if (w > display->width() || h > display->height() || x > display->width() - w ||
        y > display->height() - h || w > 4096 / h) {
        mcs_raise(vm, "ArgumentOutOfRangeException",
                  "Image must fit the canvas and contain <=4096 pixels");
        return mcs_null();
    }
    int kind = mcs_obj_kind(argv[4]);
    if ((kind != MCS_O_ARRAY && kind != MCS_O_LIST) || mcs_len(argv[4]) != (uint32_t)(w * h)) {
        mcs_raise(vm, "ArgumentException",
                  "Image requires an array or List with exactly width*height integers");
        return mcs_null();
    }
    /* Validate every entry before any pixel changes, including a late bad value. */
    for (uint32_t i = 0; i < (uint32_t)(w * h); ++i) {
        int32_t color;
        if (!color_value(vm, self, mcs_index(argv[4], i), &color))
            return mcs_null();
    }
    int depth = sprite ? sprite->display->getColorDepth() : 16;
    if (depth == 1 || depth == 4) {
        /* Real native drawPixel avoids upstream packed pushImage odd-row/padding
         * assumptions. Script values are exact bits/indices, not packed bytes. */
        for (int32_t row = 0; row < h; ++row)
            for (int32_t col = 0; col < w; ++col)
                sprite->display->drawPixel(
                    x + col, y + row, (uint16_t)mcs_index(argv[4], (uint32_t)(row * w + col)).as.i);
        return mcs_null();
    }
    bool swap = display->getSwapBytes();
    display->setSwapBytes(true);
    if (owner)
        display->startWrite();
    uint16_t pixels[64]; // 128 bytes; mutable RAM overload, no PROGMEM/VLA path
    for (int32_t row = 0; row < h; ++row) {
        for (int32_t col = 0; col < w; col += 64) {
            int32_t n = w - col < 64 ? w - col : 64;
            for (int32_t i = 0; i < n; ++i)
                pixels[i] = (uint16_t)mcs_index(argv[4], (uint32_t)(row * w + col + i)).as.i;
            if (sprite)
                sprite->display->pushImage(x + col, y + row, n, 1, pixels);
            else
                display->pushImage(x + col, y + row, n, 1, pixels);
        }
    }
    if (owner)
        display->endWrite();
    display->setSwapBytes(swap);
    return mcs_null();
}
NATIVE(tft_line) {
    DRAW_CHECK();
    int32_t x, y, x2, y2, c;
    if (integer(vm, argv[0], -32767, 32767, &x) && integer(vm, argv[1], -32767, 32767, &y) &&
        integer(vm, argv[2], -32767, 32767, &x2) && integer(vm, argv[3], -32767, 32767, &y2) &&
        color_value(vm, self, argv[4], &c))
        display->drawLine(x, y, x2, y2, (uint16_t)c);
    return mcs_null();
}
static mcs_value_t rectangle(mcs_vm_t *vm, mcs_value_t self, mcs_value_t *argv, bool filled) {
    TFT_eSPI *display = drawing_checked(vm, self);
    if (!display)
        return mcs_null();
    int32_t x, y, w, h, c;
    if (integer(vm, argv[0], -32767, 32767, &x) && integer(vm, argv[1], -32767, 32767, &y) &&
        integer(vm, argv[2], 1, 4096, &w) && integer(vm, argv[3], 1, 4096, &h) &&
        color_value(vm, self, argv[4], &c)) {
        if (filled)
            display->fillRect(x, y, w, h, (uint16_t)c);
        else
            display->drawRect(x, y, w, h, (uint16_t)c);
    }
    return mcs_null();
}
NATIVE(tft_rect) {
    (void)argc;
    return rectangle(vm, self, argv, false);
}
NATIVE(tft_fill_rect) {
    (void)argc;
    return rectangle(vm, self, argv, true);
}
static mcs_value_t circle(mcs_vm_t *vm, mcs_value_t self, mcs_value_t *argv, bool filled) {
    TFT_eSPI *display = drawing_checked(vm, self);
    if (!display)
        return mcs_null();
    int32_t x, y, r, c;
    if (integer(vm, argv[0], -32767, 32767, &x) && integer(vm, argv[1], -32767, 32767, &y) &&
        integer(vm, argv[2], 0, 4096, &r) && color_value(vm, self, argv[3], &c)) {
        if (filled)
            display->fillCircle(x, y, r, (uint16_t)c);
        else
            display->drawCircle(x, y, r, (uint16_t)c);
    }
    return mcs_null();
}
NATIVE(tft_circle) {
    (void)argc;
    return circle(vm, self, argv, false);
}
NATIVE(tft_fill_circle) {
    (void)argc;
    return circle(vm, self, argv, true);
}
NATIVE(tft_text_color) {
    DRAW_CHECK();
    int32_t fg, bg;
    if (color_value(vm, self, argv[0], &fg) && color_value(vm, self, argv[1], &bg))
        display->setTextColor((uint16_t)fg, (uint16_t)bg);
    return mcs_null();
}
NATIVE(tft_text_size) {
    DRAW_CHECK();
    int32_t n;
    if (integer(vm, argv[0], 1, 7, &n))
        display->setTextSize((uint8_t)n);
    return mcs_null();
}
NATIVE(tft_draw_text) {
    DRAW_CHECK();
    int32_t x, y, font;
    if (!mcs_is_string(argv[0])) {
        mcs_raise(vm, "ArgumentException", "DrawText expects a string");
        return mcs_null();
    }
    if (!integer(vm, argv[1], -32767, 32767, &x) || !integer(vm, argv[2], -32767, 32767, &y) ||
        !integer(vm, argv[3], 1, 8, &font))
        return mcs_null();
    if (!(display->fontsLoaded() & (1u << font))) {
        mcs_raise(vm, "NotSupportedException", "TFT font is not compiled into the firmware");
        return mcs_null();
    }
    if (mcs_strlen(argv[0]) > 4096 || strlen(mcs_cstr(argv[0])) != mcs_strlen(argv[0])) {
        mcs_raise(vm, "ArgumentException", "TFT text must be <=4096 bytes without embedded NUL");
        return mcs_null();
    }
    return mcs_int(display->drawString(mcs_cstr(argv[0]), x, y, (uint8_t)font));
}
NATIVE(tft_version) {
    (void)self;
    (void)argc;
    (void)argv;
    return mcs_string(vm, TFT_ESPI_VERSION);
}
#if MCS_ENABLE_LVGL
static int row_sink(void *ud, int x, int y, int n, uint16_t *pixels) {
    ((tft_t *)ud)->display->pushImage(x, y, n, 1, pixels);
    return 0; /* TFT_eSPI exposes no per-transfer IO acknowledgement. */
}
static int flush(void *ud, int x, int y, int w, int h, const void *pixels, size_t stride,
                 size_t bytes) {
    tft_t *d = (tft_t *)ud;
    if (!d->display || !d->begun)
        return -1;
    bool swap = d->display->getSwapBytes();
    d->display->setSwapBytes(true);
    d->display->startWrite();
    int result = mcs_rgb565_transfer(d->display->width(), d->display->height(), x, y, w, h, pixels,
                                     stride, bytes, row_sink, d);
    d->display->endWrite();
    d->display->setSwapBytes(swap);
    return result;
}
#if defined(TOUCH_CS)
static int pointer(void *ud, int *x, int *y, bool *pressed) {
    tft_t *d = (tft_t *)ud;
    if (!d->display || !d->begun || !d->calibrated)
        return -1;
    uint16_t px = 0, py = 0;
    *pressed = d->display->getTouch(&px, &py);
    if (*pressed) {
        *x = px;
        *y = py;
    }
    return 0;
}
#endif
NATIVE(tft_attach) {
    TFT_CHECK();
    if (d->pin >= 0) {
        mcs_raise(vm, "InvalidOperationException", "TFT is already attached");
        return mcs_null();
    }
    d->backend.flush = flush;
    d->backend.ud = d;
    d->backend.read_pointer = NULL;
#if defined(TOUCH_CS)
    if (d->calibrated)
        d->backend.read_pointer = pointer;
#endif
    int pin = mcs_pin(vm, self);
    if (pin < 0) {
        mcs_raise(vm, "InvalidOperationException", "TFT owner pin unavailable");
        return mcs_null();
    }
    if (mcs_lvgl_set_backend(vm, &d->backend)) {
        mcs_unpin(vm, pin);
        mcs_raise(vm, "InvalidOperationException", "Attach TFT before constructing LvGui");
        return mcs_null();
    }
    d->pin = pin;
    return mcs_null();
}
NATIVE(tft_detach) {
    TFT_CHECK();
    if (d->pin >= 0) {
        if (mcs_lvgl_set_backend(vm, NULL)) {
            mcs_raise(vm, "InvalidOperationException", "Dispose LvGui before detaching TFT");
            return mcs_null();
        }
        mcs_unpin(vm, d->pin);
        d->pin = -1;
    }
    return mcs_null();
}
#endif
NATIVE(tft_set_touch) {
    TFT_CHECK();
#if defined(TOUCH_CS)
#if MCS_ENABLE_LVGL
    if (d->pin >= 0) {
        mcs_raise(vm, "InvalidOperationException", "Set touch calibration before attaching LVGL");
        return mcs_null();
    }
#endif
    uint16_t values[5];
    int32_t n;
    for (int i = 0; i < 5; i++) {
        if (!integer(vm, argv[i], 0, i == 4 ? 7 : 65535, &n))
            return mcs_null();
        values[i] = (uint16_t)n;
    }
    if (!values[1] || !values[3]) {
        mcs_raise(vm, "ArgumentException", "Touch calibration ranges must be nonzero");
        return mcs_null();
    }
    d->display->setTouch(values);
    d->calibrated = true;
#else
    mcs_raise(vm, "NotSupportedException", "TOUCH_CS was not configured in firmware");
#endif
    return mcs_null();
}
/* The parent is older than every child in the VM object list. One shared pin
 * holds it during GC; children finalize before it at VM teardown. Reservation is
 * published before VM-accounted allocations that may panic/longjmp on OOM. */
static void sprite_release(mcs_vm_t *vm, sprite_t *s) {
    if (s->display) {
        s->display->~mcs_tft_safe_sprite();
        mcs_mem_realloc(vm, s->display, sizeof(mcs_tft_safe_sprite), 0);
        s->display = NULL;
    }
    if (s->owner) {
        tft_t *d = s->owner;
        if (s->previous)
            s->previous->next = s->next;
        else
            d->sprites = s->next;
        if (s->next)
            s->next->previous = s->previous;
        s->previous = s->next = NULL;
        d->sprite_bytes -= s->bytes;
        d->sprite_count--;
        if (!d->sprite_count) {
            mcs_unpin(vm, d->sprite_pin);
            d->sprite_pin = -1;
        }
        s->owner = NULL;
        s->bytes = 0;
    }
}
static void sprite_finalizer(mcs_vm_t *vm, void *data) {
    sprite_release(vm, (sprite_t *)data);
}
static void sprite_ctor(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    sprite_t *s = (sprite_t *)mcs_userdata(self);
    if (argc != 3 && argc != 4) {
        mcs_raise(vm, "ArgumentException", "new TftSprite(Tft owner, width, height[, depth])");
        return;
    }
    tft_t *d = checked(vm, argv[0], false);
    int32_t w, h, depth = 16;
    if (!d || !integer(vm, argv[1], 1, 4096, &w) || !integer(vm, argv[2], 1, 4096, &h) ||
        (argc == 4 && !integer(vm, argv[3], 1, 16, &depth)))
        return;
    size_t bytes;
    if (!mcs_tft_sprite_allocation(w, h, depth, &bytes)) {
        mcs_raise(vm, "NotSupportedException", "TftSprite supports exact 1, 4, 8 or 16 bit depth");
        return;
    }
    if (d->sprite_count >= MCS_TFT_SPRITE_COUNT || bytes > MCS_TFT_SPRITE_BYTES ||
        d->sprite_bytes > MCS_TFT_SPRITE_BYTES - bytes) {
        mcs_raise(vm, "OutOfMemoryException",
                  "TFT sprite count/native pixel+palette budget exceeded");
        return;
    }
    if (!d->sprite_count) {
        int pin = mcs_pin(vm, argv[0]);
        if (pin < 0) {
            mcs_raise(vm, "InvalidOperationException", "TFT sprite owner pin unavailable");
            return;
        }
        d->sprite_pin = pin;
    }
    s->owner = d;
    s->next = d->sprites;
    if (s->next)
        s->next->previous = s;
    d->sprites = s;
    s->bytes = bytes;
    d->sprite_count++;
    d->sprite_bytes += bytes;
    void *memory = mcs_mem_realloc(vm, NULL, 0, sizeof(mcs_tft_safe_sprite));
    s->display = new (memory) mcs_tft_safe_sprite(d->display);
    if (!s->display->createChecked((int16_t)w, (int16_t)h, (int8_t)depth)) {
        sprite_release(vm, s);
        mcs_raise(vm, "OutOfMemoryException", "Native TFT sprite pixel/palette allocation failed");
    }
}
NATIVE(sprite_dispose) {
    (void)argc;
    (void)argv;
    sprite_t *s = (sprite_t *)mcs_check_userdata(vm, self, &sprite_def);
    if (s)
        sprite_release(vm, s);
    return mcs_null();
}
NATIVE(sprite_fill) {
    (void)argc;
    sprite_t *s = sprite_checked(vm, self);
    int32_t color;
    if (s && color_value(vm, self, argv[0], &color))
        s->display->fillSprite((uint16_t)color);
    return mcs_null();
}
static mcs_value_t sprite_read_value(mcs_vm_t *vm, mcs_value_t self, mcs_value_t *argv, bool raw) {
    sprite_t *s = sprite_checked(vm, self);
    int32_t x, y;
    if (!s || !integer(vm, argv[0], 0, s->display->width() - 1, &x) ||
        !integer(vm, argv[1], 0, s->display->height() - 1, &y))
        return mcs_null();
    return mcs_int(raw ? s->display->readPixelValue(x, y) : s->display->readPixel(x, y));
}
NATIVE(sprite_read) {
    (void)argc;
    return sprite_read_value(vm, self, argv, false);
}
NATIVE(sprite_read_raw) {
    (void)argc;
    return sprite_read_value(vm, self, argv, true);
}
NATIVE(sprite_palette_set) {
    (void)argc;
    sprite_t *s = sprite_checked(vm, self);
    int32_t index, color;
    if (!s)
        return mcs_null();
    if (s->display->getColorDepth() != 4) {
        mcs_raise(vm, "NotSupportedException", "Palette requires a 4-bit sprite");
        return mcs_null();
    }
    if (integer(vm, argv[0], 0, 15, &index) && integer(vm, argv[1], 0, 65535, &color))
        s->display->setPaletteColor((uint8_t)index, (uint16_t)color);
    return mcs_null();
}
NATIVE(sprite_palette_get) {
    (void)argc;
    sprite_t *s = sprite_checked(vm, self);
    int32_t index;
    if (!s)
        return mcs_null();
    if (s->display->getColorDepth() != 4) {
        mcs_raise(vm, "NotSupportedException", "Palette requires a 4-bit sprite");
        return mcs_null();
    }
    return integer(vm, argv[0], 0, 15, &index)
               ? mcs_int(s->display->getPaletteColor((uint8_t)index))
               : mcs_null();
}
NATIVE(sprite_bitmap_colors) {
    (void)argc;
    sprite_t *s = sprite_checked(vm, self);
    int32_t fg, bg;
    if (!s)
        return mcs_null();
    if (s->display->getColorDepth() != 1) {
        mcs_raise(vm, "NotSupportedException", "BitmapColors requires a 1-bit sprite");
        return mcs_null();
    }
    if (!integer(vm, argv[0], 0, 65535, &fg) || !integer(vm, argv[1], 0, 65535, &bg))
        return mcs_null();
    if (fg == bg) {
        mcs_raise(vm, "ArgumentException",
                  "Bitmap foreground/background must differ; no implicit inversion");
        return mcs_null();
    }
    s->display->setBitmapColor((uint16_t)fg, (uint16_t)bg); // actual upstream PARENT-wide mapping
    return mcs_null();
}
NATIVE(sprite_depth) {
    (void)argc;
    (void)argv;
    sprite_t *s = sprite_checked(vm, self);
    return s ? mcs_int(s->display->getColorDepth()) : mcs_null();
}
static mcs_value_t sprite_push(mcs_vm_t *vm, mcs_value_t self, mcs_value_t *argv,
                               bool transparent) {
    sprite_t *s = sprite_checked(vm, self);
    int32_t x, y, color = 0;
    if (!s || !integer(vm, argv[0], -32767, 32767, &x) || !integer(vm, argv[1], -32767, 32767, &y))
        return mcs_null();
    if (transparent) {
        if (!color_value(vm, self, argv[2], &color))
            return mcs_null();
        if (s->display->getColorDepth() == 1 && color != 0) {
            mcs_raise(vm, "NotSupportedException",
                      "Upstream 1-bit transparency only supports index 0");
            return mcs_null();
        }
    }
    if (!s->owner->begun) {
        mcs_raise(vm, "InvalidOperationException", "Call parent Tft.Begin before pushing sprite");
        return mcs_null();
    }
#if MCS_ENABLE_LVGL
    if (s->owner->pin >= 0) {
        mcs_raise(vm, "InvalidOperationException",
                  "Detach LVGL before pushing a sprite to its TFT");
        return mcs_null();
    }
#endif
    if (transparent)
        s->display->pushSprite(x, y, (uint16_t)color);
    else
        s->display->pushSprite(x, y);
    return mcs_null();
}
NATIVE(sprite_push_plain) {
    (void)argc;
    return sprite_push(vm, self, argv, false);
}
NATIVE(sprite_push_transparent) {
    (void)argc;
    return sprite_push(vm, self, argv, true);
}
/* Exact supported native depth pairs, but read/draw through real upstream APIs
 * instead of pushToSprite's memcpy/packed odd-width assumptions. Source and
 * destination are distinct live wrappers, rooted by the VM's call arguments.
 * No callbacks/allocation/safepoints can invalidate them mid-copy. */
NATIVE(sprite_copy) {
    (void)argc;
    sprite_t *source = sprite_checked(vm, self);
    if (!source)
        return mcs_null();
    sprite_t *destination = sprite_checked(vm, argv[0]);
    if (!destination)
        return mcs_null();
    if (source == destination) {
        mcs_raise(vm, "ArgumentException",
                  "CopyTo requires distinct source and destination sprites");
        return mcs_null();
    }
    int depth = source->display->getColorDepth();
    int target_depth = destination->display->getColorDepth();
    if (depth != target_depth && !(depth == 16 && target_depth == 8)) {
        mcs_raise(vm, "NotSupportedException", "CopyTo supports16->16/8,8->8,4->4,1->1");
        return mcs_null();
    }
    int32_t x, y;
    if (!integer(vm, argv[1], 0, 4095, &x) || !integer(vm, argv[2], 0, 4095, &y))
        return mcs_null();
    int32_t w = source->display->width(), h = source->display->height();
    int32_t dw = destination->display->width(), dh = destination->display->height();
    if (w > dw || h > dh || x > dw - w || y > dh - h || w > 4096 / h) {
        mcs_raise(vm, "ArgumentOutOfRangeException",
                  "CopyTo must fit the target and contain <=4096 pixels");
        return mcs_null();
    }
    for (int32_t row = 0; row < h; ++row)
        for (int32_t col = 0; col < w; ++col) {
            uint16_t color = depth < 8 ? source->display->readPixelValue(col, row)
                                       : source->display->readPixel(col, row);
            destination->display->drawPixel(x + col, y + row, color);
        }
    return mcs_null();
}
NATIVE(tft_sprite_bytes) {
    (void)argc;
    (void)argv;
    tft_t *d = checked(vm, self, false);
    return d ? mcs_int((mcs_int_t)d->sprite_bytes) : mcs_null();
}
NATIVE(tft_sprite_count) {
    (void)argc;
    (void)argv;
    tft_t *d = checked(vm, self, false);
    return d ? mcs_int((mcs_int_t)d->sprite_count) : mcs_null();
}
static const mcs_reg_t sprite_members[] = {MCS_FN("Dispose", sprite_dispose, 0),
                                           MCS_GET("Width", tft_width),
                                           MCS_GET("Height", tft_height),
                                           MCS_GET("ColorDepth", sprite_depth),
                                           MCS_FN("Fill", sprite_fill, 1),
                                           MCS_FN("DrawPixel", tft_pixel, 3),
                                           MCS_FN("DrawImage", tft_image, 5),
                                           MCS_FN("DrawLine", tft_line, 5),
                                           MCS_FN("DrawRect", tft_rect, 5),
                                           MCS_FN("FillRect", tft_fill_rect, 5),
                                           MCS_FN("DrawCircle", tft_circle, 4),
                                           MCS_FN("FillCircle", tft_fill_circle, 4),
                                           MCS_FN("TextColor", tft_text_color, 2),
                                           MCS_FN("TextSize", tft_text_size, 1),
                                           MCS_FN("DrawText", tft_draw_text, 4),
                                           MCS_FN("ReadPixel", sprite_read, 2),
                                           MCS_FN("ReadValue", sprite_read_raw, 2),
                                           MCS_FN("CopyTo", sprite_copy, 3),
                                           MCS_FN("SetPaletteColor", sprite_palette_set, 2),
                                           MCS_FN("GetPaletteColor", sprite_palette_get, 1),
                                           MCS_FN("BitmapColors", sprite_bitmap_colors, 2),
                                           MCS_FN("Push", sprite_push_plain, 2),
                                           MCS_FN("PushTransparent", sprite_push_transparent, 3),
                                           MCS_REG_END};
const mcs_class_def_t sprite_def = {"TftSprite",      sizeof(sprite_t), sprite_ctor,
                                    sprite_finalizer, sprite_members,   NULL};
static const mcs_reg_t tft_members[] = {MCS_FN("Begin", tft_begin, 0),
                                        MCS_FN("Dispose", tft_dispose, 0),
                                        MCS_GET("Width", tft_width),
                                        MCS_GET("Height", tft_height),
                                        MCS_GET("SpriteBytes", tft_sprite_bytes),
                                        MCS_GET("SpriteCount", tft_sprite_count),
                                        MCS_GET("Rotation", tft_rotation_get),
                                        MCS_SET("Rotation", tft_rotation_set),
                                        MCS_FN("Fill", tft_fill, 1),
                                        MCS_FN("DrawPixel", tft_pixel, 3),
                                        MCS_FN("DrawImage", tft_image, 5),
                                        MCS_FN("DrawLine", tft_line, 5),
                                        MCS_FN("DrawRect", tft_rect, 5),
                                        MCS_FN("FillRect", tft_fill_rect, 5),
                                        MCS_FN("DrawCircle", tft_circle, 4),
                                        MCS_FN("FillCircle", tft_fill_circle, 4),
                                        MCS_FN("TextColor", tft_text_color, 2),
                                        MCS_FN("TextSize", tft_text_size, 1),
                                        MCS_FN("DrawText", tft_draw_text, 4),
                                        MCS_FN("SetTouch", tft_set_touch, 5),
#if MCS_ENABLE_LVGL
                                        MCS_FN("AttachLVGL", tft_attach, 0),
                                        MCS_FN("DetachLVGL", tft_detach, 0),
#endif
                                        MCS_REG_END};
static const mcs_reg_t tft_statics[] = {MCS_GET("Version", tft_version), MCS_REG_END};
const mcs_class_def_t tft_def = {"Tft", sizeof(tft_t), ctor, finalizer, tft_members, tft_statics};
} // namespace
uint64_t mcs_tft_scope_begin(mcs_vm_t *vm) {
    (void)vm;
    return owner_serial;
}
int mcs_tft_scope_end(mcs_vm_t *vm, uint64_t checkpoint) {
    tft_t *d = active_tft;
    if (!d || d->vm != vm || d->serial == checkpoint)
        return 0;
#if MCS_ENABLE_LVGL
    /* Never leave a live GUI pointing at a freed native driver. The caller must
     * end its GUI scope first; fail safely without disposing any TFT children. */
    if (d->pin >= 0 && mcs_lvgl_set_backend(vm, NULL))
        return -1;
#endif
    release(vm, d); // invalidates reachable child wrappers as well as owner
    return 0;
}
void mcs_tft_open_lib(mcs_vm_t *vm) {
    mcs_register_class(vm, &tft_def);
    mcs_register_class(vm, &sprite_def);
}
#endif
