/* Private spelling bridge; never changes the host LVGL configuration. */
#ifndef MCS_LVGL_COMPAT_H
#define MCS_LVGL_COMPAT_H
#if LVGL_VERSION_MAJOR == 8
/* A registered display is the readiness test on LVGL 8, including early minors. */
#define mcs_lv_initialized() (lv_disp_get_next(NULL) != NULL)
#define MCS_LV_USE_IMAGE LV_USE_IMG
#define mcs_lv_image_create lv_img_create
#define mcs_lv_image_set_src lv_img_set_src
#define mcs_lv_image_get_src lv_img_get_src
#define mcs_lv_image_cache_drop lv_img_cache_invalidate_src
#define mcs_lv_font_load lv_font_load
#define mcs_lv_font_destroy lv_font_free
#define MCS_LV_USE_BUTTONMATRIX LV_USE_BTNMATRIX
#define MCS_LV_USE_IMAGEBUTTON LV_USE_IMGBTN
#define mcs_lv_buttonmatrix_create lv_btnmatrix_create
#define mcs_lv_buttonmatrix_map lv_btnmatrix_get_map
#define mcs_lv_buttonmatrix_set_map lv_btnmatrix_set_map
#define mcs_lv_buttonmatrix_set_ctrl_map lv_btnmatrix_set_ctrl_map
#define mcs_lv_buttonmatrix_select lv_btnmatrix_set_selected_btn
#define mcs_lv_buttonmatrix_ctrl_t lv_btnmatrix_ctrl_t
#define mcs_lv_imagebutton_create lv_imgbtn_create
#define mcs_lv_spinner_create(p) lv_spinner_create(p,1000,60)
#define mcs_lv_child_count lv_obj_get_child_cnt
#define mcs_lv_table_rows lv_table_get_row_cnt
#define mcs_lv_table_columns lv_table_get_col_cnt
#define mcs_lv_color_rgb lv_color_to32
#define MCS_LV_USE_BUTTON LV_USE_BTN
#define mcs_lv_button_create lv_btn_create
#define mcs_lv_obj_delete lv_obj_del
#define mcs_lv_obj_remove_state lv_obj_clear_state
#define mcs_lv_display_get_next lv_disp_get_next
#else
#define mcs_lv_initialized lv_is_initialized
#define MCS_LV_USE_IMAGE LV_USE_IMAGE
#define mcs_lv_image_create lv_image_create
#define mcs_lv_image_set_src lv_image_set_src
#define mcs_lv_image_get_src lv_image_get_src
#define mcs_lv_image_cache_drop lv_image_cache_drop
#define mcs_lv_font_load lv_binfont_create
#define mcs_lv_font_destroy lv_binfont_destroy
#define MCS_LV_USE_BUTTONMATRIX LV_USE_BUTTONMATRIX
#define MCS_LV_USE_IMAGEBUTTON LV_USE_IMAGEBUTTON
#define mcs_lv_buttonmatrix_create lv_buttonmatrix_create
#define mcs_lv_buttonmatrix_map lv_buttonmatrix_get_map
#define mcs_lv_buttonmatrix_set_map lv_buttonmatrix_set_map
#define mcs_lv_buttonmatrix_set_ctrl_map lv_buttonmatrix_set_ctrl_map
#define mcs_lv_buttonmatrix_select lv_buttonmatrix_set_selected_button
#define mcs_lv_buttonmatrix_ctrl_t lv_buttonmatrix_ctrl_t
#define mcs_lv_imagebutton_create lv_imagebutton_create
#define mcs_lv_spinner_create lv_spinner_create
#define mcs_lv_child_count lv_obj_get_child_count
#define mcs_lv_table_rows lv_table_get_row_count
#define mcs_lv_table_columns lv_table_get_column_count
#define mcs_lv_color_rgb lv_color_to_u32
#define MCS_LV_USE_BUTTON LV_USE_BUTTON
#define mcs_lv_button_create lv_button_create
#define mcs_lv_obj_delete lv_obj_delete
#define mcs_lv_obj_remove_state lv_obj_remove_state
#define mcs_lv_display_get_next lv_display_get_next
#endif
/* Call exposes a Unicode scalar, not LVGL's endian-dependent packed UTF-8
 * integer. Copy through add_text instead of feeding the code point as bytes. */
#if LV_USE_TEXTAREA
static inline void mcs_lv_textarea_add_scalar(lv_obj_t* obj,uint32_t c) {
    char text[5];unsigned n=0;
#if LV_TXT_ENC == LV_TXT_ENC_UTF8
    if(c<0x80)text[n++]=(char)c;
    else if(c<0x800){text[n++]=(char)(0xc0u|(c>>6));text[n++]=(char)(0x80u|(c&63u));}
    else if(c<0x10000){text[n++]=(char)(0xe0u|(c>>12));text[n++]=(char)(0x80u|((c>>6)&63u));text[n++]=(char)(0x80u|(c&63u));}
    else {text[n++]=(char)(0xf0u|(c>>18));text[n++]=(char)(0x80u|((c>>12)&63u));text[n++]=(char)(0x80u|((c>>6)&63u));text[n++]=(char)(0x80u|(c&63u));}
#else
    text[n++]=(char)c; /* preparation restricts non-UTF8 hosts to ASCII */
#endif
    text[n]='\0';lv_textarea_add_text(obj,text);
}
#endif
#if LV_USE_TEXTAREA
/* Preserve legacy alignment semantics without calling a deprecated entrypoint. */
static inline void mcs_lv_textarea_set_align(lv_obj_t* obj,lv_text_align_t align) {
    lv_obj_set_style_text_align(obj,align,0);
#if LVGL_VERSION_MAJOR == 8 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR < 6)
    lv_align_t anchor=align==LV_TEXT_ALIGN_RIGHT ? LV_ALIGN_TOP_RIGHT : align==LV_TEXT_ALIGN_CENTER ? LV_ALIGN_TOP_MID : LV_ALIGN_TOP_LEFT;
    lv_obj_align(lv_textarea_get_label(obj),anchor,0,0);
#endif
}
#endif
/* These compatibility entrypoints remain exported upstream but 9.6 marks
 * generic flags/List as deprecated. Keep diagnostics local to the legacy bridge,
 * not to user code or the generated API; no native private layouts are accessed. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static inline void mcs_lv_obj_add_flag(lv_obj_t* o,uint32_t f){lv_obj_add_flag(o,f);}
static inline void mcs_lv_obj_remove_flag(lv_obj_t* o,uint32_t f){
#if LVGL_VERSION_MAJOR == 8
    lv_obj_clear_flag(o,f);
#else
    lv_obj_remove_flag(o,f);
#endif
}
static inline bool mcs_lv_obj_has_flag(const lv_obj_t* o,uint32_t f){return lv_obj_has_flag(o,f);}
#if LV_USE_LIST
static inline lv_obj_t* mcs_lv_list_create(lv_obj_t* p){return lv_list_create(p);}
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#if LV_USE_TABLE
static inline void mcs_lv_table_add_cell_ctrl(lv_obj_t* o,uint32_t row,uint32_t col,uint32_t ctrl){
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 3
    lv_table_set_cell_ctrl(o,row,col,ctrl);
#else
    lv_table_add_cell_ctrl(o,row,col,ctrl);
#endif
}
#endif
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6
/* Documented exported cache API: 9.6's umbrella omits this declaration. Match
 * the upstream signature exactly; no private structures/config are required. */
#ifdef __cplusplus
extern "C" {
#endif
void lv_image_cache_drop(const void* src);
#ifdef __cplusplus
}
#endif
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
