// args: --lvgl=160x100
// Events: lv_obj_add_event_cb with C# functions and user data, simulated touch, event getters.
var scr = lv_screen_active();
var btn = lv_button_create(scr);
lv_obj_set_size(btn, 80, 40);
lv_obj_center(btn);
int clicks = 0;

lv_obj_add_event_cb(btn, e => {
    clicks++;
    var code = lv_event_get_code(e);
    Console.WriteLine("click #" + clicks + " code=" + (code == LV_EVENT_CLICKED ? "CLICKED" : "" + code) + " target ok=" + (lv_event_get_target(e) == btn));
}, LV_EVENT_CLICKED, null);

lv_obj_add_event_cb(btn, e => {
    Console.WriteLine("user data: " + lv_event_get_user_data(e));
}, LV_EVENT_PRESSED, "hello");

lvgl_host_run(50);
lvgl_host_touch(80, 50, true);
lvgl_host_touch(80, 50, false);
lvgl_host_run(50);
Console.WriteLine("clicks " + clicks);

// lv_obj_send_event / lv_event_send (v8 name) call the same callbacks
lv_obj_send_event(btn, LV_EVENT_CLICKED, null);
Console.WriteLine("clicks " + clicks);

// a handler with several event codes sees LV_EVENT_ALL
var seen = 0;
lv_obj_add_event_cb(btn, e => { seen++; }, LV_EVENT_ALL, null);
lvgl_host_touch(80, 50, true);
lvgl_host_touch(80, 50, false);
lvgl_host_run(50);
Console.WriteLine("seen more than 3: " + (seen > 3));

// removing handlers
lv_obj_remove_event_cb(btn, null);
var n0 = clicks;
lv_obj_send_event(btn, LV_EVENT_CLICKED, null);
Console.WriteLine("after remove " + (clicks - n0));

// an exception in a callback reaches the caller
var b2 = lv_button_create(scr);
lv_obj_add_event_cb(b2, e => { throw new Exception("boom"); }, LV_EVENT_CLICKED, null);
try { lv_obj_send_event(b2, LV_EVENT_CLICKED, null); Console.WriteLine("no exception"); }
catch (Exception ex) { Console.WriteLine("caught " + ex.Message); }
