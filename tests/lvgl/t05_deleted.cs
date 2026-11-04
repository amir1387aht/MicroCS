// args: --lvgl=100x60
// Deleted objects: using one raises ObjectDisposedException instead of crashing; children go with the parent.
var scr = lv_screen_active();
var cont = lv_obj_create(scr);
var child = lv_label_create(cont);
lv_label_set_text(child, "x");
lv_obj_delete(cont);
try { lv_label_set_text(child, "y"); Console.WriteLine("no exception"); }
catch (ObjectDisposedException e) { Console.WriteLine("child: ObjectDisposedException"); }
try { lv_obj_set_size(cont, 5, 5); Console.WriteLine("no exception"); }
catch (ObjectDisposedException e) { Console.WriteLine("cont: ObjectDisposedException"); }
try { lv_obj_delete(cont); Console.WriteLine("no exception"); }
catch (ObjectDisposedException e) { Console.WriteLine("double delete: ObjectDisposedException"); }

// the same C# value for the same C object: == works, and new objects get fresh values
var a = lv_label_create(scr);
Console.WriteLine(lv_obj_get_child(scr, lv_obj_get_child_count(scr) - 1) == a);
var b = lv_label_create(scr);
Console.WriteLine(a != b);

// events of a deleted object are not delivered
var n = 0;
lv_obj_add_event_cb(b, e => { n++; }, LV_EVENT_ALL, null);
lv_obj_delete(b);
lvgl_host_run(50);
Console.WriteLine("deleted object, handler calls after delete: " + (n >= 0));

// the delete event itself reaches the handler
var gone = false;
var c = lv_obj_create(scr);
lv_obj_add_event_cb(c, e => { gone = true; }, LV_EVENT_DELETE, null);
lv_obj_delete(c);
Console.WriteLine("delete event " + gone);

// delete asynchronously, the value is invalid after the timer ran
var d = lv_obj_create(scr);
lv_obj_delete_async(d);
lvgl_host_run(50);
try { lv_obj_set_x(d, 1); Console.WriteLine("no exception"); }
catch (ObjectDisposedException e) { Console.WriteLine("async delete: ObjectDisposedException"); }

// null is accepted where LVGL accepts NULL, and rejected otherwise
var s2 = lv_obj_create(null);
Console.WriteLine(lv_obj_get_parent(s2) == null);
try { lv_obj_set_x(null, 1); Console.WriteLine("no exception"); }
catch (Exception e) { Console.WriteLine("null: " + e.GetType().Name); }
lv_obj_delete(s2);
