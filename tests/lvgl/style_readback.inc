/* Generated functional readback for every advertised common scalar style setter. */
run(vm, "var stylesGui = new LvGui(20, 20); var stylesObj = stylesGui.Screen.Create(\"object\");");
run(vm, "stylesObj.SetStyle(\"arc_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"arc_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"arc_rounded\", 1);");
run(vm, "stylesObj.SetStyle(\"arc_width\", 1);");
run(vm, "stylesObj.SetStyle(\"bg_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"bg_grad_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"bg_grad_stop\", 1);");
run(vm, "stylesObj.SetStyle(\"bg_main_stop\", 1);");
run(vm, "stylesObj.SetStyle(\"bg_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"border_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"border_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"border_post\", 1);");
run(vm, "stylesObj.SetStyle(\"border_width\", 1);");
run(vm, "stylesObj.SetStyle(\"clip_corner\", 1);");
run(vm, "stylesObj.SetStyle(\"color_filter_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"height\", 1);");
run(vm, "stylesObj.SetStyle(\"line_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"line_dash_gap\", 1);");
run(vm, "stylesObj.SetStyle(\"line_dash_width\", 1);");
run(vm, "stylesObj.SetStyle(\"line_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"line_rounded\", 1);");
run(vm, "stylesObj.SetStyle(\"line_width\", 1);");
run(vm, "stylesObj.SetStyle(\"max_height\", 1);");
run(vm, "stylesObj.SetStyle(\"max_width\", 1);");
run(vm, "stylesObj.SetStyle(\"min_height\", 1);");
run(vm, "stylesObj.SetStyle(\"min_width\", 1);");
run(vm, "stylesObj.SetStyle(\"opa\", 123);");
run(vm, "stylesObj.SetStyle(\"opa_layered\", 123);");
run(vm, "stylesObj.SetStyle(\"outline_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"outline_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"outline_pad\", 1);");
run(vm, "stylesObj.SetStyle(\"outline_width\", 1);");
run(vm, "stylesObj.SetStyle(\"pad_bottom\", 1);");
run(vm, "stylesObj.SetStyle(\"pad_column\", 1);");
run(vm, "stylesObj.SetStyle(\"pad_left\", 1);");
run(vm, "stylesObj.SetStyle(\"pad_right\", 1);");
run(vm, "stylesObj.SetStyle(\"pad_row\", 1);");
run(vm, "stylesObj.SetStyle(\"pad_top\", 1);");
run(vm, "stylesObj.SetStyle(\"radius\", 1);");
run(vm, "stylesObj.SetStyle(\"shadow_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"shadow_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"shadow_spread\", 1);");
run(vm, "stylesObj.SetStyle(\"shadow_width\", 1);");
run(vm, "stylesObj.SetStyle(\"text_color\", 1060944);");
run(vm, "stylesObj.SetStyle(\"text_letter_space\", 1);");
run(vm, "stylesObj.SetStyle(\"text_line_space\", 1);");
run(vm, "stylesObj.SetStyle(\"text_opa\", 123);");
run(vm, "stylesObj.SetStyle(\"transform_height\", 1);");
run(vm, "stylesObj.SetStyle(\"transform_pivot_x\", 1);");
run(vm, "stylesObj.SetStyle(\"transform_pivot_y\", 1);");
run(vm, "stylesObj.SetStyle(\"transform_width\", 1);");
run(vm, "stylesObj.SetStyle(\"translate_x\", 1);");
run(vm, "stylesObj.SetStyle(\"translate_y\", 1);");
run(vm, "stylesObj.SetStyle(\"width\", 1);");
run(vm, "stylesObj.SetStyle(\"x\", 1);");
run(vm, "stylesObj.SetStyle(\"y\", 1);");
#if LVGL_VERSION_MAJOR == 9
lv_obj_t* style_obj = lv_obj_get_child(lv_screen_active(), 0);
#else
lv_obj_t* style_obj = lv_obj_get_child(lv_scr_act(), 0);
#endif
CHECK(style_obj != NULL);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_arc_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_arc_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_arc_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_arc_rounded(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_arc_width(style_obj, LV_PART_MAIN) == 1);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_bg_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_bg_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_bg_grad_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_bg_grad_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_bg_grad_stop(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_bg_main_stop(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_bg_opa(style_obj, LV_PART_MAIN) == 123);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_border_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_border_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_border_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_border_post(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_border_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_clip_corner(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_color_filter_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_height(style_obj, LV_PART_MAIN) == 1);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_line_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_line_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_line_dash_gap(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_line_dash_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_line_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_line_rounded(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_line_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_max_height(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_max_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_min_height(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_min_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_opa_layered(style_obj, LV_PART_MAIN) == 123);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_outline_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_outline_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_outline_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_outline_pad(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_outline_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_pad_bottom(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_pad_column(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_pad_left(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_pad_right(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_pad_row(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_pad_top(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_radius(style_obj, LV_PART_MAIN) == 1);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_shadow_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_shadow_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_shadow_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_shadow_spread(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_shadow_width(style_obj, LV_PART_MAIN) == 1);
#if LVGL_VERSION_MAJOR == 9
CHECK(lv_color_eq(lv_obj_get_style_text_color(style_obj, LV_PART_MAIN), lv_color_hex(1060944)));
#else
CHECK(lv_color_to32(lv_obj_get_style_text_color(style_obj, LV_PART_MAIN)) == lv_color_to32(lv_color_hex(1060944)));
#endif
CHECK(lv_obj_get_style_text_letter_space(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_text_line_space(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_text_opa(style_obj, LV_PART_MAIN) == 123);
CHECK(lv_obj_get_style_transform_height(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_transform_pivot_x(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_transform_pivot_y(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_transform_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_translate_x(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_translate_y(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_width(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_x(style_obj, LV_PART_MAIN) == 1);
CHECK(lv_obj_get_style_y(style_obj, LV_PART_MAIN) == 1);
run(vm, "stylesGui.Dispose();");
