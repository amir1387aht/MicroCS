/* Host-owned LVGL: no initialization, timer, display, or global registry. */
#include "mcs_lvgl.h"
#if MCS_ENABLE_LVGL
#include <string.h>
#include <limits.h>
#include "mcs_lvgl_compat.h"

typedef struct session session_t;
typedef struct {
    session_t* session;
    lv_obj_t* obj;
    uint32_t generation, subscription;
    int pin, kind;
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
    bool dispatching;
};
typedef struct { unsigned slot; uint32_t generation; } handle_t;
static const mcs_class_def_t object_def;
void mcs_lvgl_vm_cleanup(mcs_vm_t* vm);
static const char* const kinds[] = { "Object", "Label", "Button", "Slider", "Bar", "Checkbox", "Switch", "Arc" };
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
    default: return false;
    }
}
static int kind_id(const char* name) {
    for (int i = 0; i < 8; ++i) if (!strcmp(name, kinds[i])) return i;
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
/* Called ONLY while holding host lock; no VM access, allocation, or dispatch. */
static void event_cb(lv_event_t* e) {
    slot_t* r = lv_event_get_user_data(e);
    session_t* s = r->session;
    lv_event_code_t code = lv_event_get_code(e);
    if (lv_event_get_target(e) != r->obj) return; /* ignore bubbled child events */
    if (code == LV_EVENT_DELETE) { r->obj = NULL; return; }
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
    for (unsigned i = 0; i < MCS_LVGL_MAX_OBJECTS; ++i) {
        slot_t* r = &s->slots[i];
        if (!r->obj && r->pin >= 0) { mcs_unpin(vm, r->pin); r->pin = -1; }
    }
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
            ++r->generation; r->kind = k; r->subscription = 0;
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
    }
    s->host.unlock(s->host.ctx);
    if (!r) invalid(vm);
    else if (!ok) mcs_raise(vm,"NotSupportedException","Text requires Label or Checkbox");
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
static const mcs_reg_t members[] = {
    MCS_FN("Dispose",dispose,0), MCS_GET("IsAlive",alive), MCS_FN("Center",center,0),
    MCS_FN("SetSize",set_size,2), MCS_FN("SetPosition",set_pos,2),
    MCS_FN("SetBackground",set_bg,1), MCS_FN("SetTextColor",set_fg,1), MCS_SET("Text",set_text),
    MCS_GET("Value",get_value), MCS_SET("Value",set_value),
    MCS_GET("Checked",get_checked), MCS_SET("Checked",set_checked),
    MCS_FN("OnClicked",on_clicked,1), MCS_FN("OnChanged",on_changed,1), MCS_FN("ClearEvent",clear_event,0), MCS_REG_END
};
static const mcs_class_def_t object_def = { "LvObject",sizeof(handle_t),ctor,NULL,members,NULL };
static const mcs_reg_t statics[] = {
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
    if (!vm || !c || !c->root || !c->lock || !c->unlock || !c->is_vm_thread || !c->alloc || !c->free || !c->is_vm_thread(c->ctx) || mcs_running(vm)) return MCS_LVGL_INVALID;
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
