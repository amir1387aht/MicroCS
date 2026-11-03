/* mcs_lvgl_more.inc - hand-written wrappers, part 2 (included by mcs_lvgl.c, never compiled alone).
 *
 * Everything here needs C-side storage or callbacks, so the generated 1:1 wrapper is not enough:
 *   arrays LVGL keeps pointing to  chart ext arrays, grid dsc arrays, calendar dates, canvas buffer,
 *                                  style transition properties, string / pointer subjects
 *                                  -> copied into "slots": storage keyed by (kind, object, ...), freed when the VM closes,
 *                                     re-used when the same function is called again for the same object
 *   callbacks                      observers (9.x), lv_obj_tree_walk, lv_log_register_print_cb, lv_msg (8.x),
 *                                  anim custom_exec / deleted / get_value
 *   buffers LVGL fills             lv_dropdown_get_selected_str / lv_roller_get_selected_str (byte[] and a string result) */

#if LVGL_VERSION_MAJOR >= 9
typedef int32_t lvb_coord_t;
#else
typedef lv_coord_t lvb_coord_t;
#endif

enum { SL_CHART_X = 1, SL_CHART_Y, SL_GRID_COL, SL_GRID_ROW, SL_STYLE_COL, SL_STYLE_ROW, SL_OSTYLE_COL, SL_OSTYLE_ROW,
       SL_CAL, SL_CANVAS, SL_TRANS, SL_SUBJ_STR, SL_SUBJ_PREV, SL_SUBJ_PTR };

static lvb_slot_t* slot_find(lvb_state_t* st, int kind, const void* k1, const void* k2) {
    for (lvb_slot_t* s = st->slots; s; s = s->next)
        if (s->kind == kind && s->k1 == k1 && s->k2 == k2) return s;
    return NULL;
}
/* zeroed storage of `size` bytes aligned to `align` (power of two) */
static void* slot_block(mcs_vm_t* vm, int kind, const void* k1, const void* k2, size_t size, size_t align) {
    lvb_state_t* st = ST(vm);
    lvb_slot_t* s = slot_find(st, kind, k1, k2);
    size_t need = size + align;
    if (s && s->size < need) { LVB_FREE(s->raw); s->raw = NULL; s->size = 0; }
    if (!s) {
        s = (lvb_slot_t*)LVB_MALLOC(sizeof *s);
        if (!s) { mcs_raise(vm, "OutOfMemoryException", "LVGL array"); return NULL; }
        memset(s, 0, sizeof *s);
        s->kind = kind; s->k1 = k1; s->k2 = k2;
        s->next = st->slots; st->slots = s;
    }
    if (!s->raw) {
        s->raw = LVB_MALLOC(need);
        if (!s->raw) { mcs_raise(vm, "OutOfMemoryException", "LVGL array"); return NULL; }
        s->size = need;
    }
    memset(s->raw, 0, s->size);
    s->mem = (void*)(((uintptr_t)s->raw + align - 1u) & ~(uintptr_t)(align - 1u));
    return s->mem;
}
static int is_seq(mcs_value_t v) { int k = mcs_obj_kind(v); return k == MCS_O_ARRAY || k == MCS_O_LIST; }
#if MCS_LV_VER >= 80300
static int val_same(mcs_value_t a, mcs_value_t b) {
    return a.type == b.type && (a.type < MCS_T_OBJ ? a.as.i == b.as.i : a.as.o == b.as.o);
}
#endif
#if LVGL_VERSION_MAJOR >= 9 || (MCS_LV_VER >= 80300 && defined(LV_USE_MSG) && LV_USE_MSG)
static lvb_cb_t* cb_find(lvb_state_t* st, const void* p, int kind) {
    for (lvb_cb_t* c = st->cbs; c; c = c->next) if ((const void*)c == p && c->kind == kind) return c;
    return NULL;
}
#endif
/* like cb_call, but returns the callback's result (ok = 0 when it did not run or failed) */
static int cb_call_ret(lvb_state_t* st, int fn_idx, int argc, mcs_value_t* argv, mcs_value_t* res) {
    mcs_vm_t* vm = st->vm;
    if (st->closing || fn_idx < 0 || mcs_has_exception(vm)) return 0;
    mcs_value_t f = kept(st, fn_idx);
    int ar = mcs_arity(f);
    if (ar >= 0 && ar < argc) argc = ar;
    st->event_depth++;
    mcs_result_t r = mcs_call_value(vm, f, argc, argv, res);
    st->event_depth--;
    if (r != MCS_OK) { if (!mcs_running(vm)) mcs_fail(vm, r, "%s", mcs_last_error(vm)); return 0; }
    return 1;
}
/* int[] / lv_coord_t[] -> storage of at least `min_count` entries; `grid_end`: add LV_GRID_TEMPLATE_LAST */
static lvb_coord_t* coord_array(mcs_vm_t* vm, int kind, const void* k1, const void* k2, mcs_value_t arr, uint32_t min_count, int grid_end) {
    if (!is_seq(arr)) { mcs_raise(vm, "ArgumentException", "int[] expected"); return NULL; }
    uint32_t n = mcs_len(arr);
    uint32_t total = (n > min_count ? n : min_count) + (grid_end ? 1u : 0u);
    lvb_coord_t* m = (lvb_coord_t*)slot_block(vm, kind, k1, k2, (size_t)(total ? total : 1u) * sizeof(lvb_coord_t), 8);
    if (!m) return NULL;
    for (uint32_t i = 0; i < n; i++) m[i] = (lvb_coord_t)mcs_to_int(vm, mcs_index(arr, i));
    if (grid_end) m[total - 1u] = (lvb_coord_t)LV_GRID_TEMPLATE_LAST;
    return mcs_has_exception(vm) ? NULL : m;
}

