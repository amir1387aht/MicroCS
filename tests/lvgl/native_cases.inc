/* Generated availability/arity/style smoke cases; semantic tests are separate. */
static const struct { const char* name; bool available; int argc; const char* smoke; } native_cases[] = {
{"lv_arc_get_angle_end",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_angle_start",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_bg_angle_end",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_bg_angle_start",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_max_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_min_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_get_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
0,NULL},
{"lv_arc_set_angles",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
2,NULL},
{"lv_arc_set_bg_angles",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
2,NULL},
{"lv_arc_set_bg_end_angle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_bg_start_angle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_change_rate",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_end_angle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_range",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
2,NULL},
{"lv_arc_set_rotation",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_start_angle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_arc_set_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
1,NULL},
{"lv_bar_get_max_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
0,NULL},
{"lv_bar_get_min_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
0,NULL},
{"lv_bar_get_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
0,NULL},
{"lv_bar_get_start_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
0,NULL},
{"lv_bar_get_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
0,NULL},
{"lv_bar_set_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
1,NULL},
{"lv_bar_set_range",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
2,NULL},
{"lv_bar_set_start_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
2,NULL},
{"lv_bar_set_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
2,NULL},
{"lv_btnmatrix_clear_btn_ctrl",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
2,NULL},
{"lv_btnmatrix_clear_btn_ctrl_all",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
1,NULL},
{"lv_btnmatrix_get_btn_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
1,NULL},
{"lv_btnmatrix_get_one_checked",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
0,NULL},
{"lv_btnmatrix_get_selected_btn",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
0,NULL},
{"lv_btnmatrix_has_btn_ctrl",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
2,NULL},
{"lv_btnmatrix_set_btn_ctrl",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
2,NULL},
{"lv_btnmatrix_set_btn_ctrl_all",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
1,NULL},
{"lv_btnmatrix_set_btn_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
2,NULL},
{"lv_btnmatrix_set_one_checked",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
1,NULL},
{"lv_btnmatrix_set_selected_btn",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
1,NULL},
{"lv_calendar_set_showed_date",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CALENDAR)
true,
#else
false,
#endif
2,NULL},
{"lv_calendar_set_today_date",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CALENDAR)
true,
#else
false,
#endif
3,NULL},
{"lv_chart_get_point_count",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
0,NULL},
{"lv_chart_get_pressed_point",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
0,NULL},
{"lv_chart_get_type",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
0,NULL},
{"lv_chart_refresh",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
0,NULL},
{"lv_chart_set_div_line_count",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
2,NULL},
{"lv_chart_set_point_count",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
1,NULL},
{"lv_chart_set_range",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
3,NULL},
{"lv_chart_set_type",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
1,NULL},
{"lv_chart_set_update_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
1,NULL},
{"lv_checkbox_get_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHECKBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_checkbox_set_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHECKBOX)
true,
#else
false,
#endif
1,NULL},
{"lv_dropdown_add_option",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
2,NULL},
{"lv_dropdown_clear_options",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_close",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_option_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_options",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_selected",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_selected_highlight",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_selected_str",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_symbol",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_get_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_is_open",
#if (LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_open",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
0,NULL},
{"lv_dropdown_set_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
1,NULL},
{"lv_dropdown_set_options",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
1,NULL},
{"lv_dropdown_set_selected",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
1,NULL},
{"lv_dropdown_set_selected_highlight",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
1,NULL},
{"lv_dropdown_set_symbol",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
1,NULL},
{"lv_dropdown_set_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_DROPDOWN)
true,
#else
false,
#endif
1,NULL},
{"lv_img_get_angle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
0,NULL},
{"lv_img_get_antialias",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
0,NULL},
{"lv_img_get_offset_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
0,NULL},
{"lv_img_get_offset_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
0,NULL},
{"lv_img_get_zoom",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
0,NULL},
{"lv_img_set_angle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
1,NULL},
{"lv_img_set_antialias",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
1,NULL},
{"lv_img_set_offset_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
1,NULL},
{"lv_img_set_offset_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
1,NULL},
{"lv_img_set_pivot",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
2,NULL},
{"lv_img_set_zoom",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_IMAGE)
true,
#else
false,
#endif
1,NULL},
{"lv_keyboard_get_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
0,NULL},
{"lv_keyboard_set_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
1,NULL},
{"lv_keyboard_set_popovers",
#if (LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
1,NULL},
{"lv_keyboard_set_textarea",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
1,NULL},
{"lv_label_cut_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
2,NULL},
{"lv_label_get_long_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
0,NULL},
{"lv_label_get_recolor",
#if (LVGL_VERSION_MAJOR == 8) && (LV_USE_LABEL)
true,
#else
false,
#endif
0,NULL},
{"lv_label_get_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
0,NULL},
{"lv_label_get_text_selection_end",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
0,NULL},
{"lv_label_get_text_selection_start",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
0,NULL},
{"lv_label_ins_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
2,NULL},
{"lv_label_set_long_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
1,NULL},
{"lv_label_set_recolor",
#if (LVGL_VERSION_MAJOR == 8) && (LV_USE_LABEL)
true,
#else
false,
#endif
1,NULL},
{"lv_label_set_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
1,NULL},
{"lv_label_set_text_sel_end",
#if (LVGL_VERSION_MAJOR == 8) && (LV_USE_LABEL)
true,
#else
false,
#endif
1,NULL},
{"lv_label_set_text_sel_start",
#if (LVGL_VERSION_MAJOR == 8) && (LV_USE_LABEL)
true,
#else
false,
#endif
1,NULL},
{"lv_led_get_brightness",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LED)
true,
#else
false,
#endif
0,NULL},
{"lv_led_off",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LED)
true,
#else
false,
#endif
0,NULL},
{"lv_led_on",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LED)
true,
#else
false,
#endif
0,NULL},
{"lv_led_set_brightness",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LED)
true,
#else
false,
#endif
1,NULL},
{"lv_led_set_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LED)
true,
#else
false,
#endif
1,NULL},
{"lv_led_toggle",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LED)
true,
#else
false,
#endif
0,NULL},
{"lv_line_get_y_invert",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LINE)
true,
#else
false,
#endif
0,NULL},
{"lv_line_set_y_invert",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LINE)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_add_flag",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_add_state",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
3,NULL},
{"lv_obj_align_to",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
4,NULL},
{"lv_obj_clean",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_clear_flag",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_clear_state",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_get_child_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_content_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_content_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_index",
#if (LVGL_VERSION_MAJOR == 9 || LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_bottom",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_left",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_right",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_snap_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_snap_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_top",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scroll_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_scrollbar_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_state",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_style_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_align\",0);"},
{"lv_obj_get_style_anim_duration",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_anim_duration\",0);"},
{"lv_obj_get_style_anim_speed",
#if (LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_anim_speed\",0);"},
{"lv_obj_get_style_arc_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_arc_color\",0);"},
{"lv_obj_get_style_arc_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_arc_color_filtered\",0);"},
{"lv_obj_get_style_arc_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_arc_opa\",0);"},
{"lv_obj_get_style_arc_rounded",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_arc_rounded\",0);"},
{"lv_obj_get_style_arc_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_arc_width\",0);"},
{"lv_obj_get_style_base_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_base_dir\",0);"},
{"lv_obj_get_style_bg_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_color\",0);"},
{"lv_obj_get_style_bg_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_color_filtered\",0);"},
{"lv_obj_get_style_bg_grad_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_grad_color\",0);"},
{"lv_obj_get_style_bg_grad_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_grad_color_filtered\",0);"},
{"lv_obj_get_style_bg_grad_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_grad_dir\",0);"},
{"lv_obj_get_style_bg_grad_opa",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_grad_opa\",0);"},
{"lv_obj_get_style_bg_grad_stop",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_grad_stop\",0);"},
{"lv_obj_get_style_bg_image_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_image_opa\",0);"},
{"lv_obj_get_style_bg_image_recolor",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_image_recolor\",0);"},
{"lv_obj_get_style_bg_image_recolor_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_image_recolor_opa\",0);"},
{"lv_obj_get_style_bg_image_tiled",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_image_tiled\",0);"},
{"lv_obj_get_style_bg_img_recolor_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_img_recolor_filtered\",0);"},
{"lv_obj_get_style_bg_main_opa",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_main_opa\",0);"},
{"lv_obj_get_style_bg_main_stop",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_main_stop\",0);"},
{"lv_obj_get_style_bg_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_bg_opa\",0);"},
{"lv_obj_get_style_blend_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_blend_mode\",0);"},
{"lv_obj_get_style_border_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_border_color\",0);"},
{"lv_obj_get_style_border_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_border_color_filtered\",0);"},
{"lv_obj_get_style_border_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_border_opa\",0);"},
{"lv_obj_get_style_border_post",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_border_post\",0);"},
{"lv_obj_get_style_border_side",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_border_side\",0);"},
{"lv_obj_get_style_border_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_border_width\",0);"},
{"lv_obj_get_style_clip_corner",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_clip_corner\",0);"},
{"lv_obj_get_style_color_filter_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_color_filter_opa\",0);"},
{"lv_obj_get_style_flex_cross_place",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_flex_cross_place\",0);"},
{"lv_obj_get_style_flex_flow",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_flex_flow\",0);"},
{"lv_obj_get_style_flex_grow",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_flex_grow\",0);"},
{"lv_obj_get_style_flex_main_place",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_flex_main_place\",0);"},
{"lv_obj_get_style_flex_track_place",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_flex_track_place\",0);"},
{"lv_obj_get_style_grid_cell_column_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_cell_column_pos\",0);"},
{"lv_obj_get_style_grid_cell_column_span",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_cell_column_span\",0);"},
{"lv_obj_get_style_grid_cell_row_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_cell_row_pos\",0);"},
{"lv_obj_get_style_grid_cell_row_span",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_cell_row_span\",0);"},
{"lv_obj_get_style_grid_cell_x_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_cell_x_align\",0);"},
{"lv_obj_get_style_grid_cell_y_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_cell_y_align\",0);"},
{"lv_obj_get_style_grid_column_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_column_align\",0);"},
{"lv_obj_get_style_grid_row_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_grid_row_align\",0);"},
{"lv_obj_get_style_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_height\",0);"},
{"lv_obj_get_style_image_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_image_opa\",0);"},
{"lv_obj_get_style_image_recolor",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_image_recolor\",0);"},
{"lv_obj_get_style_image_recolor_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_image_recolor_opa\",0);"},
{"lv_obj_get_style_img_recolor_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_img_recolor_filtered\",0);"},
{"lv_obj_get_style_length",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_length\",0);"},
{"lv_obj_get_style_line_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_color\",0);"},
{"lv_obj_get_style_line_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_color_filtered\",0);"},
{"lv_obj_get_style_line_dash_gap",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_dash_gap\",0);"},
{"lv_obj_get_style_line_dash_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_dash_width\",0);"},
{"lv_obj_get_style_line_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_opa\",0);"},
{"lv_obj_get_style_line_rounded",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_rounded\",0);"},
{"lv_obj_get_style_line_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_line_width\",0);"},
{"lv_obj_get_style_margin_bottom",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_margin_bottom\",0);"},
{"lv_obj_get_style_margin_left",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_margin_left\",0);"},
{"lv_obj_get_style_margin_right",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_margin_right\",0);"},
{"lv_obj_get_style_margin_top",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_margin_top\",0);"},
{"lv_obj_get_style_max_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_max_height\",0);"},
{"lv_obj_get_style_max_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_max_width\",0);"},
{"lv_obj_get_style_min_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_min_height\",0);"},
{"lv_obj_get_style_min_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_min_width\",0);"},
{"lv_obj_get_style_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_opa\",0);"},
{"lv_obj_get_style_opa_layered",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_opa_layered\",0);"},
{"lv_obj_get_style_outline_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_outline_color\",0);"},
{"lv_obj_get_style_outline_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_outline_color_filtered\",0);"},
{"lv_obj_get_style_outline_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_outline_opa\",0);"},
{"lv_obj_get_style_outline_pad",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_outline_pad\",0);"},
{"lv_obj_get_style_outline_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_outline_width\",0);"},
{"lv_obj_get_style_pad_bottom",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_pad_bottom\",0);"},
{"lv_obj_get_style_pad_column",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_pad_column\",0);"},
{"lv_obj_get_style_pad_left",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_pad_left\",0);"},
{"lv_obj_get_style_pad_right",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_pad_right\",0);"},
{"lv_obj_get_style_pad_row",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_pad_row\",0);"},
{"lv_obj_get_style_pad_top",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_pad_top\",0);"},
{"lv_obj_get_style_radius",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_radius\",0);"},
{"lv_obj_get_style_shadow_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_color\",0);"},
{"lv_obj_get_style_shadow_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_color_filtered\",0);"},
{"lv_obj_get_style_shadow_offset_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_offset_x\",0);"},
{"lv_obj_get_style_shadow_offset_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_offset_y\",0);"},
{"lv_obj_get_style_shadow_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_opa\",0);"},
{"lv_obj_get_style_shadow_spread",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_spread\",0);"},
{"lv_obj_get_style_shadow_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_shadow_width\",0);"},
{"lv_obj_get_style_text_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_align\",0);"},
{"lv_obj_get_style_text_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_color\",0);"},
{"lv_obj_get_style_text_color_filtered",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_color_filtered\",0);"},
{"lv_obj_get_style_text_decor",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_decor\",0);"},
{"lv_obj_get_style_text_letter_space",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_letter_space\",0);"},
{"lv_obj_get_style_text_line_space",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_line_space\",0);"},
{"lv_obj_get_style_text_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_text_opa\",0);"},
{"lv_obj_get_style_transform_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_height\",0);"},
{"lv_obj_get_style_transform_pivot_x",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_pivot_x\",0);"},
{"lv_obj_get_style_transform_pivot_y",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_pivot_y\",0);"},
{"lv_obj_get_style_transform_rotation",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_rotation\",0);"},
{"lv_obj_get_style_transform_scale_x",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_scale_x\",0);"},
{"lv_obj_get_style_transform_scale_y",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_scale_y\",0);"},
{"lv_obj_get_style_transform_skew_x",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_skew_x\",0);"},
{"lv_obj_get_style_transform_skew_y",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_skew_y\",0);"},
{"lv_obj_get_style_transform_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_width\",0);"},
{"lv_obj_get_style_transform_zoom",
#if (LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_transform_zoom\",0);"},
{"lv_obj_get_style_translate_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_translate_x\",0);"},
{"lv_obj_get_style_translate_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_translate_y\",0);"},
{"lv_obj_get_style_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_width\",0);"},
{"lv_obj_get_style_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_x\",0);"},
{"lv_obj_get_style_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,"probe.Call(\"lv_obj_get_style_y\",0);"},
{"lv_obj_get_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_x_aligned",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_get_y_aligned",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_has_flag",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_has_state",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_invalidate",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_is_layout_positioned",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_mark_layout_as_dirty",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_move_background",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_move_foreground",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_obj_scroll_by",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
3,NULL},
{"lv_obj_scroll_to",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
3,NULL},
{"lv_obj_scroll_to_view",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_scroll_to_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,NULL},
{"lv_obj_scroll_to_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,NULL},
{"lv_obj_set_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_flex_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
3,NULL},
{"lv_obj_set_flex_flow",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_flex_grow",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_grid_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,NULL},
{"lv_obj_set_grid_cell",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
6,NULL},
{"lv_obj_set_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_parent",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_scroll_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_scroll_snap_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_scroll_snap_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_scrollbar_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_style_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_align\",0,0);"},
{"lv_obj_set_style_anim_duration",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_anim_duration\",0,0);"},
{"lv_obj_set_style_anim_speed",
#if (LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_anim_speed\",0,0);"},
{"lv_obj_set_style_arc_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_arc_color\",16711680,0);"},
{"lv_obj_set_style_arc_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_arc_opa\",0,0);"},
{"lv_obj_set_style_arc_rounded",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_arc_rounded\",0,0);"},
{"lv_obj_set_style_arc_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_arc_width\",0,0);"},
{"lv_obj_set_style_base_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_base_dir\",0,0);"},
{"lv_obj_set_style_bg_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_color\",16711680,0);"},
{"lv_obj_set_style_bg_grad_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_grad_color\",16711680,0);"},
{"lv_obj_set_style_bg_grad_dir",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_grad_dir\",0,0);"},
{"lv_obj_set_style_bg_grad_opa",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_grad_opa\",0,0);"},
{"lv_obj_set_style_bg_grad_stop",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_grad_stop\",0,0);"},
{"lv_obj_set_style_bg_image_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_image_opa\",0,0);"},
{"lv_obj_set_style_bg_image_recolor",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_image_recolor\",16711680,0);"},
{"lv_obj_set_style_bg_image_recolor_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_image_recolor_opa\",0,0);"},
{"lv_obj_set_style_bg_image_tiled",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_image_tiled\",0,0);"},
{"lv_obj_set_style_bg_main_opa",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_main_opa\",0,0);"},
{"lv_obj_set_style_bg_main_stop",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_main_stop\",0,0);"},
{"lv_obj_set_style_bg_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_bg_opa\",0,0);"},
{"lv_obj_set_style_blend_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_blend_mode\",0,0);"},
{"lv_obj_set_style_border_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_border_color\",16711680,0);"},
{"lv_obj_set_style_border_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_border_opa\",0,0);"},
{"lv_obj_set_style_border_post",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_border_post\",0,0);"},
{"lv_obj_set_style_border_side",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_border_side\",0,0);"},
{"lv_obj_set_style_border_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_border_width\",0,0);"},
{"lv_obj_set_style_clip_corner",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_clip_corner\",0,0);"},
{"lv_obj_set_style_color_filter_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_color_filter_opa\",0,0);"},
{"lv_obj_set_style_flex_cross_place",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_flex_cross_place\",0,0);"},
{"lv_obj_set_style_flex_flow",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_flex_flow\",0,0);"},
{"lv_obj_set_style_flex_grow",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_flex_grow\",0,0);"},
{"lv_obj_set_style_flex_main_place",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_flex_main_place\",0,0);"},
{"lv_obj_set_style_flex_track_place",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_flex_track_place\",0,0);"},
{"lv_obj_set_style_grid_cell_column_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_cell_column_pos\",0,0);"},
{"lv_obj_set_style_grid_cell_column_span",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_cell_column_span\",1,0);"},
{"lv_obj_set_style_grid_cell_row_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_cell_row_pos\",0,0);"},
{"lv_obj_set_style_grid_cell_row_span",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_cell_row_span\",1,0);"},
{"lv_obj_set_style_grid_cell_x_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_cell_x_align\",0,0);"},
{"lv_obj_set_style_grid_cell_y_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_cell_y_align\",0,0);"},
{"lv_obj_set_style_grid_column_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_column_align\",0,0);"},
{"lv_obj_set_style_grid_row_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_grid_row_align\",0,0);"},
{"lv_obj_set_style_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_height\",0,0);"},
{"lv_obj_set_style_image_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_image_opa\",0,0);"},
{"lv_obj_set_style_image_recolor",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_image_recolor\",16711680,0);"},
{"lv_obj_set_style_image_recolor_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_image_recolor_opa\",0,0);"},
{"lv_obj_set_style_length",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_length\",0,0);"},
{"lv_obj_set_style_line_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_line_color\",16711680,0);"},
{"lv_obj_set_style_line_dash_gap",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_line_dash_gap\",0,0);"},
{"lv_obj_set_style_line_dash_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_line_dash_width\",0,0);"},
{"lv_obj_set_style_line_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_line_opa\",0,0);"},
{"lv_obj_set_style_line_rounded",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_line_rounded\",0,0);"},
{"lv_obj_set_style_line_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_line_width\",0,0);"},
{"lv_obj_set_style_margin_bottom",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_margin_bottom\",0,0);"},
{"lv_obj_set_style_margin_left",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_margin_left\",0,0);"},
{"lv_obj_set_style_margin_right",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_margin_right\",0,0);"},
{"lv_obj_set_style_margin_top",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_margin_top\",0,0);"},
{"lv_obj_set_style_max_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_max_height\",0,0);"},
{"lv_obj_set_style_max_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_max_width\",0,0);"},
{"lv_obj_set_style_min_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_min_height\",0,0);"},
{"lv_obj_set_style_min_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_min_width\",0,0);"},
{"lv_obj_set_style_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_opa\",0,0);"},
{"lv_obj_set_style_opa_layered",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_opa_layered\",0,0);"},
{"lv_obj_set_style_outline_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_outline_color\",16711680,0);"},
{"lv_obj_set_style_outline_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_outline_opa\",0,0);"},
{"lv_obj_set_style_outline_pad",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_outline_pad\",0,0);"},
{"lv_obj_set_style_outline_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_outline_width\",0,0);"},
{"lv_obj_set_style_pad_bottom",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_pad_bottom\",0,0);"},
{"lv_obj_set_style_pad_column",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_pad_column\",0,0);"},
{"lv_obj_set_style_pad_left",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_pad_left\",0,0);"},
{"lv_obj_set_style_pad_right",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_pad_right\",0,0);"},
{"lv_obj_set_style_pad_row",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_pad_row\",0,0);"},
{"lv_obj_set_style_pad_top",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_pad_top\",0,0);"},
{"lv_obj_set_style_radius",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_radius\",0,0);"},
{"lv_obj_set_style_shadow_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_shadow_color\",16711680,0);"},
{"lv_obj_set_style_shadow_offset_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_shadow_offset_x\",0,0);"},
{"lv_obj_set_style_shadow_offset_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_shadow_offset_y\",0,0);"},
{"lv_obj_set_style_shadow_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_shadow_opa\",0,0);"},
{"lv_obj_set_style_shadow_spread",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_shadow_spread\",0,0);"},
{"lv_obj_set_style_shadow_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_shadow_width\",0,0);"},
{"lv_obj_set_style_text_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_text_align\",0,0);"},
{"lv_obj_set_style_text_color",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_text_color\",16711680,0);"},
{"lv_obj_set_style_text_decor",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_text_decor\",0,0);"},
{"lv_obj_set_style_text_letter_space",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_text_letter_space\",0,0);"},
{"lv_obj_set_style_text_line_space",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_text_line_space\",0,0);"},
{"lv_obj_set_style_text_opa",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_text_opa\",0,0);"},
{"lv_obj_set_style_transform_height",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_height\",0,0);"},
{"lv_obj_set_style_transform_pivot_x",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_pivot_x\",0,0);"},
{"lv_obj_set_style_transform_pivot_y",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_pivot_y\",0,0);"},
{"lv_obj_set_style_transform_rotation",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_rotation\",0,0);"},
{"lv_obj_set_style_transform_scale_x",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_scale_x\",256,0);"},
{"lv_obj_set_style_transform_scale_y",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_scale_y\",256,0);"},
{"lv_obj_set_style_transform_skew_x",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_skew_x\",0,0);"},
{"lv_obj_set_style_transform_skew_y",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_skew_y\",0,0);"},
{"lv_obj_set_style_transform_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_width\",0,0);"},
{"lv_obj_set_style_transform_zoom",
#if (LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_transform_zoom\",256,0);"},
{"lv_obj_set_style_translate_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_translate_x\",0,0);"},
{"lv_obj_set_style_translate_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_translate_y\",0,0);"},
{"lv_obj_set_style_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_width\",0,0);"},
{"lv_obj_set_style_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_x\",0,0);"},
{"lv_obj_set_style_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
2,"probe.Call(\"lv_obj_set_style_y\",0,0);"},
{"lv_obj_set_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_x",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_set_y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
1,NULL},
{"lv_obj_update_layout",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
0,NULL},
{"lv_roller_get_option_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
0,NULL},
{"lv_roller_get_options",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
0,NULL},
{"lv_roller_get_selected",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
0,NULL},
{"lv_roller_get_selected_str",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
0,NULL},
{"lv_roller_set_options",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
2,NULL},
{"lv_roller_set_selected",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
2,NULL},
{"lv_roller_set_visible_row_count",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
1,NULL},
{"lv_slider_get_left_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
0,NULL},
{"lv_slider_get_max_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
0,NULL},
{"lv_slider_get_min_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
0,NULL},
{"lv_slider_get_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
0,NULL},
{"lv_slider_get_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
0,NULL},
{"lv_slider_is_dragged",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
0,NULL},
{"lv_slider_set_left_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
2,NULL},
{"lv_slider_set_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
1,NULL},
{"lv_slider_set_range",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
2,NULL},
{"lv_slider_set_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
2,NULL},
{"lv_spinbox_decrement",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_spinbox_get_rollover",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_spinbox_get_step",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_spinbox_get_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_spinbox_increment",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_spinbox_set_digit_format",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
2,NULL},
{"lv_spinbox_set_range",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
2,NULL},
{"lv_spinbox_set_rollover",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
1,NULL},
{"lv_spinbox_set_step",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
1,NULL},
{"lv_spinbox_set_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
1,NULL},
{"lv_spinbox_step_next",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_spinbox_step_prev",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SPINBOX)
true,
#else
false,
#endif
0,NULL},
{"lv_table_add_cell_ctrl",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
3,NULL},
{"lv_table_clear_cell_ctrl",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
3,NULL},
{"lv_table_get_cell_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
2,NULL},
{"lv_table_get_col_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
0,NULL},
{"lv_table_get_col_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
1,NULL},
{"lv_table_get_row_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
0,NULL},
{"lv_table_has_cell_ctrl",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
3,NULL},
{"lv_table_set_cell_value",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
3,NULL},
{"lv_table_set_col_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
1,NULL},
{"lv_table_set_col_width",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
2,NULL},
{"lv_table_set_row_cnt",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_add_char",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_add_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_clear_selection",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_cursor_down",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_cursor_left",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_cursor_right",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_cursor_up",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_del_char",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_del_char_forward",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_accepted_chars",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_cursor_click_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_cursor_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_max_length",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_one_line",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_password_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_password_show_time",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_placeholder_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_get_text_selection",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
{"lv_textarea_set_accepted_chars",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_align",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_cursor_click_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_cursor_pos",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_max_length",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_one_line",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_password_mode",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_password_show_time",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_placeholder_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_text",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_set_text_selection",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
1,NULL},
{"lv_textarea_text_is_selected",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
0,NULL},
};
static const struct { const char* name;bool available; } constant_cases[] = {
{"LV_ALIGN_BOTTOM_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_BOTTOM_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_BOTTOM_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_CENTER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_DEFAULT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_LEFT_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_BOTTOM_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_BOTTOM_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_BOTTOM_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_LEFT_BOTTOM",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_LEFT_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_LEFT_TOP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_RIGHT_BOTTOM",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_RIGHT_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_RIGHT_TOP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_TOP_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_TOP_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_OUT_TOP_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_RIGHT_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_TOP_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_TOP_MID",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ALIGN_TOP_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ANIM_OFF",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ANIM_ON",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_ARC_MODE_NORMAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
},
{"LV_ARC_MODE_SYMMETRICAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ARC)
true,
#else
false,
#endif
},
{"LV_BAR_MODE_NORMAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
},
{"LV_BAR_MODE_RANGE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
},
{"LV_BAR_MODE_SYMMETRICAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_BAR)
true,
#else
false,
#endif
},
{"LV_BASE_DIR_AUTO",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BASE_DIR_LTR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BASE_DIR_NEUTRAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BASE_DIR_RTL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BASE_DIR_WEAK",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BLEND_MODE_ADDITIVE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BLEND_MODE_MULTIPLY",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BLEND_MODE_NORMAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BLEND_MODE_SUBTRACTIVE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_BOTTOM",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_FULL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_INTERNAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BORDER_SIDE_TOP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_BTN_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_CHECKABLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_CHECKED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_CLICK_TRIG",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_DISABLED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_HIDDEN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_NO_REPEAT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_POPOVER",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BTNMATRIX_CTRL_RECOLOR",
#if (LVGL_VERSION_MAJOR == 8) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_BUTTON_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_CHECKABLE",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_CHECKED",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_CLICK_TRIG",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_DISABLED",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_HIDDEN",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_NO_REPEAT",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_BUTTONMATRIX_CTRL_POPOVER",
#if (LVGL_VERSION_MAJOR == 9) && (MCS_LV_USE_BUTTONMATRIX)
true,
#else
false,
#endif
},
{"LV_CHART_AXIS_PRIMARY_X",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_AXIS_PRIMARY_Y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_AXIS_SECONDARY_X",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_AXIS_SECONDARY_Y",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_TYPE_BAR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_TYPE_LINE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_TYPE_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_TYPE_SCATTER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_UPDATE_MODE_CIRCULAR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_CHART_UPDATE_MODE_SHIFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_CHART)
true,
#else
false,
#endif
},
{"LV_DIR_ALL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_BOTTOM",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_HOR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_TOP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_DIR_VER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_CANCEL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_CLICKED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_DEFOCUSED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_FOCUSED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_LEAVE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_LONG_PRESSED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_LONG_PRESSED_REPEAT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_PRESSED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_PRESSING",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_PRESS_LOST",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_READY",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_RELEASED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_SCROLL_BEGIN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_SCROLL_END",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_SHORT_CLICKED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_EVENT_VALUE_CHANGED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_FLEX_ALIGN_CENTER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_ALIGN_END",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_ALIGN_SPACE_AROUND",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_ALIGN_SPACE_BETWEEN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_ALIGN_SPACE_EVENLY",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_ALIGN_START",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_COLUMN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_COLUMN_REVERSE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_COLUMN_WRAP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_COLUMN_WRAP_REVERSE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_ROW",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_ROW_REVERSE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_ROW_WRAP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_FLEX_FLOW_ROW_WRAP_REVERSE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_GRAD_DIR_HOR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_GRAD_DIR_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_GRAD_DIR_VER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_CENTER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_END",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_SPACE_AROUND",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_SPACE_BETWEEN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_SPACE_EVENLY",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_START",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_ALIGN_STRETCH",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_GRID_CONTENT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_GRID)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_NUMBER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_SPECIAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_TEXT_LOWER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_TEXT_UPPER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_USER_1",
#if (LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_USER_2",
#if (LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_USER_3",
#if (LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_KEYBOARD_MODE_USER_4",
#if (LVGL_VERSION_MAJOR == 9) && (LV_USE_KEYBOARD)
true,
#else
false,
#endif
},
{"LV_LABEL_LONG_CLIP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
},
{"LV_LABEL_LONG_DOT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
},
{"LV_LABEL_LONG_SCROLL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
},
{"LV_LABEL_LONG_SCROLL_CIRCULAR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
},
{"LV_LABEL_LONG_WRAP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_LABEL)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_ADV_HITTEST",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_CHECKABLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_CLICKABLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_CLICK_FOCUSABLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_EVENT_BUBBLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_FLEX_IN_NEW_TRACK",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_FLEX)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_FLOATING",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_GESTURE_BUBBLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_HIDDEN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_IGNORE_LAYOUT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_LAYOUT_1",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_LAYOUT_2",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_OVERFLOW_VISIBLE",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_PRESS_LOCK",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLLABLE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_CHAIN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_CHAIN_HOR",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_CHAIN_VER",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_ELASTIC",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_MOMENTUM",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_ONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_ON_FOCUS",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SCROLL_WITH_ARROW",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SNAPABLE",
#if (LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_SNAPPABLE",
#if (LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_USER_1",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_USER_2",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_USER_3",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_USER_4",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_WIDGET_1",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OBJ_FLAG_WIDGET_2",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_0",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_10",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_100",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_20",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_30",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_40",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_50",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_60",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_70",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_80",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_90",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_COVER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_MAX",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_MIN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_OPA_TRANSP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_ANY",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_CURSOR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_INDICATOR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_ITEMS",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_KNOB",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_MAIN",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_SCROLLBAR",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_SELECTED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_PART_TEXTAREA_PLACEHOLDER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TEXTAREA)
true,
#else
false,
#endif
},
{"LV_PART_TICKS",
#if (LVGL_VERSION_MAJOR == 8) && (1)
true,
#else
false,
#endif
},
{"LV_ROLLER_MODE_INFINITE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
},
{"LV_ROLLER_MODE_NORMAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_ROLLER)
true,
#else
false,
#endif
},
{"LV_SCROLLBAR_MODE_ACTIVE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SCROLLBAR_MODE_AUTO",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SCROLLBAR_MODE_OFF",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SCROLLBAR_MODE_ON",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SCROLL_SNAP_END",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SCROLL_SNAP_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SCROLL_SNAP_START",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SIZE_CONTENT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_SLIDER_MODE_NORMAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
},
{"LV_SLIDER_MODE_RANGE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
},
{"LV_SLIDER_MODE_SYMMETRICAL",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_SLIDER)
true,
#else
false,
#endif
},
{"LV_STATE_ANY",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_CHECKED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_DEFAULT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_DISABLED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_EDITED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_FOCUSED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_FOCUS_KEY",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_HOVERED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_PRESSED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_SCROLLED",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_USER_1",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_USER_2",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_USER_3",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_STATE_USER_4",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TABLE_CELL_CTRL_MERGE_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
},
{"LV_TABLE_CELL_CTRL_TEXT_CROP",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (LV_USE_TABLE)
true,
#else
false,
#endif
},
{"LV_TEXT_ALIGN_AUTO",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TEXT_ALIGN_CENTER",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TEXT_ALIGN_LEFT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TEXT_ALIGN_RIGHT",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TEXT_DECOR_NONE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TEXT_DECOR_STRIKETHROUGH",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
{"LV_TEXT_DECOR_UNDERLINE",
#if (LVGL_VERSION_MAJOR == 8 || LVGL_VERSION_MAJOR == 9) && (1)
true,
#else
false,
#endif
},
};
