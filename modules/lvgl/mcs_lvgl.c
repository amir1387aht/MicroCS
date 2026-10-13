/* MicroCS optional LVGL adapter. Real upstream only, no vendored dependency.
 * Native object handles never expose pointers. Versioned slots are invalidated
 * by LV_EVENT_DELETE, including ancestor Clean/Delete and display teardown.
 * WIP coverage and exact supported configurations: docs/LVGL.md. */
#include "mcs_lvgl.h"
#if MCS_ENABLE_LVGL
#if defined(__has_include)
#if !__has_include(<lvgl.h>)
#error                                                                                             \
    "MCS_ENABLE_LVGL=1 requires real LVGL headers/library/lv_conf.h: see docs/LVGL.md and MICROCS_LVGL_ROOT"
#endif
#endif
#include <lvgl.h>
#include <string.h>
#include <limits.h>
#if !defined(MCS_LVGL_MAJOR) || MCS_LVGL_MAJOR != LVGL_VERSION_MAJOR
#error "Select MCS_LVGL_MAJOR=8 or 9 to match the actual linked LVGL headers"
#endif
#if (LVGL_VERSION_MAJOR != 8 && LVGL_VERSION_MAJOR != 9) || LVGL_VERSION_MINOR != 4 ||             \
    LVGL_VERSION_PATCH != 0
#error "This WIP adapter validates LVGL 8.4.0 and 9.4.0 only"
#endif
#if LVGL_VERSION_MAJOR == 8 && LV_MEM_CUSTOM && !LV_ENABLE_GC
#error                                                                                             \
    "MicroCS LVGL8 requires deinit support: LV_MEM_CUSTOM=0 (or a validated LV_ENABLE_GC integration); custom allocator without GC cannot safely reset LVGL8"
#endif
#if MCS_ENABLE_THREADS
#error                                                                                             \
    "MicroCS LVGL WIP requires a single-threaded VM (MCS_ENABLE_THREADS=0); multi-task GUI affinity is not yet validated"
#endif
#if LVGL_VERSION_MAJOR == 8 && (LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP)
#error "MicroCS LVGL8 WIP requires native RGB565: LV_COLOR_DEPTH=16 and LV_COLOR_16_SWAP=0"
#endif
#if MCS_LVGL_OBJECTS < 2 || MCS_LVGL_OBJECTS > 65535 || MCS_LVGL_EVENTS < 1 ||                     \
    MCS_LVGL_EVENTS > 65535
#error "MCS_LVGL_OBJECTS must be 2..65535; MCS_LVGL_EVENTS must be 1..65535"
#endif
#if LVGL_VERSION_MAJOR == 9
#define obj_delete lv_obj_delete
#define disp_delete lv_display_delete
#define screen_active lv_display_get_screen_active
typedef lv_display_t display_t;
#else
#define obj_delete lv_obj_del
#define disp_delete lv_disp_remove
#define screen_active lv_disp_get_scr_act
typedef lv_disp_t display_t;
#endif
#ifndef MCS_LVGL8_FONT_DECODER_FIX
#define MCS_LVGL8_FONT_DECODER_FIX 0
#endif
#if LVGL_VERSION_MAJOR == 8 && LV_USE_FONT_COMPRESSED
#if !MCS_LVGL8_FONT_DECODER_FIX
#error                                                                                             \
    "LVGL8.4 compressed fonts have a known negative-shift UB; use the explicit reviewed MICROCS_LVGL8_FONT_DECODER_FIX source build or a validated equivalent with linked marker, never suppress sanitizers"
#endif
extern uint32_t mcs_lvgl8_decoder_compat_version(void);
#endif
#define NATIVE(name)                                                                               \
    static mcs_value_t name(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv)
typedef struct gui gui_t;
typedef struct {
    gui_t *gui;
    lv_obj_t *obj;
    uint32_t generation;
    uint16_t index;
    const char *kind;
    char *retained[3];
    size_t retained_bytes[3];
} slot_t;
typedef struct {
    uint16_t index;
    uint32_t generation;
    int code;
} event_t;
struct gui {
    mcs_vm_t *vm;
    int owner_pin;
    unsigned handle_refs;
    display_t *display;
#if LVGL_VERSION_MAJOR == 8
    lv_disp_drv_t driver;
    lv_disp_draw_buf_t draw_buf;
    lv_indev_drv_t input_driver;
#endif
    lv_indev_t *input;
    int pointer_x, pointer_y;
    bool pressed;
    void *allocation;
    size_t allocation_bytes;
    void *pixels;
    size_t pixel_bytes;
    const mcs_lvgl_backend_t *backend;
    uint32_t serial, flushes, dropped, last_tick;
    mcs_idle_fn previous_idle;
    void *previous_idle_ud;
    uint64_t rendered_bytes;
    uint16_t read, count;
    int io_error;
    bool disposed, owns_init, busy;
    slot_t slots[MCS_LVGL_OBJECTS];
    event_t events[MCS_LVGL_EVENTS];
};
typedef struct {
    gui_t *gui;
    uint16_t index;
    uint32_t generation;
} object_t;
static gui_t *active_gui;
static uint32_t next_serial;
static const mcs_class_def_t gui_def, object_def;
static int gui_idle(mcs_vm_t *vm, void *ud);

static void release_assets(slot_t *s) {
    for (unsigned i = 0; i < 3; i++)
        if (s->retained[i]) {
            mcs_mem_realloc(s->gui->vm, s->retained[i], s->retained_bytes[i], 0);
            s->retained[i] = NULL;
            s->retained_bytes[i] = 0;
        }
}
static void release_deleted_assets(gui_t *g) {
    /* Never free borrowed text during an LVGL DELETE callback: later
     * native destructors/event listeners may still read it. */
    for (unsigned i = 0; i < MCS_LVGL_OBJECTS; i++)
        if (!g->slots[i].obj)
            release_assets(&g->slots[i]);
}
static void event_cb(lv_event_t *e) {
    slot_t *s = (slot_t *)lv_event_get_user_data(e);
    gui_t *g = s->gui;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        s->obj = NULL;
        s->generation++;
        return;
    }
    /* Queue externally meaningful interaction only; render/layout chatter
     * cannot exhaust the queue. Never enter the VM from an LVGL callback. */
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_PRESSED &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_FOCUSED && code != LV_EVENT_DEFOCUSED)
        return;
    if (g->count == MCS_LVGL_EVENTS) {
        g->dropped++;
        return;
    }
    unsigned i = (g->read + g->count) % MCS_LVGL_EVENTS;
    g->events[i].index = s->index;
    g->events[i].generation = s->generation;
    g->events[i].code = (int)code;
    g->count++;
}

