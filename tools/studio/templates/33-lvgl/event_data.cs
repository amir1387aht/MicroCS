// Event data and tree walk
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var btn = lv_button_create(scr);
lv_obj_set_size(btn, 160, 50);
lv_obj_center(btn);
var label = lv_label_create(btn);
lv_label_set_text(label, "Inspect tree");
lv_obj_center(label);
lv_obj_add_event_cb(btn, e => {
    int count = 0;
    lv_obj_tree_walk(scr, (o, data) => { count++; return LV_OBJ_TREE_WALK_NEXT; }, null);
    Console.WriteLine(lv_event_get_user_data(e) + ": " + count + " objects");
}, LV_EVENT_CLICKED, "Screen");

while (true) { lv_timer_handler(); Thread.Sleep(5); }
