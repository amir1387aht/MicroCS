// args: --lvgl=200x100
// Objects: create / tree / geometry / flags / state / text, with the C names of LVGL 8 and 9.
var scr = lv_obj_create(null);
lv_screen_load(scr);
Console.WriteLine(lv_screen_active() == scr);

var btn = lv_button_create(scr);
lv_obj_set_size(btn, 80, 30);
lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 5);
var lbl = lv_label_create(btn);
lv_label_set_text(lbl, "Press");
lv_obj_center(lbl);
lvgl_host_run(50);
Console.WriteLine("size " + lv_obj_get_width(btn) + "x" + lv_obj_get_height(btn));
Console.WriteLine("pos " + lv_obj_get_x(btn) + "," + lv_obj_get_y(btn));
Console.WriteLine("text " + lv_label_get_text(lbl));
Console.WriteLine("children " + lv_obj_get_child_count(scr) + " " + lv_obj_get_child_count(btn));
Console.WriteLine(lv_obj_get_parent(lbl) == btn);
Console.WriteLine(lv_obj_get_child(scr, 0) == btn);
Console.WriteLine(lv_obj_get_child(scr, 0) != lbl);

// flags and states
Console.WriteLine(lv_obj_has_flag(btn, LV_OBJ_FLAG_CLICKABLE));
lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
Console.WriteLine(lv_obj_has_flag(btn, LV_OBJ_FLAG_CLICKABLE));
lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
Console.WriteLine(lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN));
lv_obj_add_state(btn, LV_STATE_DISABLED);
Console.WriteLine(lv_obj_has_state(btn, LV_STATE_DISABLED));
lv_obj_remove_state(btn, LV_STATE_DISABLED);
Console.WriteLine(lv_obj_has_state(btn, LV_STATE_DISABLED));

// widgets with numbers
var cont = lv_obj_create(scr);
var sl = lv_slider_create(cont);
lv_slider_set_range(sl, 10, 90);
lv_slider_set_value(sl, 42, LV_ANIM_OFF);
Console.WriteLine("slider " + lv_slider_get_value(sl) + " " + lv_slider_get_min_value(sl) + ".." + lv_slider_get_max_value(sl));
var bar = lv_bar_create(cont);
lv_bar_set_value(bar, 70, LV_ANIM_OFF);
Console.WriteLine("bar " + lv_bar_get_value(bar));
var sw = lv_switch_create(cont);
lv_obj_add_state(sw, LV_STATE_CHECKED);
Console.WriteLine("switch " + lv_obj_has_state(sw, LV_STATE_CHECKED));
var dd = lv_dropdown_create(cont);
lv_dropdown_set_options(dd, "Apple\nBanana\nCherry");
lv_dropdown_set_selected(dd, 2);
Console.WriteLine("dropdown " + lv_dropdown_get_selected(dd) + " of " + lv_dropdown_get_option_count(dd));
var ta = lv_textarea_create(cont);
lv_textarea_set_text(ta, "abc");
lv_textarea_add_text(ta, "def");
Console.WriteLine("textarea " + lv_textarea_get_text(ta));

Console.WriteLine("children " + lv_obj_get_child_count(scr) + " " + lv_obj_get_child_count(cont));
// delete a subtree
lv_obj_delete(btn);
Console.WriteLine("children " + lv_obj_get_child_count(scr));
lv_obj_clean(cont);
Console.WriteLine("children " + lv_obj_get_child_count(cont));