static void dispose(gui_t *g) {
    if (!g || g->disposed)
        return;
    g->disposed = true; /* prevent reentry through a backend */
    void *idle_ud;
    if (mcs_get_idle(g->vm, &idle_ud) == gui_idle && idle_ud == g)
        mcs_set_idle(g->vm, g->previous_idle, g->previous_idle_ud);
    if (g->input) {
        lv_indev_delete(g->input);
        g->input = NULL;
    }
    if (g->display) {
        disp_delete(g->display);
        g->display = NULL;
    }
    for (unsigned i = 0; i < MCS_LVGL_OBJECTS; i++) {
        g->slots[i].obj = NULL;
        g->slots[i].generation++;
        release_assets(&g->slots[i]);
    }
    if (g->allocation) {
        mcs_mem_realloc(g->vm, g->allocation, g->allocation_bytes, 0);
        g->allocation = NULL;
        g->pixels = NULL;
        g->allocation_bytes = 0;
        g->pixel_bytes = 0;
    }
    g->count = 0;
    if (active_gui == g)
        active_gui = NULL;
    if (g->owns_init) {
        g->owns_init = false;
        lv_deinit();
    }
}
static void gui_fin(mcs_vm_t *vm, void *data) {
    (void)vm;
    dispose((gui_t *)data);
}
static void object_fin(mcs_vm_t *vm, void *data) {
    object_t *o = (object_t *)data;
    gui_t *g = o->gui;
    if (g && g->handle_refs && --g->handle_refs == 0) {
        if (g->owner_pin >= 0)
            mcs_unpin(vm, g->owner_pin);
        g->owner_pin = -1;
    }
    o->gui = NULL;
}
static gui_t *gui_self(mcs_vm_t *vm, mcs_value_t self) {
    gui_t *g = (gui_t *)mcs_check_userdata(vm, self, &gui_def);
    if (!g)
        return NULL;
    if (g->disposed || !g->display) {
        mcs_raise(vm, "ObjectDisposedException", "LvGui");
        return NULL;
    }
    if (g->busy) {
        mcs_raise(vm, "InvalidOperationException", "LVGL backend must not reenter the VM");
        return NULL;
    }
    return g;
}
static slot_t *object_self(mcs_vm_t *vm, mcs_value_t self) {
    object_t *o = (object_t *)mcs_check_userdata(vm, self, &object_def);
    if (!o)
        return NULL;
    gui_t *g = o->gui;
    if (!g || g->disposed || o->index >= MCS_LVGL_OBJECTS || !g->slots[o->index].obj ||
        g->slots[o->index].generation != o->generation) {
        mcs_raise(vm, "ObjectDisposedException", "LvObject was deleted or its GUI disposed");
        return NULL;
    }
    if (g->busy) {
        mcs_raise(vm, "InvalidOperationException", "LVGL backend must not reenter the VM");
        return NULL;
    }
    return &g->slots[o->index];
}
#define GUI()                                                                                      \
    gui_t *g = gui_self(vm, self);                                                                 \
    if (!g)                                                                                        \
    return mcs_null()
#define OBJ()                                                                                      \
    slot_t *s = object_self(vm, self);                                                             \
    if (!s)                                                                                        \
    return mcs_null()

