// Hello LVGL: the C API with the C names. The firmware did lv_init, the display and the touch driver (docs/LVGL.md).
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();                      // lv_scr_act() on LVGL 8 works too
lv_obj_set_style_bg_color(scr, lv_color_hex(0x003a57), 0);

var label = lv_label_create(scr);
lv_label_set_text(label, "Hello from C#!");
lv_obj_set_style_text_color(label, lv_color_white(), 0);
lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

Console.WriteLine("LVGL " + LVGL_VERSION_MAJOR + "." + LVGL_VERSION_MINOR + "." + LVGL_VERSION_PATCH);
while (true)
{
    lv_timer_handler();                            // not needed when MCS_LVGL_AUTO_HANDLER=1, harmless then
    Thread.Sleep(5);
}
