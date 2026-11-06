/* MicroCS host CLI: an in-memory LVGL display + simulated pointer for `mcs --lvgl`
 * (make mcs-lvgl, tests/lvgl). The display is the "porting" a firmware would do;
 * scripts then use the real LVGL API. Host-only helpers for tests and examples:
 *   lvgl_host_run(ms)            advance LVGL time by ms (lv_tick_inc + lv_timer_handler)
 *   lvgl_host_pixel(x, y)        rendered colour 0xRRGGBB at (x, y)
 *   lvgl_host_touch(x, y, down)  move / press / release the simulated pointer
 *   lvgl_host_screenshot(path)   write the screen as a binary PPM through the host FS */
#include "mcs.h"
#include "mcs_config.h"
#if MCS_ENABLE_LVGL
#include <lvgl.h>
#include "mcs_lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_w, g_h;
static uint32_t* g_fb;                 /* 0xRRGGBB per pixel */
static int32_t g_tx, g_ty; static int g_down;

#if LVGL_VERSION_MAJOR >= 9
static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    int32_t w = lv_area_get_width(a);
    lv_color_format_t cf = lv_display_get_color_format(d);
    uint32_t bpp = lv_color_format_get_size(cf);
    for (int32_t y = a->y1; y <= a->y2; y++)
        for (int32_t x = a->x1; x <= a->x2; x++) {
            const uint8_t* p = px + ((size_t)(y - a->y1) * (size_t)w + (size_t)(x - a->x1)) * bpp;
            uint32_t c;
            if (bpp == 2) { uint16_t v = (uint16_t)(p[0] | p[1] << 8); c = ((v >> 11 & 31u) * 255u / 31u) << 16 | ((v >> 5 & 63u) * 255u / 63u) << 8 | (v & 31u) * 255u / 31u; }
            else c = (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
            if (x >= 0 && y >= 0 && x < g_w && y < g_h) g_fb[y * g_w + x] = c;
        }
    lv_display_flush_ready(d);
}
static void read_cb(lv_indev_t* i, lv_indev_data_t* d) { (void)i; d->point.x = g_tx; d->point.y = g_ty; d->state = g_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED; }
#else
static lv_disp_draw_buf_t g_dbuf;
static lv_disp_drv_t g_ddrv;
static lv_indev_drv_t g_idrv;
static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    int32_t w = lv_area_get_width(a);
    for (int32_t y = a->y1; y <= a->y2; y++)
        for (int32_t x = a->x1; x <= a->x2; x++) {
            lv_color_t c = px[(size_t)(y - a->y1) * (size_t)w + (size_t)(x - a->x1)];
            if (x >= 0 && y >= 0 && x < g_w && y < g_h) g_fb[y * g_w + x] = lv_color_to32(c) & 0xFFFFFFu;
        }
    lv_disp_flush_ready(drv);
}
static void read_cb(lv_indev_drv_t* i, lv_indev_data_t* d) { (void)i; d->point.x = (lv_coord_t)g_tx; d->point.y = (lv_coord_t)g_ty; d->state = g_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED; }
#endif

static void step(uint32_t ms) {
    while (ms) { uint32_t s = ms > 5 ? 5 : ms; lv_tick_inc(s); lv_timer_handler(); ms -= s; }
}
void mcs_lvgl_host_poll(mcs_vm_t* vm) {
    lv_tick_inc(5);
    mcs_lvgl_poll(vm);
}
static mcs_value_t h_run(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; mcs_int_t ms = mcs_to_int(vm, argv[0]); if (ms > 0) step((uint32_t)ms);
#if LVGL_VERSION_MAJOR >= 9
    lv_refr_now(NULL);
#else
    lv_refr_now(NULL);
#endif
    return mcs_null();
}
static mcs_value_t h_pixel(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    mcs_int_t x = mcs_to_int(vm, argv[0]), y = mcs_to_int(vm, argv[1]);
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) { mcs_raise(vm, "ArgumentOutOfRangeException", "pixel (%d, %d)", (int)x, (int)y); return mcs_null(); }
    return mcs_int((mcs_int_t)g_fb[y * g_w + x]);
}
static mcs_value_t h_touch(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    g_tx = (int32_t)mcs_to_int(vm, argv[0]); g_ty = (int32_t)mcs_to_int(vm, argv[1]); g_down = mcs_to_bool(vm, argv[2]);
    step(40);   /* let the input device read it */
    return mcs_null();
}
static mcs_value_t h_shot(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    const char* p = mcs_to_cstr(vm, argv[0]);
    FILE* f = fopen(p, "wb");
    if (!f) { mcs_raise(vm, "IOException", "cannot write %s", p); return mcs_null(); }
    fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
    for (int i = 0; i < g_w * g_h; i++) { uint8_t c[3] = { (uint8_t)(g_fb[i] >> 16), (uint8_t)(g_fb[i] >> 8), (uint8_t)g_fb[i] }; fwrite(c, 1, 3, f); }
    fclose(f);
    return mcs_null();
}

int mcs_lvgl_host_init(mcs_vm_t* vm, int w, int h) {
    static int inited;
    g_w = w; g_h = h;
    if (!inited) {
        g_fb = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof *g_fb);
        lv_init();
#if LVGL_VERSION_MAJOR >= 9
        lv_display_t* d = lv_display_create(w, h);
        size_t bsz = (size_t)w * 40u * 4u;
        void* buf = malloc(bsz);
        lv_display_set_buffers(d, buf, NULL, (uint32_t)bsz, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(d, flush_cb);
        lv_indev_t* in = lv_indev_create();
        lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(in, read_cb);
#else
        size_t n = (size_t)w * 40u;
        lv_color_t* buf = (lv_color_t*)malloc(n * sizeof(lv_color_t));
        lv_disp_draw_buf_init(&g_dbuf, buf, NULL, (uint32_t)n);
        lv_disp_drv_init(&g_ddrv);
        g_ddrv.hor_res = (lv_coord_t)w; g_ddrv.ver_res = (lv_coord_t)h;
        g_ddrv.flush_cb = flush_cb; g_ddrv.draw_buf = &g_dbuf;
        lv_disp_drv_register(&g_ddrv);
        lv_indev_drv_init(&g_idrv);
        g_idrv.type = LV_INDEV_TYPE_POINTER; g_idrv.read_cb = read_cb;
        lv_indev_drv_register(&g_idrv);
#endif
        inited = 1;
    }
    /* Initialize the display separately from the VM when the GUI application
     * will be owned by a background task. */
    if (!vm) return 0;
    mcs_register_function(vm, "lvgl_host_run", h_run, 1);
    mcs_register_function(vm, "lvgl_host_pixel", h_pixel, 2);
    mcs_register_function(vm, "lvgl_host_touch", h_touch, 3);
    mcs_register_function(vm, "lvgl_host_screenshot", h_shot, 1);
    return mcs_lvgl_open_lib(vm) ? 0 : -1;
}
#else
typedef int mcs_lvgl_host_unused_t;
#endif