static void object_ctor(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    object_t *o = (object_t *)mcs_userdata(self);
    o->gui = NULL;
    /* Internal construction also checks types/bounds: direct script calls
     * cannot forge raw pointers or bypass generation checks. */
    if (argc != 3 || !mcs_userdata_of(argv[0], &gui_def)) {
        mcs_raise(vm, "ArgumentException",
                  "LvObject is returned by LvGui/objects; no public raw-pointer constructor");
        return;
    }
    gui_t *g = gui_self(vm, argv[0]);
    if (!g)
        return;
    mcs_int_t index = mcs_to_int(vm, argv[1]), generation = mcs_to_int(vm, argv[2]);
    if (mcs_has_exception(vm))
        return;
    if (index < 0 || index >= MCS_LVGL_OBJECTS || !g->slots[index].obj ||
        g->slots[index].generation != (uint32_t)generation) {
        mcs_raise(vm, "ObjectDisposedException", "invalid LvObject handle");
        return;
    }
    o->index = (uint16_t)index;
    o->generation = (uint32_t)generation;
    /* One shared owner root, not one pin per widget. MCS_MAX_PINS defaults
     * to 16; a normal GUI must not exhaust it after sixteen handles. */
    if (!g->handle_refs) {
        g->owner_pin = mcs_pin(vm, argv[0]);
        if (g->owner_pin < 0) {
            mcs_raise(vm, "InvalidOperationException", "LVGL: MCS_MAX_PINS exhausted");
            return;
        }
    }
    g->handle_refs++;
    o->gui = g;
}
static const char *widget_kind(lv_obj_t *obj) {
#if LV_USE_ARC
    if (lv_obj_check_type(obj, &lv_arc_class))
        return "arc";
#endif
#if LV_USE_BAR
    if (lv_obj_check_type(obj, &lv_bar_class))
        return "bar";
#endif
#if LV_USE_SLIDER
    if (lv_obj_check_type(obj, &lv_slider_class))
        return "slider";
#endif
#if LV_USE_SWITCH
    if (lv_obj_check_type(obj, &lv_switch_class))
        return "switch";
#endif
#if LV_USE_CHECKBOX
    if (lv_obj_check_type(obj, &lv_checkbox_class))
        return "checkbox";
#endif
#if LV_USE_DROPDOWN
    if (lv_obj_check_type(obj, &lv_dropdown_class))
        return "dropdown";
#endif
#if LV_USE_ROLLER
    if (lv_obj_check_type(obj, &lv_roller_class))
        return "roller";
#endif
#if LV_USE_TEXTAREA
    if (lv_obj_check_type(obj, &lv_textarea_class))
        return "textarea";
#endif
#if LV_USE_TABLE
    if (lv_obj_check_type(obj, &lv_table_class))
        return "table";
#endif
#if LV_USE_CHART
    if (lv_obj_check_type(obj, &lv_chart_class))
        return "chart";
#endif
#if LV_USE_CANVAS
    if (lv_obj_check_type(obj, &lv_canvas_class))
        return "canvas";
#endif
#if LV_USE_LINE
    if (lv_obj_check_type(obj, &lv_line_class))
        return "line";
#endif
#if LV_USE_LIST
    if (lv_obj_check_type(obj, &lv_list_class))
        return "list";
#endif
#if LV_USE_MENU
    if (lv_obj_check_type(obj, &lv_menu_class))
        return "menu";
#endif
#if LV_USE_KEYBOARD
    if (lv_obj_check_type(obj, &lv_keyboard_class))
        return "keyboard";
#endif
#if LV_USE_SPINBOX
    if (lv_obj_check_type(obj, &lv_spinbox_class))
        return "spinbox";
#endif
#if LV_USE_LED
    if (lv_obj_check_type(obj, &lv_led_class))
        return "led";
#endif
#if LV_USE_CALENDAR
    if (lv_obj_check_type(obj, &lv_calendar_class))
        return "calendar";
#endif
#if LV_USE_TILEVIEW
    if (lv_obj_check_type(obj, &lv_tileview_class))
        return "tileview";
#endif
#if LV_USE_SPAN
    if (lv_obj_check_type(obj, &lv_spangroup_class))
        return "spangroup";
#endif
#if LV_USE_ANIMIMG
    if (lv_obj_check_type(obj, &lv_animimg_class))
        return "animimg";
#endif
#if LV_USE_LABEL
    if (lv_obj_check_type(obj, &lv_label_class))
        return "label";
#endif
#if LV_USE_TABVIEW
    if (lv_obj_check_type(obj, &lv_tabview_class))
        return "tabview";
#endif
#if LV_USE_WIN
    if (lv_obj_check_type(obj, &lv_win_class))
        return "win";
#endif
#if LV_USE_SPINNER
    if (lv_obj_check_type(obj, &lv_spinner_class))
        return "spinner";
#endif
#if LVGL_VERSION_MAJOR == 8
#if LV_USE_BTN
    if (lv_obj_check_type(obj, &lv_btn_class))
        return "button";
#endif
#if LV_USE_IMG
    if (lv_obj_check_type(obj, &lv_img_class))
        return "image";
#endif
#else
#if LV_USE_BUTTON
    if (lv_obj_check_type(obj, &lv_button_class))
        return "button";
#endif
#if LV_USE_IMAGE
    if (lv_obj_check_type(obj, &lv_image_class))
        return "image";
#endif
#endif
    return "object";
}
static mcs_value_t handle(mcs_vm_t *vm, mcs_value_t owner, gui_t *g, lv_obj_t *obj,
                          const char *kind) {
    if (!obj) {
        mcs_raise(vm, "OutOfMemoryException", "LVGL could not allocate an object");
        return mcs_null();
    }
    slot_t *s = NULL;
    for (unsigned i = 0; i < MCS_LVGL_OBJECTS; i++) {
        if (g->slots[i].obj == obj) {
            s = &g->slots[i];
            break;
        }
    }
    if (!s) {
        for (unsigned i = 0; i < MCS_LVGL_OBJECTS; i++)
            if (!g->slots[i].obj) {
                s = &g->slots[i];
                release_assets(s);
                s->gui = g;
                s->obj = obj;
                s->index = (uint16_t)i;
                s->generation++;
                s->kind = !strcmp(kind, "screen") ? "screen" : widget_kind(obj);
                lv_obj_add_event_cb(obj, event_cb, LV_EVENT_ALL, s);
                break;
            }
    }
    if (!s) {
        mcs_raise(vm, "InvalidOperationException", "LVGL: MCS_LVGL_OBJECTS exhausted");
        return mcs_null();
    }
    mcs_value_t a[3] = {owner, mcs_int(s->index), mcs_int(s->generation)}, out = mcs_null();
    mcs_result_t result = mcs_new_object(vm, "LvObject", 3, a, &out);
    if (result != MCS_OK) {
        /* The public guarded constructor can catch an OOM panic and return
         * an error without a pending script exception. Never turn it into a
         * successful null widget. Native callers must propagate the failure. */
        if (!mcs_has_exception(vm))
            mcs_raise(
                vm, result == MCS_ERR_MEMORY ? "OutOfMemoryException" : "InvalidOperationException",
                "LVGL wrapper handle allocation failed");
        return mcs_null();
    }
    return out;
}
static mcs_value_t owner_of(mcs_vm_t *vm, mcs_value_t self) {
    object_t *o = (object_t *)mcs_userdata(self);
    return mcs_pinned(vm, o->gui->owner_pin);
}

