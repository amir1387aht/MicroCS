// One script for LVGL 8 and 9: the names of both versions work (lv_scr_act / lv_screen_active, lv_btn_create /
// lv_button_create, lv_disp_* / lv_display_*, LV_STATE_* ...). Version checks for the differences in behaviour.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

Console.WriteLine("LVGL " + LVGL_VERSION_MAJOR + "." + LVGL_VERSION_MINOR + "." + LVGL_VERSION_PATCH);
var scr = lv_scr_act();                                // LVGL 8 name
Console.WriteLine(scr == lv_screen_active());          // True: the same object on LVGL 8 and 9
var btn = lv_btn_create(scr);                          // LVGL 8 name
var btn2 = lv_button_create(scr);                      // LVGL 9 name
lv_obj_align(btn, LV_ALIGN_LEFT_MID, 20, 0);
lv_obj_align(btn2, LV_ALIGN_RIGHT_MID, -20, 0);

if (LVGL_VERSION_MAJOR >= 9)
{
    Console.WriteLine("9.x: lv_display_get_horizontal_resolution = " + lv_display_get_horizontal_resolution(lv_display_get_default()));
}
else
{
    Console.WriteLine("8.x: lv_disp_get_hor_res = " + lv_disp_get_hor_res(lv_disp_get_default()));
}

// deleted objects are safe: using one afterwards raises an exception instead of crashing
lv_obj_delete(btn);                                    // lv_obj_del on LVGL 8
try { lv_obj_set_width(btn, 10); }
catch (Exception ex) { Console.WriteLine("deleted object: " + ex.Message); }

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
