/*
 * MicroCS - built-in "ws2812" driver: the C# LedStrip class (front end).
 *
 *   new LedStrip(pin, count[, LedStrip.GRB | RGB | GRBW])  WS2812 / SK6812 "NeoPixel" strips
 *   strip[i] = 0xRRGGBB; SetPixel / GetPixel / Fill / Clear / Brightness / Show / Dispose
 *   LedStrip.Rgb(r, g, b[, w]), LedStrip.Hsv(hue[, sat, val])
 *
 * Colours are 0xRRGGBB (0xWWRRGGBB for GRBW). Show() scales them by Brightness,
 * puts them in the strip's byte order and calls the backend's write()
 * (mcs_ws2812_ops_t) of the driver registered as "ws2812": PIO on RP2, RMT on
 * ESP32, bit-bang on STM32, led_strip on Zephyr ... (see include/mcs_driver.h).
 * Off with -DMCS_ENABLE_WS2812=0 (CMake -DMICROCS_WS2812=OFF, menuconfig, Kconfig).
 */
#include "mcs_driver.h"
#if MCS_ENABLE_WS2812
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define OPT_INT(i, var, def) int var = argc > (i) ? (int)mcs_to_int(vm, argv[i]) : (def); if (mcs_has_exception(vm)) return mcs_null()

#ifndef MCS_LEDSTRIP_MAX
#define MCS_LEDSTRIP_MAX 1024     /* LEDs per strip */
#endif
typedef struct { int pin, count, order, bpp, brightness; uint32_t* px; uint8_t* wire; const mcs_driver_t* drv; } ledstrip_t;
static const mcs_class_def_t ledstrip_def;
static void ledstrip_free(mcs_vm_t* vm, void* data) {
    ledstrip_t* s = (ledstrip_t*)data;
    if (s->px) mcs_mem_realloc(vm, s->px, (size_t)s->count * sizeof(uint32_t), 0);
    if (s->wire) mcs_mem_realloc(vm, s->wire, (size_t)s->count * (size_t)s->bpp, 0);
    s->px = NULL; s->wire = NULL;
}
static void ledstrip_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    ledstrip_t* s = (ledstrip_t*)mcs_userdata(self);
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "LedStrip(pin, count[, order]) expected"); return; }
    int pin = mcs_hal_pin_arg(vm, argv[0]); if (mcs_has_exception(vm)) return;
    int count = (int)mcs_to_int(vm, argv[1]); if (mcs_has_exception(vm)) return;
    int order = argc > 2 ? (int)mcs_to_int(vm, argv[2]) : MCS_LED_GRB; if (mcs_has_exception(vm)) return;
    if (count < 1 || count > MCS_LEDSTRIP_MAX) { mcs_raise(vm, "ArgumentOutOfRangeException", "LED count out of range"); return; }
    if (order < MCS_LED_GRB || order > MCS_LED_GRBW) { mcs_raise(vm, "ArgumentOutOfRangeException", "LED order out of range"); return; }
    s->pin = pin; s->count = count; s->order = order; s->bpp = order == MCS_LED_GRBW ? 4 : 3; s->brightness = 255;
    s->px = (uint32_t*)mcs_mem_realloc(vm, NULL, 0, (size_t)count * sizeof(uint32_t));
    s->wire = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, (size_t)count * (size_t)s->bpp);
    if (!s->px || !s->wire) { ledstrip_free(vm, s); s->count = 0; mcs_raise(vm, "OutOfMemoryException", "LedStrip: no memory for %d LEDs", count); return; }
    memset(s->px, 0, (size_t)count * sizeof(uint32_t));
    s->drv = mcs_driver_find("ws2812");
}
#define LS() ledstrip_t* s = (ledstrip_t*)mcs_check_userdata(vm, self, &ledstrip_def); if (!s) return mcs_null(); \
    if (!s->px) { mcs_raise(vm, "ObjectDisposedException", "LedStrip"); return mcs_null(); }