#if LVGL_VERSION_MAJOR == 9
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *pixels) {
    gui_t *g = (gui_t *)lv_display_get_user_data(d);
#else
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *pixels) {
    gui_t *g = (gui_t *)d->user_data;
#endif
#if LVGL_VERSION_MAJOR == 9
    size_t stride = lv_display_get_buf_active(d)->header.stride;
#else
    size_t stride = (size_t)(a->x2 - a->x1 + 1) * 2;
#endif
    size_t bytes = stride * (size_t)(a->y2 - a->y1 + 1);
    if (!g->disposed && g->backend && g->backend->flush) {
        int rc = g->backend->flush(g->backend->ud, a->x1, a->y1, a->x2 - a->x1 + 1,
                                   a->y2 - a->y1 + 1, pixels, stride, bytes);
        if (rc < 0 && !g->io_error)
            g->io_error = rc;
    }
    g->flushes++;
    g->rendered_bytes += bytes;
#if LVGL_VERSION_MAJOR == 9
    lv_display_flush_ready(d);
#else
    lv_disp_flush_ready(d);
#endif
}
#if LVGL_VERSION_MAJOR == 9
static void pointer_cb(lv_indev_t *d, lv_indev_data_t *data) {
    gui_t *g = (gui_t *)lv_indev_get_user_data(d);
#else
static void pointer_cb(lv_indev_drv_t *d, lv_indev_data_t *data) {
    gui_t *g = (gui_t *)d->user_data;
#endif
    if (g->backend && g->backend->read_pointer) {
        int x = g->pointer_x, y = g->pointer_y;
        bool pressed = g->pressed;
        int rc = g->backend->read_pointer(g->backend->ud, &x, &y, &pressed);
        if (rc < 0) {
            if (!g->io_error)
                g->io_error = rc;
            g->pressed = false;
        } else if (x < -32767 || x > 32767 || y < -32767 || y > 32767) {
            if (!g->io_error)
                g->io_error = -1;
            g->pressed = false;
        } else {
            g->pointer_x = x;
            g->pointer_y = y;
            g->pressed = pressed;
        }
    }
    data->point.x = g->pointer_x;
    data->point.y = g->pointer_y;
    data->state = g->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void gui_ctor(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    gui_t *g = (gui_t *)mcs_userdata(self);
    g->vm = vm;
    g->owner_pin = -1;
    if (argc != 2) {
        mcs_raise(vm, "ArgumentException", "LvGui(width, height)");
        return;
    }
    mcs_int_t w = mcs_to_int(vm, argv[0]), h = mcs_to_int(vm, argv[1]);
    if (mcs_has_exception(vm))
        return;
    if (w < 1 || h < 1 || w > 4096 || h > 4096) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "GUI dimensions must be 1..4096");
        return;
    }
    if (active_gui || lv_is_initialized()) {
        mcs_raise(vm, "InvalidOperationException",
                  "LVGL is global: dispose the existing GUI or external LVGL instance first");
        return;
    }
    lv_init();
    g->owns_init = true;
    active_gui = g;
    g->serial = ++next_serial;
    g->backend = (const mcs_lvgl_backend_t *)mcs_get_ext(vm, MCS_EXT_LVGL);
    unsigned lines = h < 16 ? (unsigned)h : 16;
#if LVGL_VERSION_MAJOR == 9
    g->pixel_bytes =
        (size_t)lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_RGB565) * lines;
#else
    g->pixel_bytes = (size_t)w * lines * sizeof(lv_color_t);
#endif
#if LVGL_VERSION_MAJOR == 9
#if LV_DRAW_BUF_ALIGN < 1 || LV_DRAW_BUF_ALIGN > 4096 ||                                           \
    (LV_DRAW_BUF_ALIGN & (LV_DRAW_BUF_ALIGN - 1))
#error "MicroCS LVGL: LV_DRAW_BUF_ALIGN must be a power of two in 1..4096"
#endif
    const size_t alignment = LV_DRAW_BUF_ALIGN;
#else
    const size_t alignment = sizeof(void *);
#endif
    g->allocation_bytes = g->pixel_bytes + alignment - 1;
    g->allocation = mcs_mem_realloc(vm, NULL, 0, g->allocation_bytes);
    g->pixels = (void *)(((uintptr_t)g->allocation + alignment - 1) & ~(uintptr_t)(alignment - 1));
#if LVGL_VERSION_MAJOR == 9
    g->display = lv_display_create((int32_t)w, (int32_t)h);
    if (!g->display) {
        dispose(g);
        mcs_raise(vm, "OutOfMemoryException", "LVGL display allocation");
        return;
    }
    lv_display_set_color_format(g->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(g->display, g);
    lv_display_set_flush_cb(g->display, flush_cb);
    lv_display_set_buffers(g->display, g->pixels, NULL, (uint32_t)g->pixel_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    lv_disp_draw_buf_init(&g->draw_buf, g->pixels, NULL, (uint32_t)w * lines);
    lv_disp_drv_init(&g->driver);
    g->driver.hor_res = (lv_coord_t)w;
    g->driver.ver_res = (lv_coord_t)h;
    g->driver.draw_buf = &g->draw_buf;
    g->driver.flush_cb = flush_cb;
    g->driver.user_data = g;
    g->display = lv_disp_drv_register(&g->driver);
    if (!g->display) {
        dispose(g);
        mcs_raise(vm, "OutOfMemoryException", "LVGL display allocation");
        return;
    }
#endif
#if LVGL_VERSION_MAJOR == 9
    g->input = lv_indev_create();
    if (g->input) {
        lv_indev_set_type(g->input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(g->input, g->display);
        lv_indev_set_user_data(g->input, g);
        lv_indev_set_read_cb(g->input, pointer_cb);
    }
#else
    lv_indev_drv_init(&g->input_driver);
    g->input_driver.type = LV_INDEV_TYPE_POINTER;
    g->input_driver.disp = g->display;
    g->input_driver.user_data = g;
    g->input_driver.read_cb = pointer_cb;
    g->input = lv_indev_drv_register(&g->input_driver);
#endif
    if (!g->input) {
        dispose(g);
        mcs_raise(vm, "OutOfMemoryException", "LVGL input allocation");
        return;
    }
    g->last_tick = mcs_ticks(vm);
    g->previous_idle = mcs_get_idle(vm, &g->previous_idle_ud);
    mcs_set_idle(vm, gui_idle, g);
}
NATIVE(gui_dispose) {
    (void)argc;
    (void)argv;
    gui_t *g = (gui_t *)mcs_check_userdata(vm, self, &gui_def);
    if (g && g->busy)
        mcs_raise(vm, "InvalidOperationException",
                  "LVGL backend must not dispose a GUI during flush/input");
    else
        dispose(g);
    return mcs_null();
}
NATIVE(gui_screen) {
    (void)argc;
    (void)argv;
    GUI();
    return handle(vm, self, g, screen_active(g->display), "screen");
}
NATIVE(gui_pump) {
    GUI();
    mcs_int_t elapsed = argc ? mcs_to_int(vm, argv[0]) : 0;
    if (mcs_has_exception(vm))
        return mcs_null();
    if (elapsed < 0 || elapsed > 1000) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "Pump elapsed milliseconds: 0..1000");
        return mcs_null();
    }
    lv_tick_inc((uint32_t)elapsed);
    g->last_tick = mcs_ticks(vm);
    g->busy = true;
    uint32_t wait = lv_timer_handler();
    g->busy = false;
    release_deleted_assets(g);
    if (g->io_error) {
        mcs_raise(vm, "IOException", "LVGL flush backend failed (%d)", g->io_error);
        return mcs_null();
    }
    return mcs_int(wait);
}
NATIVE(gui_flushes) {
    (void)argc;
    (void)argv;
    GUI();
    return mcs_int(g->flushes);
}
NATIVE(gui_dropped) {
    (void)argc;
    (void)argv;
    GUI();
    return mcs_int(g->dropped);
}
NATIVE(gui_backend) {
    (void)argc;
    (void)argv;
    GUI();
    return mcs_string(vm, g->backend && g->backend->flush ? "RGB565 sink" : "headless");
}
NATIVE(gui_version) {
    (void)self;
    (void)argc;
    (void)argv;
    return mcs_string(vm, LVGL_VERSION_MAJOR == 8 ? "8.4.0" : "9.4.0");
}
NATIVE(gui_poll) {
    (void)argc;
    (void)argv;
    GUI();
    while (g->count) {
        event_t ev = g->events[g->read];
        g->read = (g->read + 1) % MCS_LVGL_EVENTS;
        g->count--;
        slot_t *s = &g->slots[ev.index];
        if (!s->obj || s->generation != ev.generation)
            continue;
        mcs_value_t out = mcs_new_array(vm, 2);
        mcs_push_root(vm, out);
        mcs_set_index(out, 0, handle(vm, self, g, s->obj, s->kind));
        mcs_set_index(out, 1, mcs_int(ev.code));
        mcs_pop_root(vm, 1);
        return out;
    }
    return mcs_null();
}