/* ---- chart: external arrays (lv_chart_set_ext_y_array(chart, series, int[]) - call it again after changing the C# array) */
#if LV_USE_CHART
static mcs_value_t chart_ext(mcs_vm_t* vm, mcs_value_t* argv, int kind) {
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    lv_chart_series_t* ser = (lv_chart_series_t*)lvb_arg_ptr(vm, argv[1], LVB_C_lv_chart_series_t, 0);
    if (!obj || !ser || mcs_has_exception(vm)) return mcs_null();
    if (kind == SL_CHART_X && lv_chart_get_type(obj) != LV_CHART_TYPE_SCATTER) {   /* LVGL only owns x arrays of scatter charts */
        mcs_raise(vm, "InvalidOperationException", "lv_chart_set_ext_x_array needs LV_CHART_TYPE_SCATTER");
        return mcs_null();
    }
    lvb_coord_t* m = coord_array(vm, kind, obj, ser, argv[2], (uint32_t)lv_chart_get_point_count(obj), 0);
    if (!m) return mcs_null();
    if (kind == SL_CHART_X) lv_chart_set_ext_x_array(obj, ser, m); else lv_chart_set_ext_y_array(obj, ser, m);
    return mcs_null();
}
static mcs_value_t lvo_lv_chart_set_ext_x_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return chart_ext(vm, argv, SL_CHART_X); }
static mcs_value_t lvo_lv_chart_set_ext_y_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return chart_ext(vm, argv, SL_CHART_Y); }
#endif

/* ---- grid descriptors: int[] ending with LV_GRID_TEMPLATE_LAST (added when missing) */
#if LV_USE_GRID
static mcs_value_t lvo_lv_obj_set_grid_dsc_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    if (!obj) return mcs_null();
    lvb_coord_t* col = coord_array(vm, SL_GRID_COL, obj, NULL, argv[1], 0, 1);
    lvb_coord_t* row = col ? coord_array(vm, SL_GRID_ROW, obj, NULL, argv[2], 0, 1) : NULL;
    if (!col || !row) return mcs_null();
    lv_obj_set_grid_dsc_array(obj, col, row);
    return mcs_null();
}
static mcs_value_t style_grid(mcs_vm_t* vm, mcs_value_t* argv, int col) {
    lv_style_t* st = (lv_style_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_style_t, 0);
    if (!st) return mcs_null();
    lvb_coord_t* m = coord_array(vm, col ? SL_STYLE_COL : SL_STYLE_ROW, st, NULL, argv[1], 0, 1);
    if (!m) return mcs_null();
    if (col) lv_style_set_grid_column_dsc_array(st, m); else lv_style_set_grid_row_dsc_array(st, m);
    return mcs_null();
}
static mcs_value_t lvo_lv_style_set_grid_column_dsc_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return style_grid(vm, argv, 1); }
static mcs_value_t lvo_lv_style_set_grid_row_dsc_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return style_grid(vm, argv, 0); }
static mcs_value_t ostyle_grid(mcs_vm_t* vm, mcs_value_t* argv, int col) {
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    lv_style_selector_t sel = (lv_style_selector_t)mcs_to_int(vm, argv[2]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    lvb_coord_t* m = coord_array(vm, col ? SL_OSTYLE_COL : SL_OSTYLE_ROW, obj, (const void*)(uintptr_t)sel, argv[1], 0, 1);
    if (!m) return mcs_null();
    if (col) lv_obj_set_style_grid_column_dsc_array(obj, m, sel); else lv_obj_set_style_grid_row_dsc_array(obj, m, sel);
    return mcs_null();
}
static mcs_value_t lvo_lv_obj_set_style_grid_column_dsc_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return ostyle_grid(vm, argv, 1); }
static mcs_value_t lvo_lv_obj_set_style_grid_row_dsc_array(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return ostyle_grid(vm, argv, 0); }
#endif

