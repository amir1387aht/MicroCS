/* Freeing an embedded VM must detach LVGL without requiring a separate close. */
#include "mcs.h"
#include "mcs_lvgl.h"
#include <lvgl.h>
#include <assert.h>
#include <stdio.h>

int mcs_lvgl_host_init(mcs_vm_t* vm, int w, int h);
#if LVGL_VERSION_MAJOR >= 9
#define SCREEN() lv_screen_active()
#define CHILDREN(s) lv_obj_get_child_count(s)
#define SEND_EVENT(s) lv_obj_send_event(s, LV_EVENT_CLICKED, NULL)
#else
#define SCREEN() lv_scr_act()
#define CHILDREN(s) lv_obj_get_child_cnt(s)
#define SEND_EVENT(s) lv_event_send(s, LV_EVENT_CLICKED, NULL)
#endif

static unsigned timer_count(void) {
    unsigned n = 0;
    for (lv_timer_t* t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) n++;
    return n;
}

int main(void) {
    mcs_vm_t* vm = mcs_new(NULL);
    assert(vm && mcs_lvgl_host_init(vm, 160, 100) == 0);
    lv_obj_t* screen = SCREEN();
    unsigned before = CHILDREN(screen);
    unsigned timers_before = timer_count();
    assert(mcs_exec_source(vm, "cleanup.cs",
        "var s = lv_screen_active();\n"
        "var o = lv_obj_create(s);\n"
        "lv_obj_add_event_cb(s, e => Console.WriteLine(\"stale event\"), LV_EVENT_CLICKED, null);\n"
        "var t = lv_timer_create(t => Console.WriteLine(\"stale timer\"), 10, null);\n") == MCS_OK);
    assert(CHILDREN(screen) == before + 1);
    assert(timer_count() == timers_before + 1);
    mcs_free(vm);
    assert(CHILDREN(screen) == before);
    assert(timer_count() == timers_before);
    SEND_EVENT(screen);
    lv_tick_inc(50); lv_timer_handler();
    vm = mcs_new(NULL);
    assert(vm && mcs_lvgl_host_init(vm, 160, 100) == 0);
    mcs_lvgl_close_lib(vm); mcs_lvgl_close_lib(vm); mcs_free(vm);
    puts("PASS LVGL VM cleanup and explicit-close idempotence");
    return 0;
}