static int led_index(mcs_vm_t* vm, ledstrip_t* s, mcs_value_t v) {
    mcs_int_t i = mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (i < 0 || i >= s->count) { mcs_raise(vm, "IndexOutOfRangeException", "LED index %d outside 0..%d", (int)i, s->count - 1); return -1; }
    return (int)i;
}
static int clamp255(mcs_vm_t* vm, mcs_value_t v) {
    mcs_int_t c = mcs_to_int(vm, v);
    return c < 0 ? 0 : c > 255 ? 255 : (int)c;
}
NATIVE(ls_get) { LS(); int i = led_index(vm, s, argv[0]); if (i < 0) return mcs_null(); return mcs_int((mcs_int_t)s->px[i]); }
NATIVE(ls_set) {
    LS(); int i = led_index(vm, s, argv[0]); if (i < 0) return mcs_null();
    mcs_int_t c = mcs_to_int(vm, argv[1]); if (mcs_has_exception(vm)) return mcs_null();
    s->px[i] = (uint32_t)c;
    return mcs_null();
}
static uint32_t rgbw(mcs_vm_t* vm, int argc, mcs_value_t* argv) {   /* (r, g, b[, w]) */
    uint32_t r = (uint32_t)clamp255(vm, argv[0]), g = (uint32_t)clamp255(vm, argv[1]), b = (uint32_t)clamp255(vm, argv[2]);
    uint32_t w = argc > 3 ? (uint32_t)clamp255(vm, argv[3]) : 0;
    return w << 24 | r << 16 | g << 8 | b;
}
NATIVE(ls_setpixel) {   /* SetPixel(i, color) or SetPixel(i, r, g, b[, w]) */
    LS();
    if (argc != 2 && argc != 4 && argc != 5) { mcs_raise(vm, "ArgumentException", "SetPixel(index, color) or SetPixel(index, r, g, b[, w])"); return mcs_null(); }
    int i = led_index(vm, s, argv[0]); if (i < 0) return mcs_null();
    uint32_t c = argc == 2 ? (uint32_t)mcs_to_int(vm, argv[1]) : rgbw(vm, argc - 1, argv + 1);
    if (mcs_has_exception(vm)) return mcs_null();
    s->px[i] = c;
    return mcs_null();
}
NATIVE(ls_fill) {   /* Fill(color[, first, count]) */
    LS();
    uint32_t c = (uint32_t)mcs_to_int(vm, argv[0]);
    OPT_INT(1, first, 0); OPT_INT(2, n, s->count - first);
    if (first < 0 || n < 0 || first + n > s->count) { mcs_raise(vm, "ArgumentOutOfRangeException", "first/count out of range"); return mcs_null(); }
    for (int i = first; i < first + n; i++) s->px[i] = c;
    return mcs_null();
}
NATIVE(ls_clear) { LS(); memset(s->px, 0, (size_t)s->count * sizeof(uint32_t)); return mcs_null(); }
NATIVE(ls_show) {
    LS();
    const mcs_ws2812_ops_t* ops = s->drv ? (const mcs_ws2812_ops_t*)s->drv->ops : NULL;
    if (!ops || !ops->write) return mcs_hal_raise(vm, "LedStrip.Show", MCS_HAL_ENOTSUP);
    int br = s->brightness;
    uint8_t* w = s->wire;
    for (int i = 0; i < s->count; i++) {
        uint32_t c = s->px[i];
        uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * (uint32_t)br / 255), g = (uint8_t)(((c >> 8) & 0xFF) * (uint32_t)br / 255);
        uint8_t b = (uint8_t)((c & 0xFF) * (uint32_t)br / 255), wh = (uint8_t)(((c >> 24) & 0xFF) * (uint32_t)br / 255);
        if (s->order == MCS_LED_RGB) { *w++ = r; *w++ = g; *w++ = b; }
        else { *w++ = g; *w++ = r; *w++ = b; if (s->order == MCS_LED_GRBW) *w++ = wh; }
    }
    int rc = ops->write(s->drv->ctx, s->pin, s->wire, (size_t)s->count * (size_t)s->bpp, s->order);
    if (rc < 0) return mcs_hal_raise(vm, "LedStrip.Show", rc);
    return mcs_null();
}
NATIVE(ls_count) { LS(); return mcs_int(s->count); }
NATIVE(ls_pin) { LS(); return mcs_int(s->pin); }
NATIVE(ls_getbright) { LS(); return mcs_int(s->brightness); }
NATIVE(ls_setbright) { LS(); s->brightness = clamp255(vm, argv[0]); return mcs_null(); }
NATIVE(ls_dispose) {
    ledstrip_t* s = (ledstrip_t*)mcs_check_userdata(vm, self, &ledstrip_def); if (!s) return mcs_null();
    ledstrip_free(vm, s);
    return mcs_null();
}
/* static colour helpers */
NATIVE(ls_rgb) {
    if (argc < 3 || argc > 4) { mcs_raise(vm, "ArgumentException", "LedStrip.Rgb(r, g, b[, w])"); return mcs_null(); }
    uint32_t c = rgbw(vm, argc, argv); if (mcs_has_exception(vm)) return mcs_null();
    return mcs_int((mcs_int_t)c);
}
NATIVE(ls_hsv) {   /* Hsv(hue 0..359, saturation 0..255, value 0..255) -> 0xRRGGBB */
    OPT_INT(0, h, 0); OPT_INT(1, sat, 255); OPT_INT(2, val, 255);
    h %= 360; if (h < 0) h += 360;
    sat = sat < 0 ? 0 : sat > 255 ? 255 : sat; val = val < 0 ? 0 : val > 255 ? 255 : val;
    int region = h / 60, rem = (h % 60) * 255 / 60;
    int p = val * (255 - sat) / 255, q = val * (255 - sat * rem / 255) / 255, t = val * (255 - sat * (255 - rem) / 255) / 255;
    int r, g, b;
    switch (region) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    default: r = val; g = p; b = q; break;
    }
    return mcs_int((mcs_int_t)((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b));
}
static const mcs_reg_t ledstrip_members[] = {
    MCS_FN("get_Item", ls_get, 1), MCS_FN("set_Item", ls_set, 2),
    MCS_FN("GetPixel", ls_get, 1), MCS_FN("SetPixel", ls_setpixel, -1), MCS_FN("Fill", ls_fill, -1),
    MCS_FN("Clear", ls_clear, 0), MCS_FN("Show", ls_show, 0), MCS_FN("Dispose", ls_dispose, 0),
    MCS_GET("Count", ls_count), MCS_GET("Pin", ls_pin), MCS_GET("Brightness", ls_getbright), MCS_SET("Brightness", ls_setbright),
    MCS_REG_END
};
static const mcs_reg_t ledstrip_statics[] = { MCS_FN("Rgb", ls_rgb, -1), MCS_FN("Hsv", ls_hsv, -1), MCS_REG_END };
static const mcs_const_t ledstrip_consts[] = {
    MCS_CONST("GRB", MCS_LED_GRB), MCS_CONST("RGB", MCS_LED_RGB), MCS_CONST("GRBW", MCS_LED_GRBW), MCS_CONST_END
};
static const mcs_class_def_t ledstrip_def = { "LedStrip", sizeof(ledstrip_t), ledstrip_ctor, ledstrip_free, ledstrip_members, ledstrip_statics };

void mcs_ws2812_open(mcs_vm_t* vm, const mcs_driver_t* drv) {
    (void)drv;
    mcs_register_class(vm, &ledstrip_def);
    mcs_register_consts(vm, "LedStrip", ledstrip_consts);
}
#endif
