/* Host-owned LVGL: no initialization, timer, display, or global registry. */
#include "mcs_lvgl.h"
#if MCS_ENABLE_LVGL
#include <string.h>
#include <limits.h>
#include "mcs_lvgl_compat.h"

typedef struct session session_t;
#if LV_USE_GRID || LV_USE_LINE || LV_USE_TEXTAREA || LV_USE_DROPDOWN || MCS_LV_USE_BUTTONMATRIX
#if LVGL_VERSION_MAJOR == 8
typedef lv_coord_t mcs_grid_coord_t;
typedef lv_point_t mcs_line_point_t;
#else
typedef int32_t mcs_grid_coord_t;
typedef lv_point_precise_t mcs_line_point_t;
#endif
typedef struct retained_buffer {
    struct retained_buffer* next;
    unsigned slot;
    uint32_t generation;
    unsigned kind, split, count;
    union { mcs_line_point_t point; mcs_grid_coord_t coord; const char* text; } alignment;
    unsigned char data[]; /* aligned raw storage; avoids typed-array aliasing */
} retained_buffer_t;
#endif
typedef struct {
    session_t* session;
    lv_obj_t* obj;
    uint32_t generation, subscription;
    int pin;
    uint8_t kind, spin_digits; /* share the former int-kind storage, no slot growth */
    lv_font_t* owned_font;
    lv_event_code_t filter;
} slot_t;
typedef struct { unsigned slot; uint32_t generation, subscription; } queued_t;
struct session {
    mcs_lvgl_config_t host;
    lv_obj_t* container;
    slot_t slots[MCS_LVGL_MAX_OBJECTS];
    queued_t queue[MCS_LVGL_EVENT_QUEUE];
    unsigned head, count;
    uint32_t dropped;
    bool dispatching, reap_pending;
#if LV_USE_GRID || LV_USE_LINE || LV_USE_TEXTAREA || LV_USE_DROPDOWN || MCS_LV_USE_BUTTONMATRIX
    retained_buffer_t* buffers; /* lazy buffers; no per-slot pointer or reservation */
#endif
};
typedef struct { unsigned slot; uint32_t generation; } handle_t;
static const mcs_class_def_t object_def;
void mcs_lvgl_vm_cleanup(mcs_vm_t* vm);
static const char* const kinds[] = { "Object", "Label", "Button", "Slider", "Bar", "Checkbox", "Switch", "Arc", "Image", "Dropdown", "Roller", "TextArea", "Table", "Led", "Spinbox", "Chart", "Canvas", "Keyboard", "ButtonMatrix", "Line", "List", "Calendar", "ImageButton", "Spinner", "Tileview" };
static bool supported(int k) {
    switch (k) {
    case 0: return true;
    case 1: return LV_USE_LABEL != 0;
    case 2: return MCS_LV_USE_BUTTON != 0;
    case 3: return LV_USE_SLIDER != 0;
    case 4: return LV_USE_BAR != 0;
    case 5: return LV_USE_CHECKBOX != 0;
    case 6: return LV_USE_SWITCH != 0;
    case 7: return LV_USE_ARC != 0;
    case 8: return MCS_LV_USE_IMAGE != 0;
#if LV_USE_DROPDOWN
    case 9: return true;
#endif
#if LV_USE_ROLLER
    case 10: return true;
#endif
#if LV_USE_TEXTAREA
    case 11: return true;
#endif
#if LV_USE_TABLE
    case 12: return true;
#endif
#if LV_USE_LED
    case 13: return true;
#endif
#if LV_USE_SPINBOX
    case 14: return true;
#endif
#if LV_USE_CHART
    case 15: return true;
#endif
#if LV_USE_CANVAS
    case 16: return true;
#endif
#if LV_USE_KEYBOARD
    case 17: return true;
#endif
#if MCS_LV_USE_BUTTONMATRIX
    case 18: return true;
#endif
#if LV_USE_LINE
    case 19: return true;
#endif
#if LV_USE_LIST
    case 20: return true;
#endif
#if LV_USE_CALENDAR
    case 21: return true;
#endif
#if MCS_LV_USE_IMAGEBUTTON
    case 22: return true;
#endif
#if LV_USE_SPINNER
    case 23: return true;
#endif
#if LV_USE_TILEVIEW
    case 24: return true;
#endif
    default: return false;
    }
}
static int kind_id(const char* name) {
    for (int i = 0; i < (int)(sizeof kinds/sizeof kinds[0]); ++i) if (!strcmp(name, kinds[i])) return i;
    return -1;
}
static session_t* session(mcs_vm_t* vm) {
    session_t* s = mcs_get_ext(vm, MCS_EXT_LVGL);
    if (!s) mcs_raise(vm, "InvalidOperationException", "LVGL host is not attached");
    else if (!s->host.is_vm_thread(s->host.ctx)) {
        mcs_raise(vm, "InvalidOperationException", "LVGL call requires the VM owner thread");
        return NULL;
    }
    return s;
}
static void drop_image(slot_t* r) {
#if MCS_LV_USE_IMAGE
    if(r->obj && r->kind==8) {
        const void* src=mcs_lv_image_get_src(r->obj);
        if(src)mcs_lv_image_cache_drop(src); /* never drop all host caches via NULL */
    }
#else
    (void)r;
#endif
}
/* Called ONLY while holding host lock; no VM access, allocation, or dispatch. */
static void event_cb(lv_event_t* e) {
    slot_t* r = lv_event_get_user_data(e);
    session_t* s = r->session;
    lv_event_code_t code = lv_event_get_code(e);
    if (lv_event_get_target(e) != r->obj) return; /* ignore bubbled child events */
    if (code == LV_EVENT_DELETE) {
#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
        /* LVGL 8's keyboard retains a raw textarea pointer without observing
         * deletion. Detach dependents before the target storage is destroyed. */
        if(r->kind==11 || r->kind==14)for(unsigned i=0;i<MCS_LVGL_MAX_OBJECTS;++i) {
            slot_t* k=&s->slots[i];
            if(k->obj && k->kind==17 && lv_keyboard_get_textarea(k->obj)==r->obj)lv_keyboard_set_textarea(k->obj,NULL);
        }
#endif
        drop_image(r); r->obj = NULL; s->reap_pending=true; return;
    }
    if (r->pin < 0 || code != r->filter) return;
    if (s->count == MCS_LVGL_EVENT_QUEUE) {
        if (s->dropped != UINT32_MAX) ++s->dropped;
        return; /* drop newest, never allocate */
    }
    unsigned tail = (s->head + s->count) % MCS_LVGL_EVENT_QUEUE;
    s->queue[tail] = (queued_t){ (unsigned)(r - s->slots), r->generation, r->subscription };
    ++s->count;
}
static void root_deleted(lv_event_t* e) {
    session_t* s = lv_event_get_user_data(e);
    if (lv_event_get_target(e) == s->container) s->container = NULL;
}
/* VM thread + host lock; retire delegates after host-side deletion, before reuse. */
static void reap(mcs_vm_t* vm, session_t* s) {
    if(!s->reap_pending)return;
    s->reap_pending=false;
    for (unsigned i = 0; i < MCS_LVGL_MAX_OBJECTS; ++i) {
        slot_t* r = &s->slots[i];
        /* Defer font destruction until the complete native delete traversal has
         * returned: children may still inherit the parent's font in destructors. */
        if (!r->obj && r->owned_font) { mcs_lv_font_destroy(r->owned_font); r->owned_font=NULL; }
        if (!r->obj && r->pin >= 0) { mcs_unpin(vm, r->pin); r->pin = -1; }
    }
#if LV_USE_GRID || LV_USE_LINE || LV_USE_TEXTAREA || LV_USE_DROPDOWN || MCS_LV_USE_BUTTONMATRIX
    retained_buffer_t** link=&s->buffers;
    while(*link) {
        retained_buffer_t* b=*link;slot_t* r=&s->slots[b->slot];
        if(!r->obj || r->generation!=b->generation){*link=b->next;s->host.free(s->host.ctx,b);}
        else link=&b->next;
    }
#endif
}
static handle_t* handle(mcs_vm_t* vm, mcs_value_t self) {
    return mcs_check_userdata(vm, self, &object_def);
}
/* host lock held. Generation prevents pointer reuse from reviving old handles. */
static slot_t* lookup(session_t* s, handle_t* h) {
    if (!h || !h->generation || h->slot >= MCS_LVGL_MAX_OBJECTS) return NULL;
    slot_t* r = &s->slots[h->slot];
    return r->generation == h->generation && r->obj ? r : NULL;
}
static void invalid(mcs_vm_t* vm) {
    mcs_raise(vm, "InvalidOperationException", "LVGL object is deleted or disposed");
}
static lv_obj_t* create(int k, lv_obj_t* p) {
    switch (k) {
    case 0: return lv_obj_create(p);
#if LV_USE_LABEL
    case 1: return lv_label_create(p);
#endif
#if MCS_LV_USE_BUTTON
    case 2: return mcs_lv_button_create(p);
#endif
#if LV_USE_SLIDER
    case 3: return lv_slider_create(p);
#endif
#if LV_USE_BAR
    case 4: return lv_bar_create(p);
#endif
#if LV_USE_CHECKBOX
    case 5: return lv_checkbox_create(p);
#endif
#if LV_USE_SWITCH
    case 6: return lv_switch_create(p);
#endif
#if LV_USE_ARC
    case 7: return lv_arc_create(p);
#endif
#if MCS_LV_USE_IMAGE
    case 8: return mcs_lv_image_create(p);
#endif
#if LV_USE_DROPDOWN
    case 9: return lv_dropdown_create(p);
#endif
#if LV_USE_ROLLER
    case 10: return lv_roller_create(p);
#endif
#if LV_USE_TEXTAREA
    case 11: return lv_textarea_create(p);
#endif
#if LV_USE_TABLE
    case 12: return lv_table_create(p);
#endif
#if LV_USE_LED
    case 13: return lv_led_create(p);
#endif
#if LV_USE_SPINBOX
    case 14: { lv_obj_t* o=lv_spinbox_create(p);if(o)lv_spinbox_set_digit_format(o,5,0);return o; }
#endif
#if LV_USE_CHART
    case 15: return lv_chart_create(p);
#endif
#if LV_USE_CANVAS
    case 16: return lv_canvas_create(p);
#endif
#if LV_USE_KEYBOARD
    case 17: return lv_keyboard_create(p);
#endif
#if MCS_LV_USE_BUTTONMATRIX
    case 18: return mcs_lv_buttonmatrix_create(p);
#endif
#if LV_USE_LINE
    case 19: return lv_line_create(p);
#endif
#if LV_USE_LIST
    case 20: return mcs_lv_list_create(p);
#endif
#if LV_USE_CALENDAR
    case 21: return lv_calendar_create(p);
#endif
#if MCS_LV_USE_IMAGEBUTTON
    case 22: return mcs_lv_imagebutton_create(p);
#endif
#if LV_USE_SPINNER
    case 23: return mcs_lv_spinner_create(p);
#endif
#if LV_USE_TILEVIEW
    case 24: return lv_tileview_create(p);
#endif
    default: return NULL;
    }
}
static void ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    if (argc < 1 || argc > 2) { mcs_raise(vm,"ArgumentException","new LvObject(kind[, parent])"); return; }
    const char* name = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return;
    int k = kind_id(name);
    if (!supported(k)) { mcs_raise(vm,"NotSupportedException","LVGL widget unavailable: %s", name); return; }
    handle_t* h = handle(vm, self);
    handle_t* ph = argc == 2 && !mcs_is_null(argv[1]) ? handle(vm, argv[1]) : NULL;
    if (mcs_has_exception(vm)) return;
    session_t* s = session(vm); if (!s) return;
    int error = 0;
    s->host.lock(s->host.ctx);
    reap(vm, s);
    slot_t* parent = ph ? lookup(s, ph) : NULL;
    lv_obj_t* p = ph ? (parent ? parent->obj : NULL) : s->container;
    if (!p) error = 1;
    else {
        unsigned i;
        for (i = 0; i < MCS_LVGL_MAX_OBJECTS; ++i)
            if (!s->slots[i].obj && s->slots[i].generation != UINT32_MAX) break;
        if (i == MCS_LVGL_MAX_OBJECTS) error = 2;
        else {
            slot_t* r = &s->slots[i];
            ++r->generation; r->kind = (uint8_t)k; r->spin_digits = 5; r->subscription = 0;
            r->obj = create(k, p);
            if (!r->obj) error = 3;
            else if (!mcs_lv_add_event(r->obj, event_cb, LV_EVENT_ALL, r)) {
                mcs_lv_obj_delete(r->obj); r->obj = NULL; error = 3;
            } else { h->slot = i; h->generation = r->generation; }
        }
    }
    s->host.unlock(s->host.ctx);
    if (error) mcs_raise(vm, "InvalidOperationException", "%s", error == 1 ? "LVGL root/parent is not ready" : error == 2 ? "LVGL object capacity exhausted" : "LVGL native allocation failed");
}
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define UNUSED (void)argc; (void)argv
#define BEGIN handle_t* h = handle(vm,self); if (!h) return mcs_null(); session_t* s = session(vm); if (!s) return mcs_null(); s->host.lock(s->host.ctx); slot_t* r = lookup(s,h)
#define END s->host.unlock(s->host.ctx); if (!r) invalid(vm); return mcs_null()
NATIVE(dispose) {
    UNUSED; BEGIN;
    if (r) mcs_lv_obj_delete(r->obj);
    reap(vm,s);
    s->host.unlock(s->host.ctx);
    return mcs_null(); /* deleted and double Dispose are harmless */
}
NATIVE(alive) {
    UNUSED; BEGIN;
    bool yes = r != NULL;
    s->host.unlock(s->host.ctx); return mcs_bool(yes);
}
NATIVE(center) { UNUSED; BEGIN; if (r) lv_obj_center(r->obj); END; }
static bool integer(mcs_vm_t* vm, mcs_value_t v, int32_t* out, int32_t lo, int32_t hi) {
    mcs_int_t n = mcs_to_int(vm,v);
    if (mcs_has_exception(vm)) return false;
    if (n < lo || n > hi) { mcs_raise(vm,"ArgumentException","LVGL number out of range"); return false; }
    *out = (int32_t)n; return true;
}
static mcs_value_t geometry(mcs_vm_t* vm, mcs_value_t self, mcs_value_t* argv, bool size) {
    int32_t a,b;
    if (!integer(vm,argv[0],&a,size ? 0 : -32767,32767) || !integer(vm,argv[1],&b,size ? 0 : -32767,32767)) return mcs_null();
    BEGIN;
    if (r) { if (size) lv_obj_set_size(r->obj,a,b); else lv_obj_set_pos(r->obj,a,b); }
    END;
}
NATIVE(set_size) { (void)argc; return geometry(vm,self,argv,true); }
NATIVE(set_pos) { (void)argc; return geometry(vm,self,argv,false); }
static mcs_value_t color(mcs_vm_t* vm, mcs_value_t self, mcs_value_t* argv, bool bg) {
    int32_t rgb; if (!integer(vm,argv[0],&rgb,0,0xffffff)) return mcs_null();
    BEGIN;
    if (r) {
        if (bg) { lv_obj_set_style_bg_color(r->obj,lv_color_hex((uint32_t)rgb),0); lv_obj_set_style_bg_opa(r->obj,LV_OPA_COVER,0); }
        else lv_obj_set_style_text_color(r->obj,lv_color_hex((uint32_t)rgb),0);
    }
    END;
}
NATIVE(set_bg) { (void)argc; return color(vm,self,argv,true); }
NATIVE(set_fg) { (void)argc; return color(vm,self,argv,false); }
NATIVE(set_text) {
    (void)argc;
    const char* text = mcs_to_cstr(vm,argv[0]); if (mcs_has_exception(vm)) return mcs_null();
    (void)text; /* optional text widgets may both be disabled */
    bool ok = false; BEGIN;
    if (r) {
#if LV_USE_LABEL
        if (r->kind == 1) { lv_label_set_text(r->obj,text); ok = true; }
#endif
#if LV_USE_CHECKBOX
        if (r->kind == 5) { lv_checkbox_set_text(r->obj,text); ok = true; }
#endif
#if LV_USE_TEXTAREA
        if (r->kind == 11 || r->kind == 14) { lv_textarea_set_text(r->obj,text); ok = true; }
#endif
    }
    s->host.unlock(s->host.ctx);
    if (!r) invalid(vm);
    else if (!ok) mcs_raise(vm,"NotSupportedException","Text requires Label, Checkbox, TextArea or Spinbox");
    return mcs_null();
}
NATIVE(set_font) {
    (void)argc;
    const char* name=mcs_to_cstr(vm,argv[0]);if(mcs_has_exception(vm))return mcs_null();
    int error=0; BEGIN;
    if(r) {
        const lv_font_t* font=NULL;lv_font_t* owned=NULL;
        if(!strcmp(name,"Default"))font=LV_FONT_DEFAULT;
        else if(name[0] && name[1]==':') {
            if(!lv_fs_get_drv(name[0]))error=1;
            else {
                owned=mcs_lv_font_load(name);font=owned;if(!font)error=2;
#if !LV_USE_FONT_COMPRESSED
                /* Binary loaders can accept compressed data even when native
                 * glyph decompression is off. Reject before any rendering. */
                if(owned && ((const lv_font_fmt_txt_dsc_t*)owned->dsc)->bitmap_format!=LV_FONT_FMT_TXT_PLAIN) {
                    mcs_lv_font_destroy(owned);owned=NULL;font=NULL;error=4;
                }
#endif
            }
        } else {
            for(size_t i=0;i<s->host.font_count;++i)
                if(s->host.fonts[i].name && !strcmp(name,s->host.fonts[i].name)){font=s->host.fonts[i].font;break;}
            if(!font)error=3;
        }
        if(font) {
            lv_font_t* old=r->owned_font;
            lv_obj_set_style_text_font(r->obj,font,0);
            r->owned_font=owned;
            if(old)mcs_lv_font_destroy(old);
        }
    }
    s->host.unlock(s->host.ctx);
    if(!r)invalid(vm);
    else if(error)mcs_raise(vm,(error==3 || error==4) ? "NotSupportedException" : "IOException","%s",error==1 ? "LVGL font drive is not registered" : error==2 ? "LVGL binary font could not be loaded" : error==4 ? "Binary font requires host LV_USE_FONT_COMPRESSED" : "LVGL native font name is not registered");
    return mcs_null();
}
NATIVE(set_source) {
    (void)argc;
    const char* path=mcs_to_cstr(vm,argv[0]);if(mcs_has_exception(vm))return mcs_null();
    if(!path[0] || path[1]!=':'){mcs_raise(vm,"ArgumentException","Image Source requires an LVGL file path, e.g. S:/logo.bin");return mcs_null();}
    int error=0; BEGIN;
    if(r) {
#if MCS_LV_USE_IMAGE
        if(r->kind!=8)error=1;
        else if(!lv_fs_get_drv(path[0]))error=2;
        else {
#if LVGL_VERSION_MAJOR == 8
            lv_img_header_t header={0};
            bool valid=lv_img_decoder_get_info(path,&header)==LV_RES_OK;
#else
            lv_image_header_t header={0};
            bool valid=lv_image_decoder_get_info(path,&header)==LV_RESULT_OK;
#endif
            /* Early LVGL 9.0 bin decoder can return OK after an open failure.
             * A usable file image must still have nonzero dimensions. */
            if(!valid || !header.w || !header.h)error=3;
            else { drop_image(r);mcs_lv_image_set_src(r->obj,path); }
        }
#else
        error=1;
#endif
    }
    s->host.unlock(s->host.ctx);
    if(!r)invalid(vm);
    else if(error)mcs_raise(vm,error==1 ? "NotSupportedException" : "IOException","%s",error==1 ? "Source requires an enabled Image widget" : error==2 ? "LVGL image drive is not registered" : "LVGL image header could not be decoded");
    return mcs_null();
}
static mcs_value_t value(mcs_vm_t* vm, mcs_value_t self, mcs_value_t* argv, bool set) {
    int32_t n = 0; bool ok = false;
    if (set && !integer(vm,argv[0],&n,0,100)) return mcs_null();
    BEGIN;
    if (r) switch (r->kind) {
#if LV_USE_SLIDER
    case 3: if (set) lv_slider_set_value(r->obj,n,LV_ANIM_OFF); else n=lv_slider_get_value(r->obj); ok=true; break;
#endif
#if LV_USE_BAR
    case 4: if (set) lv_bar_set_value(r->obj,n,LV_ANIM_OFF); else n=lv_bar_get_value(r->obj); ok=true; break;
#endif
#if LV_USE_ARC
    case 7: if (set) lv_arc_set_value(r->obj,n); else n=lv_arc_get_value(r->obj); ok=true; break;
#endif
    default: break;
    }
    s->host.unlock(s->host.ctx);
    if (!r) invalid(vm); else if (!ok) mcs_raise(vm,"NotSupportedException","Value requires Slider, Bar or Arc");
    return mcs_int(n);
}
NATIVE(set_value) { (void)argc; return value(vm,self,argv,true); }
NATIVE(get_value) { UNUSED; return value(vm,self,NULL,false); }
static mcs_value_t checked(mcs_vm_t* vm, mcs_value_t self, mcs_value_t* argv, bool set) {
    bool n = set ? mcs_to_bool(vm,argv[0]) : false;
    if (mcs_has_exception(vm)) return mcs_null();
    bool ok = false; BEGIN;
    if (r && (r->kind == 5 || r->kind == 6)) {
        ok = true;
        if (!set) n = lv_obj_has_state(r->obj,LV_STATE_CHECKED);
        else if (n) lv_obj_add_state(r->obj,LV_STATE_CHECKED);
        else mcs_lv_obj_remove_state(r->obj,LV_STATE_CHECKED);
    }
    s->host.unlock(s->host.ctx);
    if (!r) invalid(vm); else if (!ok) mcs_raise(vm,"NotSupportedException","Checked requires Checkbox or Switch");
    return mcs_bool(n);
}
NATIVE(set_checked) { (void)argc; return checked(vm,self,argv,true); }
NATIVE(get_checked) { UNUSED; return checked(vm,self,NULL,false); }
static mcs_value_t subscribe(mcs_vm_t* vm, mcs_value_t self, mcs_value_t callback, lv_event_code_t code) {
    handle_t* h = handle(vm,self); if (!h) return mcs_null();
    session_t* s = session(vm); if (!s) return mcs_null();
    int pin = mcs_is_null(callback) ? -1 : mcs_pin(vm, callback);
    if (!mcs_is_null(callback) && pin < 0) { mcs_raise(vm,"InvalidOperationException","LVGL callback pin capacity exhausted"); return mcs_null(); }
    bool exhausted = false;
    s->host.lock(s->host.ctx);
    reap(vm,s);
    slot_t* r = lookup(s,h);
    if (r) {
        if (r->subscription == UINT32_MAX) exhausted = true;
        else {
            if (r->pin >= 0) mcs_unpin(vm,r->pin);
            r->pin = pin; r->filter = code; ++r->subscription;
        }
    }
    s->host.unlock(s->host.ctx);
    if (!r || exhausted) {
        if (pin >= 0) mcs_unpin(vm,pin);
        if (!r) invalid(vm); else mcs_raise(vm,"InvalidOperationException","LVGL subscription generation exhausted");
    }
    return mcs_null();
}
NATIVE(on_clicked) { (void)argc; return subscribe(vm,self,argv[0],LV_EVENT_CLICKED); }
NATIVE(on_changed) { (void)argc; return subscribe(vm,self,argv[0],LV_EVENT_VALUE_CHANGED); }
NATIVE(clear_event) { UNUSED; return subscribe(vm,self,mcs_null(),LV_EVENT_ALL); }
NATIVE(on_event) {
    (void)argc;int32_t event;if(!integer(vm,argv[0],&event,0,INT32_MAX))return mcs_null();
    switch(event) {
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_SHORT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
    case LV_EVENT_LONG_PRESSED_REPEAT:
    case LV_EVENT_CLICKED:
    case LV_EVENT_RELEASED:
    case LV_EVENT_SCROLL_BEGIN:
    case LV_EVENT_SCROLL_END:
    case LV_EVENT_FOCUSED:
    case LV_EVENT_DEFOCUSED:
    case LV_EVENT_LEAVE:
    case LV_EVENT_VALUE_CHANGED:
    case LV_EVENT_READY:
    case LV_EVENT_CANCEL:
        return subscribe(vm,self,argv[1],(lv_event_code_t)event);
    default:mcs_raise(vm,"NotSupportedException","Only documented queued input/value events are supported; no synchronous draw/delete callbacks");return mcs_null();
    }
}

