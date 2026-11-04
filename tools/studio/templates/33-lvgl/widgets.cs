// A small control panel: slider, bar, arc, switch, checkbox, dropdown, roller - each with an event handler.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

var info = lv_label_create(scr);
lv_label_set_text(info, "Move the slider");

var bar = lv_bar_create(scr);
lv_obj_set_size(bar, 200, 12);

var slider = lv_slider_create(scr);
lv_obj_set_width(slider, 200);
lv_obj_add_event_cb(slider, e =>
{
    int v = lv_slider_get_value(slider);
    lv_bar_set_value(bar, v, LV_ANIM_ON);
    lv_label_set_text(info, "Value " + v);
}, LV_EVENT_VALUE_CHANGED, null);

var sw = lv_switch_create(scr);
lv_obj_add_event_cb(sw, e => Console.WriteLine("switch " + (lv_obj_has_state(sw, LV_STATE_CHECKED) ? "on" : "off")), LV_EVENT_VALUE_CHANGED, null);

var cb = lv_checkbox_create(scr);
lv_checkbox_set_text(cb, "Enable logging");

var dd = lv_dropdown_create(scr);
lv_dropdown_set_options(dd, "Apple\nBanana\nCherry");
lv_obj_add_event_cb(dd, e =>
{
    var buf = new byte[32];
    lv_dropdown_get_selected_str(dd, buf, 32);           // text goes into a byte[]
    Console.WriteLine("selected index " + lv_dropdown_get_selected(dd));
}, LV_EVENT_VALUE_CHANGED, null);

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
