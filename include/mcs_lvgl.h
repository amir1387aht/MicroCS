/* Optional binding to a host-owned LVGL 8.x/9.x instance. See docs/LVGL.md. */
#ifndef MCS_LVGL_H
#define MCS_LVGL_H
#include "mcs.h"
#if MCS_ENABLE_LVGL
#ifdef MCS_LVGL_INCLUDE
#include MCS_LVGL_INCLUDE
#elif defined(__has_include)
#if __has_include(<lvgl.h>)
#include <lvgl.h>
#elif __has_include(<lvgl/lvgl.h>)
#include <lvgl/lvgl.h>
#else
#error "MCS_ENABLE_LVGL needs host LVGL 8.x/9.x: provide include paths or MCS_LVGL_INCLUDE; link the same host library and lv_conf.h"
#endif
#else
#include <lvgl.h>
#endif
#if LVGL_VERSION_MAJOR != 8 && LVGL_VERSION_MAJOR != 9
#error "MicroCS LVGL binding requires LVGL major 8 or 9"
#endif
#if MCS_LVGL_MAX_OBJECTS < 1 || MCS_LVGL_EVENT_QUEUE < 1
#error "LVGL object and event capacities must be positive"
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    lv_obj_t* root;                 /* live host object; never owned by MicroCS */
    void* ctx;
    void (*lock)(void* ctx);        /* blocking; same lock used by ALL host LVGL calls */
    void (*unlock)(void* ctx);
    bool (*is_vm_thread)(void* ctx); /* owner check; never calls LVGL or the VM */
    void* (*alloc)(void* ctx, size_t bytes); /* native storage, aligned like malloc */
    void (*free)(void* ctx, void* ptr);
} mcs_lvgl_config_t;
enum { MCS_LVGL_OK = 0, MCS_LVGL_INVALID = -1, MCS_LVGL_NOT_READY = -2,
       MCS_LVGL_NO_MEMORY = -3, MCS_LVGL_ALREADY_ATTACHED = -4 };
/* VM owner thread, VM not running. Attach once per VM, after host initialization.
 * On registration OOM attachment storage/widgets are cleaned; discard the VM
 * (some lazy API declarations may have registered before OOM).
 * Config callbacks/context must outlive the VM. No work occurs before attachment. */
int mcs_lvgl_attach(mcs_vm_t* vm, const mcs_lvgl_config_t* config);
/* mcs_free automatically removes the attachment before VM object/pin teardown. */
size_t mcs_lvgl_storage_size(void); /* native attachment bytes, not VM or LVGL bytes */
#ifdef __cplusplus
}
#endif
#endif
#endif