/* ---- calendar: lv_calendar_set_highlighted_dates(cal, lv_calendar_date_t[], count) */
#if LV_USE_CALENDAR
static mcs_value_t lvo_lv_calendar_set_highlighted_dates(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    mcs_int_t n = mcs_to_int(vm, argv[2]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    if (!is_seq(argv[1])) { mcs_raise(vm, "ArgumentException", "lv_calendar_date_t[] expected"); return mcs_null(); }
    if (n < 0 || (uint32_t)n > mcs_len(argv[1])) { mcs_raise(vm, "ArgumentOutOfRangeException", "date count"); return mcs_null(); }
    lv_calendar_date_t* d = (lv_calendar_date_t*)slot_block(vm, SL_CAL, obj, NULL, (size_t)(n ? n : 1) * sizeof *d, 8);
    if (!d) return mcs_null();
    for (mcs_int_t i = 0; i < n; i++) lvb_arg_struct(vm, mcs_index(argv[1], (uint32_t)i), LVB_C_lv_calendar_date_t, &d[i], sizeof d[i]);
    if (mcs_has_exception(vm)) return mcs_null();
    lv_calendar_set_highlighted_dates(obj, d, (uint32_t)n);
    return mcs_null();
}
#endif

/* ---- canvas: lv_canvas_set_buffer(canvas, byte[] or null, w, h, color format): the pixel storage lives in C
 * (its size comes from the format), initialised from the byte[] when given */
#if LV_USE_CANVAS
static mcs_value_t lvo_lv_canvas_set_buffer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    mcs_int_t w = mcs_to_int(vm, argv[2]), h = mcs_to_int(vm, argv[3]), cf = mcs_to_int(vm, argv[4]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    if (w <= 0 || h <= 0) { mcs_raise(vm, "ArgumentOutOfRangeException", "canvas size"); return mcs_null(); }
#if LVGL_VERSION_MAJOR >= 9
    size_t sz = (size_t)lv_draw_buf_width_to_stride((uint32_t)w, (lv_color_format_t)cf) * (size_t)h;
#else
    size_t sz = (size_t)lv_img_buf_get_img_size((lv_coord_t)w, (lv_coord_t)h, (lv_img_cf_t)cf);
#endif
    sz += 1024u;   /* palette of the indexed formats */
    uint8_t* b = (uint8_t*)slot_block(vm, SL_CANVAS, obj, NULL, sz, 64);
    if (!b) return mcs_null();
    if (!mcs_is_null(argv[1])) {
        if (!is_seq(argv[1])) { mcs_raise(vm, "ArgumentException", "canvas buffer: byte[] or null expected"); return mcs_null(); }
        uint32_t n = mcs_len(argv[1]);
        for (uint32_t i = 0; i < n && i < sz; i++) b[i] = (uint8_t)mcs_to_int(vm, mcs_index(argv[1], i));
    }
    if (mcs_has_exception(vm)) return mcs_null();
    lv_canvas_set_buffer(obj, b, (lvb_coord_t)w, (lvb_coord_t)h, (
#if LVGL_VERSION_MAJOR >= 9
        lv_color_format_t
#else
        lv_img_cf_t
#endif
        )cf);
    return mcs_null();
}
#endif

/* ---- style transitions: lv_style_transition_dsc_init(tr, int[] props, lv_anim_path_*, time, delay, user_data) */
static mcs_value_t lvo_lv_style_transition_dsc_init(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_style_transition_dsc_t* tr = (lv_style_transition_dsc_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_style_transition_dsc_t, 0);
    uint32_t time = (uint32_t)mcs_to_int(vm, argv[3]), delay = (uint32_t)mcs_to_int(vm, argv[4]);
    if (!tr || mcs_has_exception(vm)) return mcs_null();
    if (!is_seq(argv[1])) { mcs_raise(vm, "ArgumentException", "style properties: int[] expected (LV_STYLE_...)"); return mcs_null(); }
    lv_anim_path_cb_t path = NULL;
    if (!mcs_is_null(argv[2])) {
        mcs_native_fn f = mcs_native_fn_of(argv[2]);
        for (int i = 0; lvb_anim_paths[i].w; i++) if (lvb_anim_paths[i].w == f) path = lvb_anim_paths[i].c;
        if (!path) { mcs_raise(vm, "ArgumentException", "lv_style_transition_dsc_init: pass one of LVGL's lv_anim_path_* functions or null"); return mcs_null(); }
    }
    uint32_t n = mcs_len(argv[1]);
    lv_style_prop_t* props = (lv_style_prop_t*)slot_block(vm, SL_TRANS, tr, NULL, (size_t)(n + 1u) * sizeof(lv_style_prop_t), 8);
    if (!props) return mcs_null();
    for (uint32_t i = 0; i < n; i++) props[i] = (lv_style_prop_t)mcs_to_int(vm, mcs_index(argv[1], i));   /* the last entry stays 0 = end */
    if (mcs_has_exception(vm)) return mcs_null();
    lv_style_transition_dsc_init(tr, props, path, time, delay, NULL);
    return mcs_null();
}

/* ---- lv_line_set_points_mutable (9.x): same as lv_line_set_points, the points are copied either way */
/* ---- lv_obj_tree_walk(start, cb(obj, user_data) -> LV_OBJ_TREE_WALK_*, user_data) */
typedef struct { lvb_state_t* st; mcs_value_t fn, ud; } lvb_walk_t;
static lv_obj_tree_walk_res_t walk_tramp(lv_obj_t* obj, void* p) {
    lvb_walk_t* w = (lvb_walk_t*)p;
    mcs_vm_t* vm = w->st->vm;
    if (mcs_has_exception(vm)) return LV_OBJ_TREE_WALK_END;
    mcs_value_t args[2] = { lvb_ret_ptr(vm, obj, LVB_C_lv_obj_t), w->ud }, res;
    mcs_push_root(vm, args[0]);
    w->st->event_depth++;
    mcs_result_t r = mcs_call_value(vm, w->fn, 2, args, &res);
    w->st->event_depth--;
    mcs_pop_root(vm, 1);
    if (r != MCS_OK) { if (!mcs_running(vm)) mcs_fail(vm, r, "%s", mcs_last_error(vm)); return LV_OBJ_TREE_WALK_END; }
    if (mcs_is_null(res)) return LV_OBJ_TREE_WALK_NEXT;
    return (lv_obj_tree_walk_res_t)mcs_to_int(vm, res);
}
static mcs_value_t lvo_lv_obj_tree_walk(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* start = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    check_callable(vm, argv[1], 0);
    if (!start || mcs_has_exception(vm)) return mcs_null();
    lvb_walk_t w = { ST(vm), argv[1], argv[2] };
    mcs_push_root(vm, argv[1]); mcs_push_root(vm, argv[2]);
    MCS_LVGL_LOCK();
    lv_obj_tree_walk(start, walk_tramp, &w);
    MCS_LVGL_UNLOCK();
    mcs_pop_root(vm, 2);
    return mcs_null();
}

/* ---- lv_async_call_cancel(cb, user_data) */
static mcs_value_t lvo_lv_async_call_cancel(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
#if MCS_LV_VER >= 80300
    lvb_state_t* st = ST(vm);
    int n = 0;
    for (lvb_cb_t* c = st->cbs, *nx; c; c = nx) {
        nx = c->next;
        if (c->kind != CB_ASYNC || !val_same(kept(st, c->fn), argv[0]) || !val_same(kept(st, c->ud), argv[1])) continue;
        MCS_LVGL_LOCK();
        lv_async_call_cancel(async_tramp, c);
        MCS_LVGL_UNLOCK();
        cb_zombify(st, c);
        n++;
    }
    REAP(st);
    return mcs_int(n > 0 ? 1 : 0);   /* LV_RESULT_OK : LV_RESULT_INVALID */
#else
    (void)argv;
    mcs_raise(vm, "NotSupportedException", "lv_async_call_cancel needs LVGL 8.3 or newer");
    return mcs_null();
#endif
}

/* ---- remove an event handler by index / descriptor (lv_event_dsc_t*s come from lv_obj_get_event_dsc) */
#if LVGL_VERSION_MAJOR >= 9
static mcs_value_t lvo_lv_obj_remove_event(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    uint32_t idx = (uint32_t)mcs_to_int(vm, argv[1]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    lv_event_dsc_t* d = lv_obj_get_event_dsc(obj, idx);
    lvb_cb_t* c = (d && lv_event_dsc_get_cb(d) == event_tramp) ? cb_find(st, lv_event_dsc_get_user_data(d), CB_EVENT) : NULL;
    MCS_LVGL_LOCK();
    bool r = lv_obj_remove_event(obj, idx);
    MCS_LVGL_UNLOCK();
    if (r && c) { cb_zombify(st, c); REAP(st); }
    return mcs_bool(r);
}
#endif
#if MCS_LV_VER >= 90100
static mcs_value_t lvo_lv_obj_remove_event_dsc(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    lv_event_dsc_t* d = (lv_event_dsc_t*)lvb_arg_ptr(vm, argv[1], LVB_C_lv_event_dsc_t, 0);
    if (!obj || !d || mcs_has_exception(vm)) return mcs_null();
    lvb_cb_t* c = lv_event_dsc_get_cb(d) == event_tramp ? cb_find(st, lv_event_dsc_get_user_data(d), CB_EVENT) : NULL;
    MCS_LVGL_LOCK();
    bool r = lv_obj_remove_event_dsc(obj, d);
    MCS_LVGL_UNLOCK();
    if (r && c) { cb_zombify(st, c); REAP(st); }
    return mcs_bool(r);
}
#elif LVGL_VERSION_MAJOR < 9
static mcs_value_t lvo_lv_obj_remove_event_dsc(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; (void)argv;
    mcs_raise(vm, "NotSupportedException", "lv_obj_remove_event_dsc: LVGL 8 descriptors are not stable - use lv_obj_remove_event_cb");
    return mcs_null();
}
#endif

/* ---- animations: get_value_cb(anim) -> int, deleted_cb(anim), custom_exec_cb(anim, value) */
static int32_t anim_getval_tramp(lv_anim_t* a) {
    lvb_cb_t* c = (lvb_cb_t*)a->user_data;
    if (!c || c->magic != LVB_MAGIC || c->getval < 0) return 0;
    mcs_vm_t* vm = c->st->vm;
    mcs_value_t w = new_wrapper(vm, a, LVB_C_lv_anim_t), res;
    mcs_push_root(vm, w);
    int ok = cb_call_ret(c->st, c->getval, 1, &w, &res);
    tmp_dispose(w);
    mcs_pop_root(vm, 1);
    return ok && !mcs_is_null(res) ? (int32_t)mcs_to_int(vm, res) : 0;
}
#if MCS_LV_VER >= 80300
static void anim_deleted_tramp(lv_anim_t* a) {
    lvb_cb_t* c = (lvb_cb_t*)a->user_data;
    if (!c || c->magic != LVB_MAGIC || c->deleted < 0) return;
    mcs_value_t w = new_wrapper(c->st->vm, a, LVB_C_lv_anim_t);
    mcs_push_root(c->st->vm, w);
    cb_call(c->st, c->deleted, 1, &w);
    tmp_dispose(w);
    mcs_pop_root(c->st->vm, 1);
}
#endif
static mcs_value_t lvo_lv_anim_set_custom_exec_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; check_callable(vm, argv[1], 1); if (mcs_has_exception(vm)) return mcs_null(); ANIM_ARG(0);
    set_kept(st, &c->fn, argv[1]);
    c->custom = 1;
    lv_anim_set_custom_exec_cb(a, mcs_is_null(argv[1]) ? NULL : anim_exec_tramp);
    return mcs_null();
}
#if MCS_LV_VER >= 80300
static mcs_value_t lvo_lv_anim_set_deleted_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; check_callable(vm, argv[1], 1); if (mcs_has_exception(vm)) return mcs_null(); ANIM_ARG(0);
    set_kept(st, &c->deleted, argv[1]);
    lv_anim_set_deleted_cb(a, mcs_is_null(argv[1]) ? NULL : anim_deleted_tramp);
    return mcs_null();
}
#endif
static mcs_value_t lvo_lv_anim_set_get_value_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; check_callable(vm, argv[1], 1); if (mcs_has_exception(vm)) return mcs_null(); ANIM_ARG(0);
    set_kept(st, &c->getval, argv[1]);
    lv_anim_set_get_value_cb(a, mcs_is_null(argv[1]) ? NULL : anim_getval_tramp);
    return mcs_null();
}

