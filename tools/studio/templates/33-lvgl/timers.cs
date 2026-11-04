// lv_timer_create runs a C# lambda inside LVGL's own loop: a clock label that updates twice a second.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var label = lv_label_create(scr);
lv_obj_center(label);

int seconds = 0;
var timer = lv_timer_create(t =>
{
    seconds++;
    lv_label_set_text(label, "Uptime " + (seconds / 2) + "." + (seconds % 2 * 5) + " s");
}, 500, null);

// lv_async_call: run something once, at the next lv_timer_handler()
lv_async_call(ud => Console.WriteLine("async: " + ud), "hello from lv_async_call");

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
    if (seconds >= 10)
    {
        lv_timer_delete(timer);                          // lv_timer_del on LVGL 8 works too
        break;
    }
}
Console.WriteLine("timer deleted");
