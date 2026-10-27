/* Private spelling bridge; never changes the host LVGL configuration. */
#ifndef MCS_LVGL_COMPAT_H
#define MCS_LVGL_COMPAT_H
#if LVGL_VERSION_MAJOR == 8
/* A registered display is the readiness test on LVGL 8, including early minors. */
#define mcs_lv_initialized() (lv_disp_get_next(NULL) != NULL)
#define MCS_LV_USE_BUTTON LV_USE_BTN
#define mcs_lv_obj_remove_flag lv_obj_clear_flag
#define mcs_lv_button_create lv_btn_create
#define mcs_lv_obj_delete lv_obj_del
#define mcs_lv_obj_remove_state lv_obj_clear_state
#define mcs_lv_display_get_next lv_disp_get_next
#else
#define mcs_lv_initialized lv_is_initialized
#define MCS_LV_USE_BUTTON LV_USE_BUTTON
#define mcs_lv_obj_remove_flag lv_obj_remove_flag
#define mcs_lv_button_create lv_button_create
#define mcs_lv_obj_delete lv_obj_delete
#define mcs_lv_obj_remove_state lv_obj_remove_state
#define mcs_lv_display_get_next lv_display_get_next
#endif
/* LVGL 9.0 returned void; 9.1 restored a descriptor return value. */
static inline bool mcs_lv_add_event(lv_obj_t* obj, lv_event_cb_t cb, lv_event_code_t code, void* data) {
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 0
    uint32_t before = lv_obj_get_event_count(obj);
    lv_obj_add_event_cb(obj,cb,code,data);
    return lv_obj_get_event_count(obj) == before + 1;
#else
    return lv_obj_add_event_cb(obj,cb,code,data) != NULL;
#endif
}
#endif
