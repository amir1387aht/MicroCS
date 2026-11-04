// Several screens: lv_obj_create(null) makes a screen, lv_screen_load_anim switches with an animation.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

object MakeScreen(string title, int color, string buttonText)
{
    var s = lv_obj_create(null);
    lv_obj_set_style_bg_color(s, lv_color_hex(color), 0);
    var l = lv_label_create(s);
    lv_label_set_text(l, title);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 20);
    var b = lv_button_create(s);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 20);
    var bl = lv_label_create(b);
    lv_label_set_text(bl, buttonText);
    return s;
}

var first = MakeScreen("Screen one", 0x234e70, "Next");
var second = MakeScreen("Screen two", 0x6b2b2b, "Back");
lv_screen_load(first);

lv_obj_add_event_cb(lv_obj_get_child(first, 1), e => lv_screen_load_anim(second, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false), LV_EVENT_CLICKED, null);
lv_obj_add_event_cb(lv_obj_get_child(second, 1), e => lv_screen_load_anim(first, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false), LV_EVENT_CLICKED, null);

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