/* ---- lv_dropdown_get_selected_str / lv_roller_get_selected_str(obj, byte[] buf, size): fills buf (or only returns) the string */
#if LV_USE_DROPDOWN || LV_USE_ROLLER
static mcs_value_t selected_str(mcs_vm_t* vm, mcs_value_t* argv, int roller) {
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    mcs_int_t size = mcs_to_int(vm, argv[2]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    int has_buf = !mcs_is_null(argv[1]);
    if (has_buf && !is_seq(argv[1])) { mcs_raise(vm, "ArgumentException", "buf: byte[] or null expected"); return mcs_null(); }
    uint32_t cap = size > 0 ? (uint32_t)size : 256u;
    if (has_buf && cap > mcs_len(argv[1])) cap = mcs_len(argv[1]);
    if (cap == 0) return mcs_string(vm, "");
    char* tmp = (char*)LVB_MALLOC(cap);
    if (!tmp) { mcs_raise(vm, "OutOfMemoryException", "selected_str"); return mcs_null(); }
    tmp[0] = 0;
    MCS_LVGL_LOCK();
#if LV_USE_DROPDOWN
    if (!roller) lv_dropdown_get_selected_str(obj, tmp, cap);
#endif
#if LV_USE_ROLLER
    if (roller) lv_roller_get_selected_str(obj, tmp, cap);
#endif
    MCS_LVGL_UNLOCK();
    (void)roller;
    tmp[cap - 1u] = 0;
    uint32_t n = (uint32_t)strlen(tmp);
    if (has_buf) { for (uint32_t i = 0; i < n; i++) mcs_set_index(argv[1], i, mcs_int((unsigned char)tmp[i])); if (n < mcs_len(argv[1])) mcs_set_index(argv[1], n, mcs_int(0)); }
    mcs_value_t r = mcs_string(vm, tmp);
    LVB_FREE(tmp);
    return r;
}
#endif
#if LV_USE_DROPDOWN
static mcs_value_t lvo_lv_dropdown_get_selected_str(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return selected_str(vm, argv, 0); }
#endif
#if LV_USE_ROLLER
static mcs_value_t lvo_lv_roller_get_selected_str(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)self; (void)argc; return selected_str(vm, argv, 1); }
#endif

