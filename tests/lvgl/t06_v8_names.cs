// args: --lvgl=100x60
// The names of LVGL 8 work on LVGL 9 and the names of LVGL 9 work on LVGL 8.
var scr = lv_scr_act();
Console.WriteLine(scr == lv_screen_active());
var b = lv_btn_create(scr);
var i = lv_img_create(scr);
var l = lv_label_create(scr);
lv_obj_set_size(b, 30, 20);
lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
lv_obj_add_state(b, LV_STATE_PRESSED);
lv_obj_clear_state(b, LV_STATE_PRESSED);
lv_obj_update_layout(b);
Console.WriteLine("w=" + lv_obj_get_width(b));
var disp = lv_disp_get_default();
Console.WriteLine("hor " + lv_disp_get_hor_res(disp) + " ver " + lv_disp_get_ver_res(disp));
lv_obj_del(i);
lv_obj_del_async(l);
lvgl_host_run(30);
Console.WriteLine("children " + lv_obj_get_child_count(scr));
Console.WriteLine("symbol " + (LV_SYMBOL_OK.Length > 0));
Console.WriteLine(LV_ALIGN_CENTER + " " + LV_DIR_ALL + " " + LV_OPA_50 + " " + LV_PART_MAIN);
