// Animations: lv_anim_t with a setter (lv_obj_set_x), a C# lambda, an easing path and callbacks.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var ball = lv_obj_create(scr);
lv_obj_set_size(ball, 40, 40);
lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
lv_obj_set_pos(ball, 10, 60);

var move = new lv_anim_t();
lv_anim_init(move);
lv_anim_set_var(move, ball);
lv_anim_set_exec_cb(move, lv_obj_set_x);                // a native LVGL setter works directly
lv_anim_set_values(move, 10, 250);
lv_anim_set_duration(move, 1200);
lv_anim_set_path_cb(move, lv_anim_path_ease_in_out);
lv_anim_set_repeat_count(move, LV_ANIM_REPEAT_INFINITE);
lv_anim_set_playback_time(move, 1200);   // (legacy name in newer LVGL 9: see docs/LVGL.md, renames)
lv_anim_start(move);

var fade = new lv_anim_t();
lv_anim_init(fade);
lv_anim_set_var(fade, ball);
lv_anim_set_values(fade, 80, 255);
lv_anim_set_duration(fade, 600);
lv_anim_set_repeat_count(fade, LV_ANIM_REPEAT_INFINITE);
lv_anim_set_playback_time(fade, 600);   // (legacy name in newer LVGL 9: see docs/LVGL.md, renames)
lv_anim_set_custom_exec_cb(fade, (a, v) => lv_obj_set_style_bg_opa(ball, v, 0));   // any C# lambda
lv_anim_start(fade);

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