/* ---- observers / subjects (LVGL 9.x, LV_USE_OBSERVER): cb(observer, subject) */
#if LV_USE_OBSERVER
static void* observer_user_data(lv_observer_t* o) {
#if MCS_LV_VER >= 90200
    return lv_observer_get_user_data(o);
#else
    return o->user_data;   /* LVGL 9.0/9.1 exposes the struct, not the getter */
#endif
}
static void observer_tramp(lv_observer_t* o, lv_subject_t* s) {
    lvb_cb_t* c = (lvb_cb_t*)observer_user_data(o);
    if (!c || c->magic != LVB_MAGIC || c->kind != CB_OBSERVER) return;
    mcs_vm_t* vm = c->st->vm;
    if (c->st->closing || mcs_has_exception(vm)) return;
    mcs_value_t args[2] = { new_wrapper(vm, o, LVB_C_lv_observer_t), new_wrapper(vm, s, LVB_C_lv_subject_t) };
    mcs_push_root(vm, args[0]); mcs_push_root(vm, args[1]);
    cb_call(c->st, c->fn, 2, args);
    tmp_dispose(args[0]); tmp_dispose(args[1]);
    mcs_pop_root(vm, 2);
}
static mcs_value_t lvo_lv_subject_add_observer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    check_callable(vm, argv[1], 0);
    if (!subj || mcs_has_exception(vm)) return mcs_null();
    lvb_cb_t* c = cb_new(st, CB_OBSERVER, NULL);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[1]); c->ud = keep(st, argv[2]);
    MCS_LVGL_LOCK();
    lv_observer_t* o = lv_subject_add_observer(subj, observer_tramp, c);   /* calls back once right away */
    MCS_LVGL_UNLOCK();
    return lvb_ret_ptr(vm, o, LVB_C_lv_observer_t);
}
/* lv_subject_add_observer_obj(subject, cb, obj, user_data): removed by LVGL when obj is deleted */
static mcs_value_t lvo_lv_subject_add_observer_obj(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    check_callable(vm, argv[1], 0);
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[2], LVB_C_lv_obj_t, 0);
    if (!subj || !obj || mcs_has_exception(vm)) return mcs_null();
    lvb_ret_ptr(vm, obj, LVB_C_lv_obj_t);
    lvb_cb_t* c = cb_new(st, CB_OBSERVER, obj);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[1]); c->ud = keep(st, argv[3]);
    MCS_LVGL_LOCK();
    lv_observer_t* o = lv_subject_add_observer_obj(subj, observer_tramp, obj, c);
    MCS_LVGL_UNLOCK();
    return lvb_ret_ptr(vm, o, LVB_C_lv_observer_t);
}
static mcs_value_t lvo_lv_observer_get_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_observer_t* o = (lv_observer_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_observer_t, 0);
    lvb_cb_t* c = o ? cb_find(st, observer_user_data(o), CB_OBSERVER) : NULL;
    return c ? kept(st, c->ud) : mcs_null();
}
/* string subjects: the buffers are allocated here (size bytes each), the C buffers given are ignored */
static mcs_value_t lvo_lv_subject_init_string(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    mcs_int_t size = mcs_to_int(vm, argv[3]);
    const char* val = mcs_is_null(argv[4]) ? "" : lvb_arg_str(vm, argv[4]);
    if (!subj || mcs_has_exception(vm)) return mcs_null();
    if (size <= 0) { mcs_raise(vm, "ArgumentOutOfRangeException", "string subject size"); return mcs_null(); }
    char* b = (char*)slot_block(vm, SL_SUBJ_STR, subj, NULL, (size_t)size, 1);
    char* pb = (b && !mcs_is_null(argv[2])) ? (char*)slot_block(vm, SL_SUBJ_PREV, subj, NULL, (size_t)size, 1) : NULL;
    if (!b || mcs_has_exception(vm)) return mcs_null();
    lv_subject_init_string(subj, b, pb, (size_t)size, val);
    return mcs_null();
}
/* pointer subjects: a string is copied into storage owned by the subject, a wrapper gives its pointer */
static void* subj_ptr_arg(mcs_vm_t* vm, const void* subj, mcs_value_t v) {
    if (mcs_is_null(v)) return NULL;
    if (mcs_is_string(v)) {
        const char* s = mcs_cstr(v);
        size_t l = strlen(s);
        char* b = (char*)slot_block(vm, SL_SUBJ_PTR, subj, NULL, l + 1u, 1);
        if (b) memcpy(b, s, l + 1u);
        return b;
    }
    lvb_ud_t* u = ud_of(v);
    if (u && u->p) return u->p;
    mcs_raise(vm, "InvalidCastException", "pointer subject: null, string or an LVGL object expected");
    return NULL;
}
static mcs_value_t subj_ptr_ret(mcs_vm_t* vm, const void* subj, const void* p) {
    lvb_state_t* st = ST(vm);
    if (!p) return mcs_null();
    lvb_slot_t* s = slot_find(st, SL_SUBJ_PTR, subj, NULL);
    if (s && s->mem == p) return mcs_string(vm, (const char*)p);
    if (obj_find(st, p)) return lvb_ret_ptr(vm, (void*)p, LVB_C_lv_obj_t);
    return mcs_null();
}
static mcs_value_t lvo_lv_subject_init_pointer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    if (!subj || mcs_has_exception(vm)) return mcs_null();
    void* p = subj_ptr_arg(vm, subj, argv[1]);
    if (mcs_has_exception(vm)) return mcs_null();
    lv_subject_init_pointer(subj, p);
    return mcs_null();
}
static mcs_value_t lvo_lv_subject_set_pointer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    if (!subj || mcs_has_exception(vm)) return mcs_null();
    void* p = subj_ptr_arg(vm, subj, argv[1]);
    if (mcs_has_exception(vm)) return mcs_null();
    MCS_LVGL_LOCK();
    lv_subject_set_pointer(subj, p);
    MCS_LVGL_UNLOCK();
    return mcs_null();
}
static mcs_value_t lvo_lv_subject_get_pointer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    return subj ? subj_ptr_ret(vm, subj, lv_subject_get_pointer(subj)) : mcs_null();
}
static mcs_value_t lvo_lv_subject_get_previous_pointer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_subject_t* subj = (lv_subject_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_subject_t, 0);
    return subj ? subj_ptr_ret(vm, subj, lv_subject_get_previous_pointer(subj)) : mcs_null();
}
#endif