/* Constructors with a common semantic contract. Special constructors are
 * explicitly adapted rather than cast across incompatible signatures. */
static lv_obj_t *create_widget(const char *name, lv_obj_t *parent) {
    if (!strcmp(name, "object"))
        return lv_obj_create(parent);
#define WIDGET(n, macro, fn) /* entries below are guarded by actual lv_conf */
#if LV_USE_LABEL
    if (!strcmp(name, "label"))
        return lv_label_create(parent);
#endif
#if LVGL_VERSION_MAJOR == 8
#if LV_USE_BTN
    if (!strcmp(name, "button"))
        return lv_btn_create(parent);
#endif
#if LV_USE_IMG
    if (!strcmp(name, "image"))
        return lv_img_create(parent);
#endif
#else
#if LV_USE_BUTTON
    if (!strcmp(name, "button"))
        return lv_button_create(parent);
#endif
#if LV_USE_IMAGE
    if (!strcmp(name, "image"))
        return lv_image_create(parent);
#endif
#endif
#include "mcs_lvgl_widgets.inc"
    return NULL;
}
NATIVE(object_create) {
    (void)argc;
    OBJ();
    const char *name = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm))
        return mcs_null();
    unsigned free_slots = 0;
    for (unsigned i = 0; i < MCS_LVGL_OBJECTS; i++)
        if (!s->gui->slots[i].obj)
            free_slots++;
    if (!free_slots) {
        mcs_raise(vm, "InvalidOperationException", "LVGL: MCS_LVGL_OBJECTS exhausted");
        return mcs_null();
    }
    lv_obj_t *obj = create_widget(name, s->obj);
    if (!obj) {
        mcs_raise(vm, "NotSupportedException",
                  "LVGL widget '%s' is disabled, unsupported here, or could not be allocated",
                  name);
        return mcs_null();
    }
    /* handle() infers the actual class and stores only static kind names. */
    mcs_value_t out = handle(vm, owner_of(vm, self), s->gui, obj, "object");
    if (mcs_is_null(out)) {
        obj_delete(obj);
        release_deleted_assets(s->gui);
    }
    return out;
}
NATIVE(object_delete) {
    (void)argc;
    (void)argv;
    OBJ();
    if (!strcmp(s->kind, "screen")) {
        mcs_raise(vm, "InvalidOperationException",
                  "Dispose LvGui instead of deleting its active screen");
        return mcs_null();
    }
    obj_delete(s->obj);
    release_deleted_assets(s->gui);
    return mcs_null();
}
NATIVE(object_clean) {
    (void)argc;
    (void)argv;
    OBJ();
    lv_obj_clean(s->obj);
    release_deleted_assets(s->gui);
    return mcs_null();
}
NATIVE(object_kind) {
    (void)argc;
    (void)argv;
    OBJ();
    return mcs_string(vm, s->kind);
}
static bool coords(mcs_vm_t *vm, int argc, mcs_value_t *argv, int *x, int *y) {
    if (argc != 2) {
        mcs_raise(vm, "ArgumentException", "expected two coordinates");
        return false;
    }
    mcs_int_t a = mcs_to_int(vm, argv[0]), b = mcs_to_int(vm, argv[1]);
    if (mcs_has_exception(vm))
        return false;
    if (a < -32767 || a > 32767 || b < -32767 || b > 32767) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "coordinates -32767..32767");
        return false;
    }
    *x = (int)a;
    *y = (int)b;
    return true;
}
NATIVE(gui_input) {
    GUI();
    if (argc != 3) {
        mcs_raise(vm, "ArgumentException", "Input(x, y, pressed)");
        return mcs_null();
    }
    int x, y;
    if (!coords(vm, 2, argv, &x, &y))
        return mcs_null();
    bool pressed = mcs_to_bool(vm, argv[2]);
    if (mcs_has_exception(vm))
        return mcs_null();
    if (g->backend && g->backend->read_pointer) {
        mcs_raise(vm, "InvalidOperationException",
                  "Input is owned by the configured pointer backend");
        return mcs_null();
    }
    g->pointer_x = x;
    g->pointer_y = y;
    g->pressed = pressed;
    return mcs_null();
}
NATIVE(object_size) {
    OBJ();
    int x, y;
    if (coords(vm, argc, argv, &x, &y))
        lv_obj_set_size(s->obj, x, y);
    return mcs_null();
}
NATIVE(object_pos) {
    OBJ();
    int x, y;
    if (coords(vm, argc, argv, &x, &y))
        lv_obj_set_pos(s->obj, x, y);
    return mcs_null();
}
NATIVE(object_text_set) {
    (void)argc;
    OBJ();
    const char *text = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm))
        return mcs_null();
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label")) {
        lv_label_set_text(s->obj, text);
        return mcs_null();
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea")) {
        lv_textarea_set_text(s->obj, text);
        return mcs_null();
    }
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(s->kind, "checkbox")) {
        lv_checkbox_set_text(s->obj, text);
        return mcs_null();
    }
