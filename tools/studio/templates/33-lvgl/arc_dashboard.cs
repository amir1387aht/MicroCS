// Arc and progress dashboard
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var arc = lv_arc_create(scr);
lv_obj_set_size(arc, 120, 120);
lv_obj_align(arc, LV_ALIGN_LEFT_MID, 10, 0);
lv_arc_set_range(arc, 0, 100);
var bar = lv_bar_create(scr);
lv_obj_set_size(bar, 120, 16);
lv_obj_align(bar, LV_ALIGN_RIGHT_MID, -10, 10);
var label = lv_label_create(scr);
lv_obj_align(label, LV_ALIGN_RIGHT_MID, -25, -30);
int value = 0;
lv_timer_create(t => {
    value = (value + 5) % 105;
    lv_arc_set_value(arc, value);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_label_set_text(label, "Level " + value + "%");
}, 250, null);

while (true) { lv_timer_handler(); Thread.Sleep(5); }