/* ---- LVGL 8 messages (LV_USE_MSG): the subscription is an int token for lv_msg_unsubscribe */
#if MCS_LV_VER >= 80300 && LVGL_VERSION_MAJOR < 9 && defined(LV_USE_MSG) && LV_USE_MSG
static void msg_tramp(void* s, lv_msg_t* m) {
    lvb_cb_t* c = (lvb_cb_t*)lv_msg_get_user_data(m);
    if (!c || c->magic != LVB_MAGIC || c->kind != CB_MSG) return;
    mcs_vm_t* vm = c->st->vm;
    if (c->st->closing || mcs_has_exception(vm)) return;
    (void)s;
    mcs_value_t args[2] = { mcs_int(c->fn), new_wrapper(vm, m, LVB_C_lv_msg_t) };
    mcs_push_root(vm, args[1]);
    cb_call(c->st, c->fn, 2, args);
    tmp_dispose(args[1]);
    mcs_pop_root(vm, 1);
}
static mcs_value_t lvo_lv_msg_subscribe(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    uint32_t id = (uint32_t)mcs_to_int(vm, argv[0]);
    check_callable(vm, argv[1], 0);
    if (mcs_has_exception(vm)) return mcs_null();
    lvb_cb_t* c = cb_new(st, CB_MSG, NULL);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[1]); c->ud = keep(st, argv[2]);
    MCS_LVGL_LOCK();
    c->handle = lv_msg_subsribe(id, msg_tramp, c);
    MCS_LVGL_UNLOCK();
    return mcs_int(c->fn);
}
static mcs_value_t lvo_lv_msg_subscribe_obj(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    uint32_t id = (uint32_t)mcs_to_int(vm, argv[0]);
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[1], LVB_C_lv_obj_t, 0);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    lvb_cb_t* c = cb_new(st, CB_MSGOBJ, obj);
    if (!c) return mcs_null();
    c->ud = keep(st, argv[2]);
    MCS_LVGL_LOCK();
    c->handle = lv_msg_subsribe_obj(id, obj, c);
    MCS_LVGL_UNLOCK();
    return mcs_null();
}
static mcs_value_t lvo_lv_msg_get_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_msg_t* m = (lv_msg_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_msg_t, 0);
    void* ud = m ? lv_msg_get_user_data(m) : NULL;
    lvb_cb_t* c = cb_find(st, ud, CB_MSG);
    if (!c) c = cb_find(st, ud, CB_MSGOBJ);
    return c ? kept(st, c->ud) : mcs_null();
}
static mcs_value_t lvo_lv_msg_unsubscribe(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    mcs_int_t tok = mcs_to_int(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    for (lvb_cb_t* c = st->cbs; c; c = c->next) {
        if (c->kind != CB_MSG || c->fn != (int)tok) continue;
        MCS_LVGL_LOCK();
        lv_msg_unsubscribe(c->handle);
        MCS_LVGL_UNLOCK();
        cb_zombify(st, c); REAP(st);
        return mcs_int(1);
    }
    return mcs_int(0);
}
#endif

/* ---- lv_log_register_print_cb(cb(level, text)) (LV_USE_LOG). One callback per process. */
#if LV_USE_LOG
static lvb_cb_t* g_log_cb;
static int g_log_busy;
static void log_deliver(int level, const char* buf) {
    lvb_cb_t* c = g_log_cb;
    if (!c || g_log_busy || c->magic != LVB_MAGIC || c->st->closing) return;
    mcs_vm_t* vm = c->st->vm;
    if (mcs_has_exception(vm)) return;
    g_log_busy = 1;
    mcs_value_t args[2] = { mcs_int(level), mcs_string(vm, buf ? buf : "") };
    mcs_push_root(vm, args[1]);
    if (mcs_arity(kept(c->st, c->fn)) == 1) cb_call(c->st, c->fn, 1, &args[1]);   /* a one-argument callback gets the text */
    else cb_call(c->st, c->fn, 2, args);
    mcs_pop_root(vm, 1);
    g_log_busy = 0;
}
#if LVGL_VERSION_MAJOR >= 9
static void log_tramp(lv_log_level_t level, const char* buf) { log_deliver((int)level, buf); }
#else
static void log_tramp(const char* buf) {
    static const char* const tags[] = { "[Trace]", "[Info]", "[Warn]", "[Error]", "[User]" };
    int level = 0;
    for (int i = 0; i < 5; i++) if (buf && strstr(buf, tags[i])) { level = i; break; }
    log_deliver(level, buf);
}
#endif
static mcs_value_t lvo_lv_log_register_print_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    check_callable(vm, argv[0], 1);
    if (mcs_has_exception(vm)) return mcs_null();
    if (g_log_cb) { cb_zombify(g_log_cb->st, g_log_cb); g_log_cb = NULL; }
    if (mcs_is_null(argv[0])) { lv_log_register_print_cb(NULL); REAP(st); return mcs_null(); }
    lvb_cb_t* c = cb_new(st, CB_LOG, NULL);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[0]);
    g_log_cb = c;
    lv_log_register_print_cb(log_tramp);
    return mcs_null();
}
#endif