#endif
    mcs_raise(vm, "NotSupportedException",
              "Text is supported on enabled label/textarea/checkbox widgets");
    return mcs_null();
}
NATIVE(object_text_get) {
    (void)argc;
    (void)argv;
    OBJ();
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label"))
        return mcs_string(vm, lv_label_get_text(s->obj));
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea"))
        return mcs_string(vm, lv_textarea_get_text(s->obj));
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(s->kind, "checkbox"))
        return mcs_string(vm, lv_checkbox_get_text(s->obj));
#endif
    mcs_raise(vm, "NotSupportedException",
              "Text is supported on enabled label/textarea/checkbox widgets");
    return mcs_null();
}
NATIVE(object_value_set) {
    (void)argc;
    OBJ();
    mcs_int_t val = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm))
        return mcs_null();
    if (val < -32768 || val > 32767) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "Value -32768..32767");
        return mcs_null();
    }
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider")) {
        lv_slider_set_value(s->obj, (int32_t)val, LV_ANIM_OFF);
        return mcs_null();
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar")) {
        lv_bar_set_value(s->obj, (int32_t)val, LV_ANIM_OFF);
        return mcs_null();
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc")) {
        lv_arc_set_value(s->obj, (int32_t)val);
        return mcs_null();
    }
#endif
    mcs_raise(vm, "NotSupportedException", "Value is supported on enabled slider/bar/arc widgets");
    return mcs_null();
}
NATIVE(object_value_get) {
    (void)argc;
    (void)argv;
    OBJ();
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider"))
        return mcs_int(lv_slider_get_value(s->obj));
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar"))
        return mcs_int(lv_bar_get_value(s->obj));
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc"))
        return mcs_int(lv_arc_get_value(s->obj));
#endif
    mcs_raise(vm, "NotSupportedException", "Value is supported on enabled slider/bar/arc widgets");
    return mcs_null();
}
NATIVE(object_range) {
    OBJ();
    int lo, hi;
    if (!coords(vm, argc, argv, &lo, &hi))
        return mcs_null();
    if (lo >= hi) {
        mcs_raise(vm, "ArgumentException", "range minimum must be below maximum");
        return mcs_null();
    }
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider")) {
        lv_slider_set_range(s->obj, lo, hi);
        return mcs_null();
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar")) {
        lv_bar_set_range(s->obj, lo, hi);
        return mcs_null();
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc")) {
        lv_arc_set_range(s->obj, lo, hi);
        return mcs_null();
    }
#endif
    mcs_raise(vm, "NotSupportedException", "SetRange: enabled slider/bar/arc only");
    return mcs_null();
}
NATIVE(object_width) {
    (void)argc;
    (void)argv;
    OBJ();
    lv_obj_update_layout(s->obj);
    return mcs_int(lv_obj_get_width(s->obj));
}
NATIVE(object_height) {
    (void)argc;
    (void)argv;
    OBJ();
    lv_obj_update_layout(s->obj);
    return mcs_int(lv_obj_get_height(s->obj));
}
NATIVE(object_children) {
    (void)argc;
    (void)argv;
    OBJ();
    return mcs_int(lv_obj_get_child_cnt(s->obj));
}
NATIVE(object_child) {
    (void)argc;
    OBJ();
    mcs_int_t i = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm))
        return mcs_null();
    if (i < 0 || (uint64_t)i >= lv_obj_get_child_cnt(s->obj)) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "Child index");
        return mcs_null();
    }
    return handle(vm, owner_of(vm, self), s->gui, lv_obj_get_child(s->obj, (int32_t)i), "object");
}
NATIVE(object_parent) {
    (void)argc;
    (void)argv;
    OBJ();
    lv_obj_t *p = lv_obj_get_parent(s->obj);
    return p ? handle(vm, owner_of(vm, self), s->gui, p, "object") : mcs_null();
}
static bool state_value(mcs_vm_t *vm, mcs_value_t value, lv_state_t *out) {
    mcs_int_t state = mcs_to_int(vm, value);
    if (mcs_has_exception(vm))
        return false;
    const uint32_t mask = LV_STATE_CHECKED | LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY |
                          LV_STATE_EDITED | LV_STATE_HOVERED | LV_STATE_PRESSED |
                          LV_STATE_SCROLLED | LV_STATE_DISABLED | LV_STATE_USER_1 |
                          LV_STATE_USER_2 | LV_STATE_USER_3 | LV_STATE_USER_4;
    if (state < 0 || ((uint64_t)state & ~(uint64_t)mask)) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "unsupported LVGL state bits");
        return false;
    }
    *out = (lv_state_t)state;
    return true;
}
NATIVE(object_state_add) {
    (void)argc;
    OBJ();
    lv_state_t state;
    if (state_value(vm, argv[0], &state))
        lv_obj_add_state(s->obj, state);
    return mcs_null();
}
NATIVE(object_state_clear) {
    (void)argc;
    OBJ();
    lv_state_t state;
    if (state_value(vm, argv[0], &state))
        lv_obj_clear_state(s->obj, state);
    return mcs_null();
}
NATIVE(object_event) {
    (void)argc;
    OBJ();
    mcs_int_t code = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm))
        return mcs_null();
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_PRESSED &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_FOCUSED && code != LV_EVENT_DEFOCUSED) {
        mcs_raise(vm, "ArgumentException",
                  "SendEvent accepts exposed interaction event constants only");
        return mcs_null();
    }
