// args: --lvgl=120x80
// Churn through objects, callbacks, retained text and timers with collections between batches.
var scr = lv_screen_active();
int calls = 0;
for (int i = 0; i < 1000; i++) {
    var parent = lv_obj_create(scr);
    var child = lv_label_create(parent);
    lv_label_set_text_static(child, "temporary " + i);
    lv_label_set_text_static(child, "replacement " + i);
    lv_obj_add_event_cb(child, e => { calls++; }, LV_EVENT_CLICKED, null);
    lv_obj_send_event(child, LV_EVENT_CLICKED, null);
    lv_obj_delete(parent);
    try { lv_label_set_text(child, "bad"); }
    catch (ObjectDisposedException ex) { }
    var timer = lv_timer_create(t => { calls++; }, 10, null);
    lv_timer_delete(timer);
    if (i % 30 == 0) GC.Collect();
}
// An async callback still runs after deletion, but must see a disposed object.
var gone = lv_label_create(scr);
int disposed = 0;
lv_async_call(data => { try { lv_label_set_text(gone, "late"); } catch (ObjectDisposedException ex) { disposed++; } }, null);
lv_obj_delete(gone);
GC.Collect();
lvgl_host_run(50);
Console.WriteLine("callbacks " + calls);
Console.WriteLine("async saw disposed " + disposed);
Console.WriteLine("done");