/* ---- close hooks */
static void lvb_more_close_early(lvb_state_t* st) {
#if LV_USE_LOG
    if (g_log_cb && g_log_cb->st == st) { lv_log_register_print_cb(NULL); g_log_cb = NULL; }
#endif
    (void)st;
}
static void lvb_more_close_late(lvb_state_t* st) {
    while (st->slots) {
        lvb_slot_t* s = st->slots;
        st->slots = s->next;
        if (s->raw) LVB_FREE(s->raw);
        LVB_FREE(s);
    }
}

static const mcs_reg_t lvb_override2_fns[] = {   /* sorted */
    MCS_FN("lv_anim_set_custom_exec_cb", lvo_lv_anim_set_custom_exec_cb, 2),
#if MCS_LV_VER >= 80300
    MCS_FN("lv_anim_set_deleted_cb", lvo_lv_anim_set_deleted_cb, 2),
#endif
    MCS_FN("lv_anim_set_get_value_cb", lvo_lv_anim_set_get_value_cb, 2),
    MCS_FN("lv_async_call_cancel", lvo_lv_async_call_cancel, 2),
#if LV_USE_CALENDAR
    MCS_FN("lv_calendar_set_highlighted_dates", lvo_lv_calendar_set_highlighted_dates, 3),
#endif
#if LV_USE_CANVAS
    MCS_FN("lv_canvas_set_buffer", lvo_lv_canvas_set_buffer, 5),
#endif
#if LV_USE_CHART
    MCS_FN("lv_chart_set_ext_x_array", lvo_lv_chart_set_ext_x_array, 3),
    MCS_FN("lv_chart_set_ext_y_array", lvo_lv_chart_set_ext_y_array, 3),
#endif
#if LV_USE_DROPDOWN
    MCS_FN("lv_dropdown_get_selected_str", lvo_lv_dropdown_get_selected_str, 3),
#endif
#if LV_USE_LINE && LVGL_VERSION_MAJOR >= 9
    MCS_FN("lv_line_set_points_mutable", lvo_lv_line_set_points, 3),
#endif
#if LV_USE_LOG
    MCS_FN("lv_log_register_print_cb", lvo_lv_log_register_print_cb, 1),
#endif
#if MCS_LV_VER >= 80300 && LVGL_VERSION_MAJOR < 9 && defined(LV_USE_MSG) && LV_USE_MSG
    MCS_FN("lv_msg_get_user_data", lvo_lv_msg_get_user_data, 1),
    MCS_FN("lv_msg_subscribe", lvo_lv_msg_subscribe, 3),
    MCS_FN("lv_msg_subscribe_obj", lvo_lv_msg_subscribe_obj, 3),
    MCS_FN("lv_msg_subsribe", lvo_lv_msg_subscribe, 3),
    MCS_FN("lv_msg_subsribe_obj", lvo_lv_msg_subscribe_obj, 3),
    MCS_FN("lv_msg_unsubscribe", lvo_lv_msg_unsubscribe, 1),
#endif
#if LVGL_VERSION_MAJOR >= 9
    MCS_FN("lv_obj_remove_event", lvo_lv_obj_remove_event, 2),
#endif
#if LVGL_VERSION_MAJOR < 9 || MCS_LV_VER >= 90100
    MCS_FN("lv_obj_remove_event_dsc", lvo_lv_obj_remove_event_dsc, 2),
#endif
#if LV_USE_GRID
    MCS_FN("lv_obj_set_grid_dsc_array", lvo_lv_obj_set_grid_dsc_array, 3),
    MCS_FN("lv_obj_set_style_grid_column_dsc_array", lvo_lv_obj_set_style_grid_column_dsc_array, 3),
    MCS_FN("lv_obj_set_style_grid_row_dsc_array", lvo_lv_obj_set_style_grid_row_dsc_array, 3),
#endif
    MCS_FN("lv_obj_tree_walk", lvo_lv_obj_tree_walk, 3),
#if LV_USE_OBSERVER
    MCS_FN("lv_observer_get_user_data", lvo_lv_observer_get_user_data, 1),
#endif
#if LV_USE_ROLLER
    MCS_FN("lv_roller_get_selected_str", lvo_lv_roller_get_selected_str, 3),
#endif
#if LV_USE_GRID
    MCS_FN("lv_style_set_grid_column_dsc_array", lvo_lv_style_set_grid_column_dsc_array, 2),
    MCS_FN("lv_style_set_grid_row_dsc_array", lvo_lv_style_set_grid_row_dsc_array, 2),
#endif
    MCS_FN("lv_style_transition_dsc_init", lvo_lv_style_transition_dsc_init, 6),
#if LV_USE_OBSERVER
    MCS_FN("lv_subject_add_observer", lvo_lv_subject_add_observer, 3),
    MCS_FN("lv_subject_add_observer_obj", lvo_lv_subject_add_observer_obj, 4),
    MCS_FN("lv_subject_get_pointer", lvo_lv_subject_get_pointer, 1),
    MCS_FN("lv_subject_get_previous_pointer", lvo_lv_subject_get_previous_pointer, 1),
    MCS_FN("lv_subject_init_pointer", lvo_lv_subject_init_pointer, 2),
    MCS_FN("lv_subject_init_string", lvo_lv_subject_init_string, 5),
    MCS_FN("lv_subject_set_pointer", lvo_lv_subject_set_pointer, 2),
#endif
    MCS_REG_END
};