#if LVGL_VERSION_MAJOR == 9
    lv_obj_send_event(s->obj, (lv_event_code_t)code, NULL);
#else
    lv_event_send(s->obj, (lv_event_code_t)code, NULL);
#endif
    return mcs_null();
}
NATIVE(object_style) {
    OBJ();
    if (argc < 2 || argc > 3) {
        mcs_raise(vm, "ArgumentException", "SetStyle(property, integerOrRgb[, selector])");
        return mcs_null();
    }
    const char *name = mcs_to_cstr(vm, argv[0]);
    mcs_int_t value = mcs_to_int(vm, argv[1]);
    mcs_int_t selector = argc == 3 ? mcs_to_int(vm, argv[2]) : 0;
    if (mcs_has_exception(vm))
        return mcs_null();
    if (selector < 0 || (uint64_t)selector > UINT32_MAX) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "style selector");
        return mcs_null();
    }
#include "mcs_lvgl_styles.inc"
    mcs_raise(vm, "NotSupportedException",
              "LVGL style '%s' is not bound; see versioned coverage inventory", name);
    return mcs_null();
}

/* Built-ins are static const library objects, not script-owned assets. The
 * LV_FONT_DEFAULT pointer must likewise remain valid for the firmware lifetime.
 * No font deletion, filesystem loader, raw pointer or borrowed script bytes. */
static const lv_font_t *static_font(const char *name) {
#if LVGL_VERSION_MAJOR == 8 && LV_USE_FONT_COMPRESSED
    if (mcs_lvgl8_decoder_compat_version() != UINT32_C(0x08040001))
        return NULL; /* No unchecked or mismatched decoder provenance. */
#endif
    if (!strcmp(name, "Default"))
        return LV_FONT_DEFAULT;
#include "mcs_lvgl_fonts.inc"
    return NULL;
}
static const char *font_name(mcs_vm_t *vm, mcs_value_t value) {
    if (!mcs_is_string(value) || mcs_strlen(value) > 80 ||
        strlen(mcs_cstr(value)) != mcs_strlen(value)) {
        mcs_raise(vm, "ArgumentException", "Font name must be a string <=80bytes without NUL");
        return NULL;
    }
    return mcs_cstr(value);
}
NATIVE(gui_has_font) {
    (void)self;
    (void)argc;
    const char *name = font_name(vm, argv[0]);
    return name ? mcs_bool(static_font(name) != NULL) : mcs_null();
}
NATIVE(object_font) {
    OBJ();
    if (argc < 1 || argc > 2) {
        mcs_raise(vm, "ArgumentException", "SetFont(name[, selector])");
        return mcs_null();
    }
    const char *name = font_name(vm, argv[0]);
    if (!name)
        return mcs_null();
    if (argc == 2 && !mcs_is_int(argv[1])) {
        mcs_raise(vm, "ArgumentException", "Font selector must be an integer");
        return mcs_null();
    }
    mcs_int_t selector = argc == 2 ? argv[1].as.i : 0;
    if (selector < 0 || (uint64_t)selector > UINT32_MAX) {
        mcs_raise(vm, "ArgumentOutOfRangeException", "font style selector");
        return mcs_null();
    }
    const lv_font_t *font = static_font(name);
    if (!font) {
        mcs_raise(vm, "NotSupportedException",
                  "LVGL font '%s' is unavailable in this configuration/version", name);
        return mcs_null();
    }
    lv_obj_set_style_text_font(s->obj, font, (lv_style_selector_t)selector);
    return mcs_null();
}
NATIVE(object_font_height) {
    (void)argc;
    (void)argv;
    OBJ();
    const lv_font_t *font = lv_obj_get_style_text_font(s->obj, LV_PART_MAIN);
    if (!font) {
        mcs_raise(vm, "InvalidOperationException",
                  "No native LVGL font for current main-part style");
        return mcs_null();
    }
    return mcs_int(lv_font_get_line_height(font));
}

/* These upstream setters retain the pointer. Copy into VM-accounted storage,
 * then keep it until replacement or AFTER native object deletion completes. */
static char *copy_retained(mcs_vm_t *vm, mcs_value_t value) {
    const char *text = mcs_to_cstr(vm, value);
    if (mcs_has_exception(vm))
        return NULL;
    size_t bytes = strlen(text) + 1;
    char *copy = (char *)mcs_mem_realloc(vm, NULL, 0, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
static void replace_retained(slot_t *s, unsigned index, char *text) {
    if (s->retained[index])
        mcs_mem_realloc(s->gui->vm, s->retained[index], s->retained_bytes[index], 0);
    s->retained[index] = text;
    s->retained_bytes[index] = strlen(text) + 1;
}
static bool retained_method(mcs_vm_t *vm, slot_t *s, const char *method, int argc,
                            mcs_value_t *argv) {
    unsigned index = 0;
    bool recognized = false;
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") &&
        (!strcmp(method, "set_text") || !strcmp(method, "set_symbol"))) {
        recognized = true;
        index = !strcmp(method, "set_symbol") ? 1 : 0;
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") &&
        (!strcmp(method, "set_accepted_chars") || !strcmp(method, "set_password_bullet"))) {
        recognized = true;
        index = !strcmp(method, "set_password_bullet") ? 1 : 0;
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "set_text_static"))
        recognized = true;
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(s->kind, "checkbox") && !strcmp(method, "set_text_static"))
        recognized = true;
#endif
    if (!recognized)
        return false;
    if (argc != 2) {
        mcs_raise(vm, "ArgumentException", "%s.%s: expected one text argument", s->kind, method);
        return true;
    }
    char *copy = copy_retained(vm, argv[1]);
    if (!copy)
        return true;
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown")) {
        if (index)
            lv_dropdown_set_symbol(s->obj, copy);
        else
            lv_dropdown_set_text(s->obj, copy);
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea")) {
        if (index)
            lv_textarea_set_password_bullet(s->obj, copy);
        else
            lv_textarea_set_accepted_chars(s->obj, copy);
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label"))
        lv_label_set_text_static(s->obj, copy);
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(s->kind, "checkbox"))
        lv_checkbox_set_text_static(s->obj, copy);
