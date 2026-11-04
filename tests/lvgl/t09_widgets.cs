// args: --lvgl=120x100
// Widgets driven by the simulated pointer: button, slider, checkbox, switch, plus a smoke test of many widgets.
var scr = lv_screen_active();
lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
lv_obj_set_style_pad_all(scr, 0, 0);

int clicks = 0;
var btn = lv_button_create(scr);
lv_obj_set_pos(btn, 0, 0);
lv_obj_set_size(btn, 40, 20);
lv_obj_add_event_cb(btn, (e) => { clicks++; }, LV_EVENT_CLICKED, null);

var sl = lv_slider_create(scr);
lv_obj_set_pos(sl, 0, 30);
lv_obj_set_size(sl, 100, 10);
lv_slider_set_range(sl, 0, 100);
int changed = 0;
lv_obj_add_event_cb(sl, (e) => { changed++; }, LV_EVENT_VALUE_CHANGED, null);

var sw = lv_switch_create(scr);
lv_obj_set_pos(sw, 0, 50);
var cb = lv_checkbox_create(scr);
lv_obj_set_pos(cb, 50, 50);
lv_checkbox_set_text(cb, "x");
lv_obj_update_layout(scr);
lvgl_host_run(50);

lvgl_host_touch(10, 10, true);
lvgl_host_run(50);
lvgl_host_touch(10, 10, false);
lvgl_host_run(50);
Console.WriteLine("clicks " + clicks);

lvgl_host_touch(50, 35, true);
lvgl_host_run(50);
lvgl_host_touch(50, 35, false);
lvgl_host_run(50);
int v = lv_slider_get_value(sl);
Console.WriteLine("slider near 50: " + (v >= 45 && v <= 55) + " changed " + (changed > 0));

lvgl_host_touch(10, 55, true);
lvgl_host_run(50);
lvgl_host_touch(10, 55, false);
lvgl_host_run(50);
Console.WriteLine("switch checked " + lv_obj_has_state(sw, LV_STATE_CHECKED));
lvgl_host_touch(55, 55, true);
lvgl_host_run(50);
lvgl_host_touch(55, 55, false);
lvgl_host_run(50);
Console.WriteLine("checkbox checked " + lv_obj_has_state(cb, LV_STATE_CHECKED));

// many widgets
var box = lv_obj_create(scr);
lv_obj_set_pos(box, 60, 0);
lv_obj_set_size(box, 60, 45);
var lab = lv_label_create(box);
lv_label_set_text(lab, "Hello");
var bar = lv_bar_create(box);
lv_bar_set_value(bar, 70, LV_ANIM_OFF);
var arc = lv_arc_create(box);
lv_arc_set_value(arc, 40);
var dd = lv_dropdown_create(box);
lv_dropdown_set_options(dd, "one\ntwo\nthree");
lv_dropdown_set_selected(dd, 2);
var ro = lv_roller_create(box);
lv_roller_set_options(ro, "a\nb\nc", LV_ROLLER_MODE_NORMAL);
var ta = lv_textarea_create(box);
lv_textarea_set_text(ta, "abc");
var ch = lv_chart_create(box);
var ser = lv_chart_add_series(ch, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
lv_chart_set_next_value(ch, ser, 10);
lvgl_host_run(100);
Console.WriteLine("bar " + lv_bar_get_value(bar) + " arc " + lv_arc_get_value(arc) + " dd " + lv_dropdown_get_selected(dd) + " text " + lv_textarea_get_text(ta));
Console.WriteLine("label " + lv_label_get_text(lab) + " children " + lv_obj_get_child_count(box));
lv_obj_delete(box);
lvgl_host_run(50);
Console.WriteLine("done");
