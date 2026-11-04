// args: --lvgl=100x60
// Timers, animations, lv_async_call. Time only advances through lvgl_host_run(ms).
var count = 0;
var t = lv_timer_create(tm => { count++; }, 100, null);
lvgl_host_run(1000);
Console.WriteLine("timer fired " + (count >= 9 && count <= 11));
lv_timer_pause(t);
var c0 = count;
lvgl_host_run(500);
Console.WriteLine("paused " + (count == c0));
lv_timer_resume(t);
lv_timer_set_period(t, 50);
lv_timer_set_repeat_count(t, 3);
c0 = count;
lvgl_host_run(1000);
Console.WriteLine("repeat 3 -> " + (count - c0));

// user data of a timer
var t2 = lv_timer_create(tm => { Console.WriteLine("data " + lv_timer_get_user_data(tm)); }, 10, "payload");
lv_timer_set_repeat_count(t2, 1);
lvgl_host_run(100);

// animation with a C# exec callback
var scr = lv_screen_active();
var o = lv_obj_create(scr);
lv_obj_set_size(o, 10, 10);
var a = new lv_anim_t();
lv_anim_init(a);
lv_anim_set_var(a, o);
lv_anim_set_values(a, 0, 50);
lv_anim_set_duration(a, 200);
lv_anim_set_exec_cb(a, (obj, v) => { lv_obj_set_x(obj, v); });
lv_anim_set_path_cb(a, lv_anim_path_linear);
var done = false;
lv_anim_set_ready_cb(a, an => { done = true; Console.WriteLine("anim ready"); });
lv_anim_start(a);
lvgl_host_run(100);
var mid = lv_obj_get_x(o);
Console.WriteLine("midway " + (mid > 10 && mid < 40));
lvgl_host_run(300);
Console.WriteLine("done " + done + " x=" + lv_obj_get_x(o));

// the animation of an object that is deleted stops quietly
var o2 = lv_obj_create(scr);
var a2 = new lv_anim_t();
lv_anim_init(a2);
lv_anim_set_var(a2, o2);
lv_anim_set_values(a2, 0, 100);
lv_anim_set_duration(a2, 500);
lv_anim_set_exec_cb(a2, (obj, v) => { lv_obj_set_y(obj, v); });
lv_anim_start(a2);
lvgl_host_run(50);
lv_obj_delete(o2);
lvgl_host_run(600);
Console.WriteLine("deleted object animation ended without errors");

// lv_async_call runs on the next timer handler run
var ran = false;
lv_async_call(p => { ran = true; }, null);
Console.WriteLine("before " + ran);
lvgl_host_run(20);
Console.WriteLine("after " + ran);

// deleting a timer (the finished timer `t` deleted itself after its 3 repeats)
try { lv_timer_delete(t); Console.WriteLine("no exception"); }
catch (ObjectDisposedException e) { Console.WriteLine("finished timer: ObjectDisposedException"); }
var t3 = lv_timer_create(tm => { count++; }, 10, null);
lvgl_host_run(100);
lv_timer_delete(t3);
c0 = count;
lvgl_host_run(300);
Console.WriteLine("deleted " + (count == c0));