#endif
    replace_retained(s, index, copy);
    return true;
}
static bool number_range(int64_t value, int64_t lo, int64_t hi) {
    return value >= lo && value <= hi;
}
NATIVE(object_call) {
    OBJ();
    if (argc < 1 || argc > 8) {
        mcs_raise(vm, "ArgumentException", "Call(method, arguments...)");
        return mcs_null();
    }
    const char *method = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm))
        return mcs_null();
    if (retained_method(vm, s, method, argc, argv))
        return mcs_null();
#include "mcs_lvgl_calls.inc"
    mcs_raise(vm, "NotSupportedException",
              "LVGL %s.%s is disabled or not bound; see versioned coverage inventory", s->kind,
              method);
    return mcs_null();
}

static const mcs_reg_t lv_gui_members[] = {MCS_GET("Screen", gui_screen),
                                           MCS_FN("Pump", gui_pump, 1),
                                           MCS_FN("PollEvent", gui_poll, 0),
                                           MCS_FN("Input", gui_input, 3),
                                           MCS_GET("DroppedEvents", gui_dropped),
                                           MCS_GET("FlushCount", gui_flushes),
                                           MCS_GET("Backend", gui_backend),
                                           MCS_FN("Dispose", gui_dispose, 0),
                                           MCS_REG_END};
static const mcs_reg_t lv_gui_statics[] = {MCS_GET("Version", gui_version),
                                           MCS_FN("HasFont", gui_has_font, 1), MCS_REG_END};
static const mcs_reg_t lv_object_members[] = {MCS_FN("Create", object_create, 1),
                                              MCS_FN("Delete", object_delete, 0),
                                              MCS_FN("Clean", object_clean, 0),
                                              MCS_GET("Kind", object_kind),
                                              MCS_GET("Text", object_text_get),
                                              MCS_SET("Text", object_text_set),
                                              MCS_GET("Value", object_value_get),
                                              MCS_SET("Value", object_value_set),
                                              MCS_FN("SetRange", object_range, 2),
                                              MCS_FN("SetSize", object_size, 2),
                                              MCS_FN("SetPos", object_pos, 2),
                                              MCS_FN("SetStyle", object_style, -1),
                                              MCS_FN("SetFont", object_font, -1),
                                              MCS_GET("FontHeight", object_font_height),
                                              MCS_GET("Width", object_width),
                                              MCS_GET("Height", object_height),
                                              MCS_GET("Parent", object_parent),
                                              MCS_GET("ChildCount", object_children),
                                              MCS_FN("Child", object_child, 1),
                                              MCS_FN("AddState", object_state_add, 1),
                                              MCS_FN("ClearState", object_state_clear, 1),
                                              MCS_FN("SendEvent", object_event, 1),
                                              MCS_FN("Call", object_call, -1),
                                              MCS_REG_END};
static const mcs_const_t lv_consts[] = {
    MCS_CONST("Clicked", LV_EVENT_CLICKED), MCS_CONST("ValueChanged", LV_EVENT_VALUE_CHANGED),
    MCS_CONST("Pressed", LV_EVENT_PRESSED), MCS_CONST("Released", LV_EVENT_RELEASED),
    MCS_CONST("Focused", LV_EVENT_FOCUSED), MCS_CONST("Defocused", LV_EVENT_DEFOCUSED),
    MCS_CONST("Checked", LV_STATE_CHECKED), MCS_CONST("Disabled", LV_STATE_DISABLED),
    MCS_CONST("PartMain", LV_PART_MAIN),    MCS_CONST("PartIndicator", LV_PART_INDICATOR),
    MCS_CONST("PartKnob", LV_PART_KNOB),    MCS_CONST_END};
static const mcs_class_def_t gui_def = {"LvGui", sizeof(gui_t),  gui_ctor,
                                        gui_fin, lv_gui_members, lv_gui_statics};
static const mcs_class_def_t object_def = {"LvObject", sizeof(object_t),  object_ctor,
                                           object_fin, lv_object_members, NULL};
void mcs_lvgl_open_lib(mcs_vm_t *vm) {
    mcs_register_class(vm, &gui_def);
    mcs_register_class(vm, &object_def);
    mcs_register_consts(vm, "LvGui", lv_consts);
}
int mcs_lvgl_poll(mcs_vm_t *vm) {
    gui_t *g = active_gui;
    if (!g || g->vm != vm || g->disposed || g->busy)
        return 0;
    if (g->io_error)
        return -1;
    uint32_t now = mcs_ticks(vm), elapsed = now - g->last_tick;
    if (!elapsed)
        return 0;
    g->last_tick = now;
    lv_tick_inc(elapsed);
    g->busy = true;
    lv_timer_handler();
    g->busy = false;
    release_deleted_assets(g);
    return g->io_error ? -1 : 0;
}
static int gui_idle(mcs_vm_t *vm, void *ud) {
    gui_t *g = (gui_t *)ud;
    if (mcs_lvgl_poll(vm) < 0) {
        mcs_request_abort(vm);
        return -1;
    }
    return g->previous_idle ? g->previous_idle(vm, g->previous_idle_ud) : 0;
}
uint32_t mcs_lvgl_scope_begin(mcs_vm_t *vm) {
    (void)vm;
    return next_serial;
}
void mcs_lvgl_scope_end(mcs_vm_t *vm, uint32_t checkpoint) {
    if (active_gui && active_gui->vm == vm && (int32_t)(active_gui->serial - checkpoint) > 0)
        dispose(active_gui);
}
int mcs_lvgl_set_backend(mcs_vm_t *vm, const mcs_lvgl_backend_t *backend) {
    if (active_gui && active_gui->vm == vm)
        return -1;
    mcs_set_ext(vm, MCS_EXT_LVGL, (void *)backend);
    return 0;
}
#else
typedef int mcs_lvgl_disabled_t;
#endif
