// A button with an event handler: lv_obj_add_event_cb takes a C# lambda, user data is any C# value.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var btn = lv_button_create(scr);
lv_obj_set_size(btn, 140, 50);
lv_obj_center(btn);
var label = lv_label_create(btn);
lv_label_set_text(label, "Click me");
lv_obj_center(label);

int clicks = 0;
lv_obj_add_event_cb(btn, e =>
{
    clicks++;
    lv_label_set_text(label, "Clicked " + clicks + "x");
    Console.WriteLine("click " + clicks);
}, LV_EVENT_CLICKED, null);

// the same handler type for other events; the user data comes back with lv_event_get_user_data
lv_obj_add_event_cb(btn, e => Console.WriteLine("pressed, data = " + lv_event_get_user_data(e)), LV_EVENT_PRESSED, "button 1");

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