NATIVE(ready) {
    (void)self; UNUSED; session_t* s=session(vm); if (!s) return mcs_null();
    s->host.lock(s->host.ctx); bool yes=s->container != NULL; s->host.unlock(s->host.ctx); return mcs_bool(yes);
}
NATIVE(supports) {
    (void)self; (void)argc;
    const char* name=mcs_to_cstr(vm,argv[0]); if (mcs_has_exception(vm)) return mcs_null();
    return mcs_bool(supported(kind_id(name)));
}
NATIVE(dropped) {
    (void)self; UNUSED; session_t* s=session(vm); if (!s) return mcs_null();
    s->host.lock(s->host.ctx); uint32_t n=s->dropped; s->host.unlock(s->host.ctx);
#if !MCS_INT64
    if (n > INT32_MAX) n = INT32_MAX; /* do not wrap the public count negative */
#endif
    return mcs_int((mcs_int_t)n);
}
NATIVE(dispatch) {
    (void)self; (void)argc;
    int32_t limit; if (!integer(vm,argv[0],&limit,0,MCS_LVGL_EVENT_QUEUE)) return mcs_null();
    session_t* s=session(vm); if (!s) return mcs_null();
    if (s->dispatching) { mcs_raise(vm,"InvalidOperationException","LVGL Dispatch cannot re-enter"); return mcs_null(); }
    s->dispatching=true;
    /* Dispatch(0) is also an explicit VM-side retirement point. */
    s->host.lock(s->host.ctx);reap(vm,s);s->host.unlock(s->host.ctx);
    int called=0;
    for (int i=0;i<limit;++i) {
        mcs_value_t cb=mcs_null();
        s->host.lock(s->host.ctx); reap(vm,s);
        bool empty=s->count == 0;
        if (!empty) {
            queued_t q=s->queue[s->head]; s->head=(s->head+1)%MCS_LVGL_EVENT_QUEUE; --s->count;
            slot_t* r=&s->slots[q.slot];
            if (r->obj && r->generation==q.generation && r->subscription==q.subscription && r->pin>=0) cb=mcs_pinned(vm,r->pin);
        }
        s->host.unlock(s->host.ctx);
        if (empty) break;
        if (!mcs_is_null(cb)) {
            /* mcs_call_value roots its callee. No LVGL lock held across VM calls. */
            mcs_value_t result;
            ++called;
            if (mcs_call_value(vm,cb,0,NULL,&result)!=MCS_OK) break;
        }
    }
    s->dispatching=false;
    return mcs_int(called);
}
/* Checked, table-driven native API. Stack-only argument preparation; no VM
 * allocation while holding the GUI lock. Generated calls are explicit C, not FFI. */
