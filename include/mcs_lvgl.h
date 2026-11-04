/*
 * MicroCS - optional LVGL 8.x / 9.x binding (MCS_ENABLE_LVGL=1, docs/LVGL.md).
 *
 * C# scripts call LVGL with the C names:
 *     var btn = lv_button_create(lv_screen_active());     // lv_btn_create on LVGL 8
 *     lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
 *     lv_obj_add_event_cb(btn, e => Console.WriteLine("clicked"), LV_EVENT_CLICKED, null);
 *
 * The firmware owns the port: it calls lv_init(), creates the display and input
 * devices, drives lv_tick_inc() and runs lv_timer_handler() (or sets
 * MCS_LVGL_AUTO_HANDLER) - all on the thread that runs the VM. Then:
 *     mcs_lvgl_open_lib(vm);     // after lv_init(); the runtime does it for you
 */
#ifndef MCS_LVGL_H
#define MCS_LVGL_H
#include "mcs.h"
#include "mcs_config.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Make the LVGL API visible to scripts of this VM (flat flash tables - no RAM until
 * a name is used). Returns false when LVGL support is not compiled in. */
bool mcs_lvgl_open_lib(mcs_vm_t* vm);
/* Called by mcs_free via the VM's cleanup; call it yourself only to detach early:
 * removes the event callbacks, timers and animations of this VM's scripts and,
 * with MCS_LVGL_CLEANUP_ON_CLOSE, deletes the objects they created. */
void mcs_lvgl_close_lib(mcs_vm_t* vm);
/* Serve LVGL drive `letter` ('M') from a MicroCS VFS (LittleFS, YAFFS, RAM, POSIX...)
 * so lv_image_set_src / lv_binfont_create / lv_font_load read script files.
 * Done automatically by mcs_lvgl_open_lib with the VM's VFS and MCS_LVGL_FS_LETTER. */
bool mcs_lvgl_fs_register(void* vfs /* mcs_vfs_t* */, char letter);
/* Run lv_timer_handler() once (what MCS_LVGL_AUTO_HANDLER does while scripts sleep).
 * Returns the ms until the next timer is due. */
uint32_t mcs_lvgl_poll(mcs_vm_t* vm);
/* LVGL version the binding was compiled against (MAJOR*10000 + MINOR*100 + PATCH). */
uint32_t mcs_lvgl_version(void);

#ifdef __cplusplus
}
#endif
#endif
