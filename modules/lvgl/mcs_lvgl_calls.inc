/* Generated from actual public LVGL8/9 widget declarations; safe common subset.
 * Receiver kind checked, exact arity/types/ranges checked; no raw pointers. */
#if LV_USE_ANIMIMG
    if (!strcmp(s->kind, "animimg") && !strcmp(method, "set_duration")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "animimg.set_duration: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "animimg.set_duration argument 1: 0..4294967295"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_animimg_set_duration(s->obj, (uint32_t)a0);
        return mcs_null();
#else
        lv_animimg_set_duration(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ANIMIMG
    if (!strcmp(s->kind, "animimg") && !strcmp(method, "set_repeat_count")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "animimg.set_repeat_count: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "animimg.set_repeat_count argument 1: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_animimg_set_repeat_count(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_animimg_set_repeat_count(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_angle_end")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_angle_end: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_angle_end(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_angle_end(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_angle_start")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_angle_start: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_angle_start(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_angle_start(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_bg_angle_end")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_bg_angle_end: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_bg_angle_end(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_bg_angle_end(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_bg_angle_start")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_bg_angle_start: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_bg_angle_start(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_bg_angle_start(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_max_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_max_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_max_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_max_value(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_min_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_min_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_min_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_min_value(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "get_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "arc.get_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_arc_get_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_arc_get_value(s->obj));
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_angles")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "arc.set_angles: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_angles argument 1: 0..32767"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_angles argument 2: 0..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_angles(s->obj, (uint16_t)a0, (uint16_t)a1);
        return mcs_null();
#else
        lv_arc_set_angles(s->obj, (lv_value_precise_t)a0, (lv_value_precise_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_bg_angles")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "arc.set_bg_angles: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_bg_angles argument 1: 0..32767"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_bg_angles argument 2: 0..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_bg_angles(s->obj, (uint16_t)a0, (uint16_t)a1);
        return mcs_null();
#else
        lv_arc_set_bg_angles(s->obj, (lv_value_precise_t)a0, (lv_value_precise_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_bg_end_angle")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_bg_end_angle: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_bg_end_angle argument 1: 0..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_bg_end_angle(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_arc_set_bg_end_angle(s->obj, (lv_value_precise_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_bg_start_angle")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_bg_start_angle: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_bg_start_angle argument 1: 0..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_bg_start_angle(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_arc_set_bg_start_angle(s->obj, (lv_value_precise_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_change_rate")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_change_rate: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_change_rate argument 1: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_change_rate(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_arc_set_change_rate(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_end_angle")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_end_angle: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_end_angle argument 1: 0..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_end_angle(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_arc_set_end_angle(s->obj, (lv_value_precise_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_range")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "arc.set_range: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_range argument 1: -32768..32767"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_range argument 2: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
        if (a0 >= a1) { mcs_raise(vm, "ArgumentException", "range minimum must be below maximum"); return mcs_null(); }
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_range(s->obj, (int16_t)a0, (int16_t)a1);
        return mcs_null();
#else
        lv_arc_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_rotation")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_rotation: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_rotation argument 1: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_rotation(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_arc_set_rotation(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_start_angle")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_start_angle: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_start_angle argument 1: 0..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_start_angle(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_arc_set_start_angle(s->obj, (lv_value_precise_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ARC
    if (!strcmp(s->kind, "arc") && !strcmp(method, "set_value")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "arc.set_value: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "arc.set_value argument 1: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_arc_set_value(s->obj, (int16_t)a0);
        return mcs_null();
#else
        lv_arc_set_value(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "get_max_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "bar.get_max_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_bar_get_max_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_bar_get_max_value(s->obj));
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "get_min_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "bar.get_min_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_bar_get_min_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_bar_get_min_value(s->obj));
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "get_start_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "bar.get_start_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_bar_get_start_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_bar_get_start_value(s->obj));
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "get_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "bar.get_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_bar_get_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_bar_get_value(s->obj));
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "set_range")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "bar.set_range: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "bar.set_range argument 1: -2147483648..2147483647"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "bar.set_range argument 2: -2147483648..2147483647"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
        if (a0 >= a1) { mcs_raise(vm, "ArgumentException", "range minimum must be below maximum"); return mcs_null(); }
#if LVGL_VERSION_MAJOR == 8
        lv_bar_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#else
        lv_bar_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "set_start_value")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "bar.set_start_value: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "bar.set_start_value argument 1: -2147483648..2147483647"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(1))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "bar.set_start_value argument 2: 0..1"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_bar_set_start_value(s->obj, (int32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#else
        lv_bar_set_start_value(s->obj, (int32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_BAR
    if (!strcmp(s->kind, "bar") && !strcmp(method, "set_value")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "bar.set_value: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "bar.set_value argument 1: -2147483648..2147483647"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(1))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "bar.set_value argument 2: 0..1"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_bar_set_value(s->obj, (int32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#else
        lv_bar_set_value(s->obj, (int32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_CALENDAR
    if (!strcmp(s->kind, "calendar") && !strcmp(method, "set_today_date")) {
        if (argc != 4) { mcs_raise(vm, "ArgumentException", "calendar.set_today_date: expected 3 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(1), INT64_C(9999))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "calendar.set_today_date argument 1: 1..9999"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(1), INT64_C(12))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "calendar.set_today_date argument 2: 1..12"); return mcs_null();
        }
        mcs_int_t a2 = mcs_to_int(vm, argv[3]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a2, INT64_C(1), INT64_C(31))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "calendar.set_today_date argument 3: 1..31"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_calendar_set_today_date(s->obj, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2);
        return mcs_null();
#else
        lv_calendar_set_today_date(s->obj, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_CHART
    if (!strcmp(s->kind, "chart") && !strcmp(method, "get_point_count")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "chart.get_point_count: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_chart_get_point_count(s->obj));
#else
        return mcs_int((mcs_int_t)lv_chart_get_point_count(s->obj));
#endif
    }
#endif
#if LV_USE_CHART
    if (!strcmp(s->kind, "chart") && !strcmp(method, "get_pressed_point")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "chart.get_pressed_point: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_chart_get_pressed_point(s->obj));
#else
        return mcs_int((mcs_int_t)lv_chart_get_pressed_point(s->obj));
#endif
    }
#endif
#if LV_USE_CHART
    if (!strcmp(s->kind, "chart") && !strcmp(method, "refresh")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "chart.refresh: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_chart_refresh(s->obj);
        return mcs_null();
#else
        lv_chart_refresh(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_CHART
    if (!strcmp(s->kind, "chart") && !strcmp(method, "set_div_line_count")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "chart.set_div_line_count: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(512))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "chart.set_div_line_count argument 1: 0..512"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(512))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "chart.set_div_line_count argument 2: 0..512"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_chart_set_div_line_count(s->obj, (uint8_t)a0, (uint8_t)a1);
        return mcs_null();
#else
        lv_chart_set_div_line_count(s->obj, (uint32_t)a0, (uint32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_CHART
    if (!strcmp(s->kind, "chart") && !strcmp(method, "set_point_count")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "chart.set_point_count: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(1), INT64_C(512))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "chart.set_point_count argument 1: 1..512"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_chart_set_point_count(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_chart_set_point_count(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(s->kind, "checkbox") && !strcmp(method, "get_text")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "checkbox.get_text: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_checkbox_get_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_checkbox_get_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_CHECKBOX
    if (!strcmp(s->kind, "checkbox") && !strcmp(method, "set_text")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "checkbox.set_text: expected 1 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_checkbox_set_text(s->obj, a0);
        return mcs_null();
#else
        lv_checkbox_set_text(s->obj, a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "add_option")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "dropdown.add_option: expected 2 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "dropdown.add_option argument 2: 0..4294967295"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_add_option(s->obj, a0, (uint32_t)a1);
        return mcs_null();
#else
        lv_dropdown_add_option(s->obj, a0, (uint32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "clear_options")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.clear_options: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_clear_options(s->obj);
        return mcs_null();
#else
        lv_dropdown_clear_options(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "close")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.close: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_close(s->obj);
        return mcs_null();
#else
        lv_dropdown_close(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "get_options")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.get_options: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_dropdown_get_options(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_dropdown_get_options(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "get_selected")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.get_selected: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_dropdown_get_selected(s->obj));
#else
        return mcs_int((mcs_int_t)lv_dropdown_get_selected(s->obj));
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "get_selected_highlight")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.get_selected_highlight: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_dropdown_get_selected_highlight(s->obj));
#else
        return mcs_bool(lv_dropdown_get_selected_highlight(s->obj));
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "get_symbol")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.get_symbol: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_dropdown_get_symbol(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_dropdown_get_symbol(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "get_text")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.get_text: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_dropdown_get_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_dropdown_get_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "is_open")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.is_open: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_dropdown_is_open(s->obj));
#else
        return mcs_bool(lv_dropdown_is_open(s->obj));
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "open")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "dropdown.open: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_open(s->obj);
        return mcs_null();
#else
        lv_dropdown_open(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "set_options")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "dropdown.set_options: expected 1 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_set_options(s->obj, a0);
        return mcs_null();
#else
        lv_dropdown_set_options(s->obj, a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "set_selected")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "dropdown.set_selected: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "dropdown.set_selected argument 1: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_set_selected(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_dropdown_set_selected(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_DROPDOWN
    if (!strcmp(s->kind, "dropdown") && !strcmp(method, "set_selected_highlight")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "dropdown.set_selected_highlight: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_dropdown_set_selected_highlight(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_dropdown_set_selected_highlight(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "get_antialias")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "image.get_antialias: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_img_get_antialias(s->obj));
#else
        return mcs_bool(lv_image_get_antialias(s->obj));
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "get_offset_x")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "image.get_offset_x: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_img_get_offset_x(s->obj));
#else
        return mcs_int((mcs_int_t)lv_image_get_offset_x(s->obj));
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "get_offset_y")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "image.get_offset_y: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_img_get_offset_y(s->obj));
#else
        return mcs_int((mcs_int_t)lv_image_get_offset_y(s->obj));
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "set_antialias")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "image.set_antialias: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_img_set_antialias(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_image_set_antialias(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "set_offset_x")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "image.set_offset_x: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "image.set_offset_x argument 1: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_img_set_offset_x(s->obj, (lv_coord_t)a0);
        return mcs_null();
#else
        lv_image_set_offset_x(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "set_offset_y")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "image.set_offset_y: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "image.set_offset_y argument 1: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_img_set_offset_y(s->obj, (lv_coord_t)a0);
        return mcs_null();
#else
        lv_image_set_offset_y(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if (LVGL_VERSION_MAJOR == 8 && LV_USE_IMG) || (LVGL_VERSION_MAJOR == 9 && LV_USE_IMAGE)
    if (!strcmp(s->kind, "image") && !strcmp(method, "set_pivot")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "image.set_pivot: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "image.set_pivot argument 1: -32768..32767"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "image.set_pivot argument 2: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_img_set_pivot(s->obj, (lv_coord_t)a0, (lv_coord_t)a1);
        return mcs_null();
#else
        lv_image_set_pivot(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_KEYBOARD
    if (!strcmp(s->kind, "keyboard") && !strcmp(method, "set_popovers")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "keyboard.set_popovers: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_keyboard_set_popovers(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_keyboard_set_popovers(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "cut_text")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "label.cut_text: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "label.cut_text argument 1: 0..4294967295"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "label.cut_text argument 2: 0..4294967295"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_label_cut_text(s->obj, (uint32_t)a0, (uint32_t)a1);
        return mcs_null();
#else
        lv_label_cut_text(s->obj, (uint32_t)a0, (uint32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "get_recolor")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "label.get_recolor: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_label_get_recolor(s->obj));
#else
        return mcs_bool(lv_label_get_recolor(s->obj));
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "get_text_selection_end")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "label.get_text_selection_end: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_label_get_text_selection_end(s->obj));
#else
        return mcs_int((mcs_int_t)lv_label_get_text_selection_end(s->obj));
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "get_text_selection_start")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "label.get_text_selection_start: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_label_get_text_selection_start(s->obj));
#else
        return mcs_int((mcs_int_t)lv_label_get_text_selection_start(s->obj));
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "ins_text")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "label.ins_text: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "label.ins_text argument 1: 0..4294967295"); return mcs_null();
        }
        const char* a1 = mcs_to_cstr(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_label_ins_text(s->obj, (uint32_t)a0, a1);
        return mcs_null();
#else
        lv_label_ins_text(s->obj, (uint32_t)a0, a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "set_recolor")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "label.set_recolor: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_label_set_recolor(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_label_set_recolor(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LABEL
    if (!strcmp(s->kind, "label") && !strcmp(method, "set_text")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "label.set_text: expected 1 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_label_set_text(s->obj, a0);
        return mcs_null();
#else
        lv_label_set_text(s->obj, a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LED
    if (!strcmp(s->kind, "led") && !strcmp(method, "get_brightness")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "led.get_brightness: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_led_get_brightness(s->obj));
#else
        return mcs_int((mcs_int_t)lv_led_get_brightness(s->obj));
#endif
    }
#endif
#if LV_USE_LED
    if (!strcmp(s->kind, "led") && !strcmp(method, "off")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "led.off: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_led_off(s->obj);
        return mcs_null();
#else
        lv_led_off(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LED
    if (!strcmp(s->kind, "led") && !strcmp(method, "on")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "led.on: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_led_on(s->obj);
        return mcs_null();
#else
        lv_led_on(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LED
    if (!strcmp(s->kind, "led") && !strcmp(method, "set_brightness")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "led.set_brightness: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(255))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "led.set_brightness argument 1: 0..255"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_led_set_brightness(s->obj, (uint8_t)a0);
        return mcs_null();
#else
        lv_led_set_brightness(s->obj, (uint8_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LED
    if (!strcmp(s->kind, "led") && !strcmp(method, "set_color")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "led.set_color: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(16777215))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "led.set_color argument 1: 0..16777215"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_led_set_color(s->obj, lv_color_hex((uint32_t)a0));
        return mcs_null();
#else
        lv_led_set_color(s->obj, lv_color_hex((uint32_t)a0));
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LED
    if (!strcmp(s->kind, "led") && !strcmp(method, "toggle")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "led.toggle: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_led_toggle(s->obj);
        return mcs_null();
#else
        lv_led_toggle(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_LINE
    if (!strcmp(s->kind, "line") && !strcmp(method, "get_y_invert")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "line.get_y_invert: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_line_get_y_invert(s->obj));
#else
        return mcs_bool(lv_line_get_y_invert(s->obj));
#endif
    }
#endif
#if LV_USE_LINE
    if (!strcmp(s->kind, "line") && !strcmp(method, "set_y_invert")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "line.set_y_invert: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_line_set_y_invert(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_line_set_y_invert(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_MENU
    if (!strcmp(s->kind, "menu") && !strcmp(method, "clear_history")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "menu.clear_history: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_menu_clear_history(s->obj);
        return mcs_null();
#else
        lv_menu_clear_history(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ROLLER
    if (!strcmp(s->kind, "roller") && !strcmp(method, "get_options")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "roller.get_options: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_roller_get_options(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_roller_get_options(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_ROLLER
    if (!strcmp(s->kind, "roller") && !strcmp(method, "get_selected")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "roller.get_selected: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_roller_get_selected(s->obj));
#else
        return mcs_int((mcs_int_t)lv_roller_get_selected(s->obj));
#endif
    }
#endif
#if LV_USE_ROLLER
    if (!strcmp(s->kind, "roller") && !strcmp(method, "set_selected")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "roller.set_selected: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "roller.set_selected argument 1: 0..65535"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(1))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "roller.set_selected argument 2: 0..1"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_roller_set_selected(s->obj, (uint16_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#else
        lv_roller_set_selected(s->obj, (uint32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_ROLLER
    if (!strcmp(s->kind, "roller") && !strcmp(method, "set_visible_row_count")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "roller.set_visible_row_count: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(255))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "roller.set_visible_row_count argument 1: 0..255"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_roller_set_visible_row_count(s->obj, (uint8_t)a0);
        return mcs_null();
#else
        lv_roller_set_visible_row_count(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "get_left_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "slider.get_left_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_slider_get_left_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_slider_get_left_value(s->obj));
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "get_max_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "slider.get_max_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_slider_get_max_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_slider_get_max_value(s->obj));
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "get_min_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "slider.get_min_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_slider_get_min_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_slider_get_min_value(s->obj));
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "get_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "slider.get_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_slider_get_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_slider_get_value(s->obj));
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "is_dragged")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "slider.is_dragged: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_slider_is_dragged(s->obj));
#else
        return mcs_bool(lv_slider_is_dragged(s->obj));
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "set_range")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "slider.set_range: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "slider.set_range argument 1: -2147483648..2147483647"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "slider.set_range argument 2: -2147483648..2147483647"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
        if (a0 >= a1) { mcs_raise(vm, "ArgumentException", "range minimum must be below maximum"); return mcs_null(); }
#if LVGL_VERSION_MAJOR == 8
        lv_slider_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#else
        lv_slider_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SLIDER
    if (!strcmp(s->kind, "slider") && !strcmp(method, "set_value")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "slider.set_value: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "slider.set_value argument 1: -2147483648..2147483647"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(1))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "slider.set_value argument 2: 0..1"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_slider_set_value(s->obj, (int32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#else
        lv_slider_set_value(s->obj, (int32_t)a0, (lv_anim_enable_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPAN
    if (!strcmp(s->kind, "spangroup") && !strcmp(method, "get_expand_height")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spangroup.get_expand_height: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spangroup.get_expand_height argument 1: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_spangroup_get_expand_height(s->obj, (lv_coord_t)a0));
#else
        return mcs_int((mcs_int_t)lv_spangroup_get_expand_height(s->obj, (int32_t)a0));
#endif
    }
#endif
#if LV_USE_SPAN
    if (!strcmp(s->kind, "spangroup") && !strcmp(method, "get_expand_width")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spangroup.get_expand_width: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spangroup.get_expand_width argument 1: 0..4294967295"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_spangroup_get_expand_width(s->obj, (uint32_t)a0));
#else
        return mcs_int((mcs_int_t)lv_spangroup_get_expand_width(s->obj, (uint32_t)a0));
#endif
    }
#endif
#if LV_USE_SPAN
    if (!strcmp(s->kind, "spangroup") && !strcmp(method, "get_indent")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spangroup.get_indent: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_spangroup_get_indent(s->obj));
#else
        return mcs_int((mcs_int_t)lv_spangroup_get_indent(s->obj));
#endif
    }
#endif
#if LV_USE_SPAN
    if (!strcmp(s->kind, "spangroup") && !strcmp(method, "set_indent")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spangroup.set_indent: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-32768), INT64_C(32767))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spangroup.set_indent argument 1: -32768..32767"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spangroup_set_indent(s->obj, (lv_coord_t)a0);
        return mcs_null();
#else
        lv_spangroup_set_indent(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "decrement")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.decrement: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_decrement(s->obj);
        return mcs_null();
#else
        lv_spinbox_decrement(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "get_rollover")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.get_rollover: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_spinbox_get_rollover(s->obj));
#else
        return mcs_bool(lv_spinbox_get_rollover(s->obj));
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "get_step")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.get_step: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_spinbox_get_step(s->obj));
#else
        return mcs_int((mcs_int_t)lv_spinbox_get_step(s->obj));
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "get_value")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.get_value: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_spinbox_get_value(s->obj));
#else
        return mcs_int((mcs_int_t)lv_spinbox_get_value(s->obj));
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "increment")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.increment: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_increment(s->obj);
        return mcs_null();
#else
        lv_spinbox_increment(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "set_cursor_pos")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spinbox.set_cursor_pos: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(255))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_cursor_pos argument 1: 0..255"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_set_cursor_pos(s->obj, (uint8_t)a0);
        return mcs_null();
#else
        lv_spinbox_set_cursor_pos(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "set_digit_format")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "spinbox.set_digit_format: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(1), INT64_C(9))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_digit_format argument 1: 1..9"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(9))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_digit_format argument 2: 0..9"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
        if (a1 > a0) { mcs_raise(vm, "ArgumentException", "separator position exceeds digit count"); return mcs_null(); }
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_set_digit_format(s->obj, (uint8_t)a0, (uint8_t)a1);
        return mcs_null();
#else
        lv_spinbox_set_digit_format(s->obj, (uint32_t)a0, (uint32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "set_range")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "spinbox.set_range: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_range argument 1: -2147483648..2147483647"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_range argument 2: -2147483648..2147483647"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
        if (a0 >= a1) { mcs_raise(vm, "ArgumentException", "range minimum must be below maximum"); return mcs_null(); }
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#else
        lv_spinbox_set_range(s->obj, (int32_t)a0, (int32_t)a1);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "set_rollover")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spinbox.set_rollover: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_set_rollover(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_spinbox_set_rollover(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "set_step")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spinbox.set_step: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_step argument 1: 0..4294967295"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_set_step(s->obj, (uint32_t)a0);
        return mcs_null();
#else
        lv_spinbox_set_step(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "set_value")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "spinbox.set_value: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "spinbox.set_value argument 1: -2147483648..2147483647"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_set_value(s->obj, (int32_t)a0);
        return mcs_null();
#else
        lv_spinbox_set_value(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "step_next")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.step_next: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_step_next(s->obj);
        return mcs_null();
#else
        lv_spinbox_step_next(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_SPINBOX
    if (!strcmp(s->kind, "spinbox") && !strcmp(method, "step_prev")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "spinbox.step_prev: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_spinbox_step_prev(s->obj);
        return mcs_null();
#else
        lv_spinbox_step_prev(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TABLE
    if (!strcmp(s->kind, "table") && !strcmp(method, "get_cell_value")) {
        if (argc != 3) { mcs_raise(vm, "ArgumentException", "table.get_cell_value: expected 2 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "table.get_cell_value argument 1: 0..65535"); return mcs_null();
        }
        mcs_int_t a1 = mcs_to_int(vm, argv[2]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a1, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "table.get_cell_value argument 2: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_table_get_cell_value(s->obj, (uint16_t)a0, (uint16_t)a1);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_table_get_cell_value(s->obj, (uint32_t)a0, (uint32_t)a1);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "add_char")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.add_char: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(4294967295))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "textarea.add_char argument 1: 0..4294967295"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_add_char(s->obj, (uint32_t)a0);
        return mcs_null();
#else
        lv_textarea_add_char(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "add_text")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.add_text: expected 1 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_add_text(s->obj, a0);
        return mcs_null();
#else
        lv_textarea_add_text(s->obj, a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "clear_selection")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.clear_selection: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_clear_selection(s->obj);
        return mcs_null();
#else
        lv_textarea_clear_selection(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "cursor_down")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.cursor_down: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_cursor_down(s->obj);
        return mcs_null();
#else
        lv_textarea_cursor_down(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "cursor_left")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.cursor_left: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_cursor_left(s->obj);
        return mcs_null();
#else
        lv_textarea_cursor_left(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "cursor_right")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.cursor_right: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_cursor_right(s->obj);
        return mcs_null();
#else
        lv_textarea_cursor_right(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "cursor_up")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.cursor_up: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_cursor_up(s->obj);
        return mcs_null();
#else
        lv_textarea_cursor_up(s->obj);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_accepted_chars")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_accepted_chars: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_textarea_get_accepted_chars(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_textarea_get_accepted_chars(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_cursor_click_pos")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_cursor_click_pos: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_textarea_get_cursor_click_pos(s->obj));
#else
        return mcs_bool(lv_textarea_get_cursor_click_pos(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_cursor_pos")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_cursor_pos: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_textarea_get_cursor_pos(s->obj));
#else
        return mcs_int((mcs_int_t)lv_textarea_get_cursor_pos(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_max_length")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_max_length: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_textarea_get_max_length(s->obj));
#else
        return mcs_int((mcs_int_t)lv_textarea_get_max_length(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_one_line")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_one_line: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_textarea_get_one_line(s->obj));
#else
        return mcs_bool(lv_textarea_get_one_line(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_password_bullet")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_password_bullet: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_textarea_get_password_bullet(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_textarea_get_password_bullet(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_password_mode")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_password_mode: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_textarea_get_password_mode(s->obj));
#else
        return mcs_bool(lv_textarea_get_password_mode(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_password_show_time")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_password_show_time: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_int((mcs_int_t)lv_textarea_get_password_show_time(s->obj));
#else
        return mcs_int((mcs_int_t)lv_textarea_get_password_show_time(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_placeholder_text")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_placeholder_text: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_textarea_get_placeholder_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_textarea_get_placeholder_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_text")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_text: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        const char* text = lv_textarea_get_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#else
        const char* text = lv_textarea_get_text(s->obj);
        return text ? mcs_string(vm, text) : mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "get_text_selection")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.get_text_selection: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_textarea_get_text_selection(s->obj));
#else
        return mcs_bool(lv_textarea_get_text_selection(s->obj));
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_cursor_click_pos")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_cursor_click_pos: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_cursor_click_pos(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_textarea_set_cursor_click_pos(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_cursor_pos")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_cursor_pos: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(-2147483648), INT64_C(2147483647))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "textarea.set_cursor_pos argument 1: -2147483648..2147483647"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_cursor_pos(s->obj, (int32_t)a0);
        return mcs_null();
#else
        lv_textarea_set_cursor_pos(s->obj, (int32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_max_length")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_max_length: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "textarea.set_max_length argument 1: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_max_length(s->obj, (uint32_t)a0);
        return mcs_null();
#else
        lv_textarea_set_max_length(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_one_line")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_one_line: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_one_line(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_textarea_set_one_line(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_password_mode")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_password_mode: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_password_mode(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_textarea_set_password_mode(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_password_show_time")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_password_show_time: expected 1 arguments"); return mcs_null(); }
        mcs_int_t a0 = mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!number_range((int64_t)a0, INT64_C(0), INT64_C(65535))) {
            mcs_raise(vm, "ArgumentOutOfRangeException", "textarea.set_password_show_time argument 1: 0..65535"); return mcs_null();
        }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_password_show_time(s->obj, (uint16_t)a0);
        return mcs_null();
#else
        lv_textarea_set_password_show_time(s->obj, (uint32_t)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_placeholder_text")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_placeholder_text: expected 1 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_placeholder_text(s->obj, a0);
        return mcs_null();
#else
        lv_textarea_set_placeholder_text(s->obj, a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_text")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_text: expected 1 arguments"); return mcs_null(); }
        const char* a0 = mcs_to_cstr(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_text(s->obj, a0);
        return mcs_null();
#else
        lv_textarea_set_text(s->obj, a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "set_text_selection")) {
        if (argc != 2) { mcs_raise(vm, "ArgumentException", "textarea.set_text_selection: expected 1 arguments"); return mcs_null(); }
        bool a0 = mcs_to_bool(vm, argv[1]);
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        lv_textarea_set_text_selection(s->obj, (bool)a0);
        return mcs_null();
#else
        lv_textarea_set_text_selection(s->obj, (bool)a0);
        return mcs_null();
#endif
    }
#endif
#if LV_USE_TEXTAREA
    if (!strcmp(s->kind, "textarea") && !strcmp(method, "text_is_selected")) {
        if (argc != 1) { mcs_raise(vm, "ArgumentException", "textarea.text_is_selected: expected 0 arguments"); return mcs_null(); }
        if (mcs_has_exception(vm)) return mcs_null();
#if LVGL_VERSION_MAJOR == 8
        return mcs_bool(lv_textarea_text_is_selected(s->obj));
#else
        return mcs_bool(lv_textarea_text_is_selected(s->obj));
#endif
    }
#endif