#include "mcs_native_internal.h"
enum { NERR_UNAVAILABLE=1,NERR_KIND,NERR_DELETED,NERR_CYCLE,NERR_RANGE,NERR_MEMORY,NERR_TEXT };
enum { NRET_NULL,NRET_INT,NRET_BOOL,NRET_TEXT };
enum { NARG_NUMBER,NARG_BOOL,NARG_COLOR,NARG_SELECTOR,NARG_PART,NARG_FLAG,NARG_STATE,NARG_COORDINATE,NARG_FLEX_FLOW };
typedef union { int32_t n;const char* text;handle_t* object; } native_arg_t;
typedef struct { native_arg_t v[6]; } native_args_t;
typedef struct { int kind;int64_t number;char* text;size_t length; } native_result_t;
static bool native_arg_error(mcs_vm_t* vm,const char* message) {
    mcs_raise(vm,"ArgumentException","%s",message);return false;
}
static bool native_number_arg(mcs_vm_t* vm,mcs_value_t v,int32_t* out,int32_t lo,int32_t hi,int kind) {
    if(v.type!=MCS_T_INT && v.type!=MCS_T_CHAR && v.type!=MCS_T_BOOL)return native_arg_error(vm,"LVGL scalar must be an integer or boolean");
    mcs_int_t n=mcs_to_int(vm,v);if(mcs_has_exception(vm))return false;
    if(kind==NARG_COORDINATE && n>=INT32_MIN && n<=INT32_MAX) {
        if(n==LV_SIZE_CONTENT){*out=(int32_t)n;return true;}
        if(LV_COORD_IS_PCT((int32_t)n)) {
            int32_t pct=LV_COORD_GET_PCT((int32_t)n);
            if(pct < -1000 || pct > 1000 || LV_PCT(pct)!=(int32_t)n)return native_arg_error(vm,"LVGL encoded percentage exceeds -1000..1000");
            *out=(int32_t)n;return true;
        }
    }
    if(n<lo || n>hi)return native_arg_error(vm,"LVGL scalar out of documented range");
    if((kind==NARG_STATE || kind==NARG_SELECTOR) && ((uint32_t)n & 0xffffu & ~0xf0ffu))return native_arg_error(vm,"Unknown LVGL state bits");
    if(kind==NARG_PART && ((uint32_t)n & 0xffffu))return native_arg_error(vm,"Expected only a native LVGL part");
    if(kind==NARG_FLEX_FLOW && ((uint32_t)n & ~13u))return native_arg_error(vm,"Unknown LVGL flex flow bits");
    *out=(int32_t)n;return true;
}
static bool native_text_arg(mcs_vm_t* vm,mcs_value_t v,const char** text) {
    if(!mcs_is_string(v))return native_arg_error(vm,"LVGL text requires a non-null string");
    if(mcs_strlen(v)>MCS_LVGL_MAX_TEXT_BYTES)return native_arg_error(vm,"LVGL text exceeds MCS_LVGL_MAX_TEXT_BYTES");
    *text=mcs_cstr(v);return true;
}
static bool native_object_arg(mcs_vm_t* vm,mcs_value_t v,handle_t** h,bool nullable) {
    if(nullable && mcs_is_null(v)){*h=NULL;return true;}
    *h=mcs_userdata_of(v,&object_def);
    if(!*h)return native_arg_error(vm,"LVGL object argument requires an LvObject handle");
    return true;
}
static int native_copy_text(session_t* s,const char* text,native_result_t* out) {
    if(!text)return 0;
    size_t n=0;while(n<=MCS_LVGL_MAX_TEXT_BYTES && text[n])++n;
    if(n>MCS_LVGL_MAX_TEXT_BYTES)return NERR_TEXT;
    out->text=s->host.alloc(s->host.ctx,n+1);if(!out->text)return NERR_MEMORY;
    memcpy(out->text,text,n+1);out->length=n;out->kind=NRET_TEXT;return 0;
}
#if MCS_LV_USE_BUTTONMATRIX
static unsigned native_button_count(lv_obj_t* obj) {
    const char* const* map=mcs_lv_buttonmatrix_map(obj);unsigned count=0;
    for(size_t i=0;map && map[i] && map[i][0];++i)if(strcmp(map[i],"\n"))++count;
    return count;
}
static bool native_button_index(lv_obj_t* obj,unsigned index) {return index<native_button_count(obj);}
#endif
#if LV_USE_SPINBOX
static int32_t native_spin_limit(unsigned digits) {
    int32_t power=1;while(digits--)power*=10;return power-1;
}
#endif
#if LV_USE_GRID
static bool native_grid_bounds(session_t*,lv_obj_t*,int32_t,int32_t,int32_t,int32_t);
static bool native_grid_existing(session_t*,lv_obj_t*,lv_obj_t*,int,int32_t);
#endif
#include "mcs_lvgl_buffers.inc"
#include "mcs_lvgl_api.inc"
typedef struct { mcs_vm_t* vm; native_result_t* native; mcs_value_t value; } native_string_t;
static void native_string_body(void* context) {
    native_string_t* c=context;c->value=mcs_string_n(c->vm,c->native->text,c->native->length);
}
static int native_api_id(mcs_vm_t* vm,mcs_value_t value) {
    if(mcs_is_int(value)) {
        mcs_int_t n=mcs_to_int(vm,value);return n>=0 && n<MCS_LV_API_COUNT ? (int)n : -1;
    }
    if(!mcs_is_string(value)){native_arg_error(vm,"LVGL API requires a native name or a cached API ID");return -1;}
    return native_api_find(mcs_cstr(value));
}
NATIVE(native_call) {
    (void)native_copy_text; /* optional text widgets can all be compiled out */
    if(argc<1 || argc>7){native_arg_error(vm,"Call requires an API name/ID and up to six arguments");return mcs_null();}
    int id=native_api_id(vm,argv[0]);if(mcs_has_exception(vm))return mcs_null();
    if(!native_api_available(id)){mcs_raise(vm,"NotSupportedException","LVGL API is not wrapped or disabled in the host version/configuration");return mcs_null();}
    native_args_t args={0};native_result_t result={0};
    if(!native_api_prepare(vm,id,argc-1,argv+1,&args))return mcs_null();
    BEGIN;
    int error=r ? native_api_execute(s,r,id,&args,&result) : NERR_DELETED;
    reap(vm,s);
    s->host.unlock(s->host.ctx);
    if(error) {
        if(result.text)s->host.free(s->host.ctx,result.text);
        mcs_raise(vm,error==NERR_UNAVAILABLE || error==NERR_KIND ? "NotSupportedException" : error==NERR_MEMORY ? "OutOfMemoryException" : error==NERR_DELETED ? "InvalidOperationException" : "ArgumentException","%s",
            error==NERR_KIND ? "LVGL API does not apply to this widget/argument kind" : error==NERR_DELETED ? "LVGL object argument is deleted" : error==NERR_CYCLE ? "LVGL reparenting would create a cycle" : error==NERR_RANGE ? "LVGL index/capacity out of range" : error==NERR_MEMORY ? "LVGL native text allocation failed" : error==NERR_TEXT ? "LVGL text snapshot exceeds configured bound" : "LVGL API unavailable");
        return mcs_null();
    }
    if(result.kind==NRET_TEXT) {
        native_string_t context={vm,&result,mcs_null()};
        int panic=mcs_protect_native(vm,native_string_body,&context);
        s->host.free(s->host.ctx,result.text);mcs_rethrow_native(vm,panic);return context.value;
    }
    if(result.kind==NRET_BOOL)return mcs_bool(result.number!=0);
    if(result.kind==NRET_INT) {
#if !MCS_INT64
        if(result.number<INT32_MIN || result.number>INT32_MAX){mcs_raise(vm,"OverflowException","LVGL result requires MCS_INT64");return mcs_null();}
#endif
        return mcs_int((mcs_int_t)result.number);
    }
    return mcs_null();
}
NATIVE(native_api) {
    (void)self;(void)argc;if(!session(vm))return mcs_null();
    if(!mcs_is_string(argv[0])){native_arg_error(vm,"Api requires a native API name");return mcs_null();}
    int id=native_api_find(mcs_cstr(argv[0]));
    if(!native_api_available(id)){mcs_raise(vm,"NotSupportedException","LVGL API is not wrapped or disabled");return mcs_null();}
    return mcs_int(id);
}
NATIVE(native_has_api) {
    (void)self;(void)argc;if(!session(vm))return mcs_null();
    if(!mcs_is_string(argv[0])){native_arg_error(vm,"HasApi requires a native API name");return mcs_null();}
    return mcs_bool(native_api_available(native_api_find(mcs_cstr(argv[0]))));
}
static mcs_value_t native_constant(mcs_vm_t* vm,mcs_value_t value,bool query) {
    session_t* s=session(vm);if(!s)return mcs_null();
    if(!mcs_is_string(value)){native_arg_error(vm,"Constant requires a native constant name");return mcs_null();}
    int32_t n=0;s->host.lock(s->host.ctx);int found=native_constant_lookup(mcs_cstr(value),&n);s->host.unlock(s->host.ctx);
    if(query)return mcs_bool(found!=0);
    if(!found){mcs_raise(vm,"NotSupportedException","LVGL constant is not wrapped or disabled");return mcs_null();}
    return mcs_int(n);
}
NATIVE(native_get_constant) { (void)self;(void)argc;return native_constant(vm,argv[0],false); }
NATIVE(native_has_constant) { (void)self;(void)argc;return native_constant(vm,argv[0],true); }
NATIVE(native_percent) {
    (void)self;(void)argc;if(!session(vm))return mcs_null();int32_t n;
    if(!integer(vm,argv[0],&n,-1000,1000))return mcs_null();
    return mcs_int(LV_PCT(n));
}

