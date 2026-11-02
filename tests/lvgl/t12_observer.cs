// args: --lvgl=100x60
// only: v9
// Subjects and observers (LVGL 9.1+): C# observer callbacks, integer/string subjects, observer removal.
var s = new lv_subject_t();
lv_subject_init_int(s, 5);
int calls = 0;
var obs = lv_subject_add_observer(s, (o, subj) => {
    calls++;
    Console.WriteLine("observer sees " + lv_subject_get_int(subj) + " ud=" + lv_observer_get_user_data(o));
}, "tag");
lv_subject_set_int(s, 7);
lv_subject_set_int(s, 9);
Console.WriteLine("calls " + calls);
lv_observer_remove(obs);
lv_subject_set_int(s, 11);
Console.WriteLine("calls after remove " + calls);

var ss = new lv_subject_t();
lv_subject_init_string(ss, null, null, 16, "abc");
lv_subject_copy_string(ss, "hello");
Console.WriteLine("string subject: " + lv_subject_get_string(ss));

var scr = lv_screen_active();
var lab = lv_label_create(scr);
lv_label_bind_text(lab, s, "val %d");
lv_subject_set_int(s, 42);
lv_obj_update_layout(scr);
Console.WriteLine("label: " + lv_label_get_text(lab));
lv_subject_deinit(ss);
lv_subject_deinit(s);
