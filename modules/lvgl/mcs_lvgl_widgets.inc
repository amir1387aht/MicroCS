/* Common real upstream constructors; config-disabled families are absent. */
#if LV_USE_ARC
    if (!strcmp(name, "arc")) return lv_arc_create(parent);
#endif
#if LV_USE_BAR
    if (!strcmp(name, "bar")) return lv_bar_create(parent);
#endif
#if LV_USE_SLIDER
    if (!strcmp(name, "slider")) return lv_slider_create(parent);
#endif
#if LV_USE_SWITCH
    if (!strcmp(name, "switch")) return lv_switch_create(parent);
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(name, "checkbox")) return lv_checkbox_create(parent);
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(name, "dropdown")) return lv_dropdown_create(parent);
#endif
#if LV_USE_ROLLER
    if (!strcmp(name, "roller")) return lv_roller_create(parent);
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(name, "textarea")) return lv_textarea_create(parent);
#endif
#if LV_USE_TABLE
    if (!strcmp(name, "table")) return lv_table_create(parent);
#endif
#if LV_USE_CHART
    if (!strcmp(name, "chart")) return lv_chart_create(parent);
#endif
#if LV_USE_CANVAS
    if (!strcmp(name, "canvas")) return lv_canvas_create(parent);
#endif
#if LV_USE_LINE
    if (!strcmp(name, "line")) return lv_line_create(parent);
#endif
#if LV_USE_LIST
    if (!strcmp(name, "list")) return lv_list_create(parent);
#endif
#if LV_USE_MENU
    if (!strcmp(name, "menu")) return lv_menu_create(parent);
#endif
#if LV_USE_KEYBOARD
    if (!strcmp(name, "keyboard")) return lv_keyboard_create(parent);
#endif
#if LV_USE_SPINBOX
    if (!strcmp(name, "spinbox")) return lv_spinbox_create(parent);
#endif
#if LV_USE_LED
    if (!strcmp(name, "led")) return lv_led_create(parent);
#endif
#if LV_USE_CALENDAR
    if (!strcmp(name, "calendar")) return lv_calendar_create(parent);
#endif
#if LV_USE_TILEVIEW
    if (!strcmp(name, "tileview")) return lv_tileview_create(parent);
#endif
#if LV_USE_SPAN
    if (!strcmp(name, "spangroup")) return lv_spangroup_create(parent);
#endif
#if LV_USE_ANIMIMG
    if (!strcmp(name, "animimg")) return lv_animimg_create(parent);
#endif
#if LV_USE_TABVIEW
    if (!strcmp(name, "tabview")) {
#if LVGL_VERSION_MAJOR == 8
        return lv_tabview_create(parent, LV_DIR_TOP, 40);
#else
        return lv_tabview_create(parent);
#endif
    }
#endif
#if LV_USE_WIN
    if (!strcmp(name, "win")) {
#if LVGL_VERSION_MAJOR == 8
        return lv_win_create(parent, 40);
#else
        return lv_win_create(parent);
#endif
    }
#endif
#if LV_USE_SPINNER
    if (!strcmp(name, "spinner")) {
#if LVGL_VERSION_MAJOR == 8
        return lv_spinner_create(parent, 1000, 60);
#else
        lv_obj_t* obj = lv_spinner_create(parent);
        if (obj) lv_spinner_set_anim_params(obj, 1000, 60);
        return obj;
#endif
    }
#endif