NATIVE(native_get_text) {
    UNUSED;BEGIN;
    const char* name=NULL;
    if(r)switch(r->kind){case 1:name="lv_label_get_text";break;case 5:name="lv_checkbox_get_text";break;case 11:case 14:name="lv_textarea_get_text";break;default:break;}
    s->host.unlock(s->host.ctx);
    if(!r){invalid(vm);return mcs_null();}
    if(!name){mcs_raise(vm,"NotSupportedException","Text requires Label, Checkbox, TextArea or Spinbox");return mcs_null();}
    mcs_value_t id=mcs_int(native_api_find(name));return native_call(vm,self,1,&id);
}

static const mcs_reg_t members[] = {
    MCS_FN("Call",native_call,-1),
    MCS_FN("SetButtonMap",set_button_map,1), MCS_FN("SetButtonControls",set_button_controls,1), MCS_GET("ButtonCount",button_count),
    MCS_FN("SetGrid",set_grid,2), MCS_FN("SetPoints",set_points,1), MCS_GET("PointCount",point_count),
    MCS_FN("Dispose",dispose,0), MCS_GET("IsAlive",alive), MCS_FN("Center",center,0),
    MCS_FN("SetSize",set_size,2), MCS_FN("SetPosition",set_pos,2),
    MCS_FN("SetBackground",set_bg,1), MCS_FN("SetTextColor",set_fg,1), MCS_SET("Text",set_text), MCS_GET("Text",native_get_text),
    MCS_FN("SetFont",set_font,1), MCS_SET("Source",set_source),
    MCS_GET("Value",get_value), MCS_SET("Value",set_value),
    MCS_GET("Checked",get_checked), MCS_SET("Checked",set_checked),
    MCS_FN("OnEvent",on_event,2), MCS_FN("OnClicked",on_clicked,1), MCS_FN("OnChanged",on_changed,1), MCS_FN("ClearEvent",clear_event,0), MCS_REG_END
};
static const mcs_class_def_t object_def = { "LvObject",sizeof(handle_t),ctor,NULL,members,NULL };
static const mcs_reg_t statics[] = {
    MCS_FN("GridFraction",grid_fraction,1),
    MCS_FN("Api",native_api,1), MCS_FN("HasApi",native_has_api,1),
    MCS_FN("Constant",native_get_constant,1), MCS_FN("HasConstant",native_has_constant,1), MCS_FN("Percent",native_percent,1),
    MCS_GET("Ready",ready), MCS_FN("Supports",supports,1), MCS_GET("DroppedEvents",dropped), MCS_FN("Dispatch",dispatch,1), MCS_REG_END
};
/* Internal guarded registration entrypoint, compiled only with this module. */
extern mcs_result_t mcs_lvgl_register_api(mcs_vm_t*, void (*)(mcs_vm_t*));
static void open_api(mcs_vm_t* vm) {
    mcs_register_class(vm,&object_def);
    mcs_register_module(vm,"Lvgl",statics);
}
size_t mcs_lvgl_storage_size(void) { return sizeof(session_t); }
int mcs_lvgl_attach(mcs_vm_t* vm, const mcs_lvgl_config_t* c) {
    if (!vm || !c || !c->root || !c->lock || !c->unlock || !c->is_vm_thread || !c->alloc || !c->free || (c->font_count && !c->fonts) || !c->is_vm_thread(c->ctx) || mcs_running(vm)) return MCS_LVGL_INVALID;
    if (mcs_get_ext(vm,MCS_EXT_LVGL)) return MCS_LVGL_ALREADY_ATTACHED;
    session_t* s=c->alloc(c->ctx,sizeof(*s)); if (!s) return MCS_LVGL_NO_MEMORY;
    memset(s,0,sizeof(*s)); s->host=*c;
    for (unsigned i=0;i<MCS_LVGL_MAX_OBJECTS;++i) { s->slots[i].session=s; s->slots[i].pin=-1; }
    c->lock(c->ctx);
    bool ready=mcs_lv_initialized() && mcs_lv_display_get_next(NULL) && lv_obj_is_valid(c->root);
    bool observed=false;
    if (ready) {
        s->container=lv_obj_create(c->root);
        if (s->container) {
            observed=mcs_lv_add_event(s->container,root_deleted,LV_EVENT_DELETE,s);
            if (!observed) { mcs_lv_obj_delete(s->container); s->container=NULL; }
            else {
                /* A private transparent subtree; never register on a host object.
                 * This also avoids retaining movable LVGL 8 event descriptors. */
                lv_obj_remove_style_all(s->container);
                lv_obj_set_size(s->container,lv_pct(100),lv_pct(100));
                mcs_lv_obj_remove_flag(s->container,LV_OBJ_FLAG_SCROLLABLE);
            }
        }
    }
    c->unlock(c->ctx);
    if (!observed) { c->free(c->ctx,s); return ready ? MCS_LVGL_NO_MEMORY : MCS_LVGL_NOT_READY; }
    mcs_set_ext(vm,MCS_EXT_LVGL,s); /* publish before fallible API registration */
    if (mcs_lvgl_register_api(vm,open_api) != MCS_OK) {
        mcs_lvgl_vm_cleanup(vm);
        return MCS_LVGL_NO_MEMORY;
    }
    return MCS_LVGL_OK;
}
void mcs_lvgl_vm_cleanup(mcs_vm_t* vm) {
    session_t* s=mcs_get_ext(vm,MCS_EXT_LVGL); if (!s) return;
    s->host.lock(s->host.ctx);
    s->count=0;
    /* Children invalidate themselves through delete callbacks. Never delete root. */
    for (unsigned i=0;i<MCS_LVGL_MAX_OBJECTS;++i) if (s->slots[i].obj) mcs_lv_obj_delete(s->slots[i].obj);
    if (s->container) mcs_lv_obj_delete(s->container);
    reap(vm,s);
    mcs_set_ext(vm,MCS_EXT_LVGL,NULL);
    s->host.unlock(s->host.ctx);
    s->host.free(s->host.ctx,s);
}
#endif
