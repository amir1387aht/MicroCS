// Version-matrix smoke test: supported spellings work; pre-8.3 APIs are absent.
var s = lv_obj_create(null);
lv_screen_load(s);
lv_scr_load(s);
Console.WriteLine(lv_obj_get_width(s));
if (LVGL_VERSION_MAJOR == 8 && LVGL_VERSION_MINOR < 3) {
    try {
        lv_anim_set_deleted_cb(new lv_anim_t(), null);
        throw new Exception("deleted callback should not exist before LVGL 8.3");
    } catch (MissingMemberException e) { Console.WriteLine("deleted callback unavailable"); }
    try {
        lv_msg_subsribe(1, null, null);
        throw new Exception("message subscriptions should not exist before LVGL 8.3");
    } catch (MissingMemberException e) { Console.WriteLine("messages unavailable"); }
} else {
    var anim = new lv_anim_t();
    lv_anim_init(anim);
    lv_anim_set_deleted_cb(anim, null);
    Console.WriteLine("deleted callback available");
}
if (LVGL_VERSION_MAJOR == 9) {
    var subject = new lv_subject_t();
    lv_subject_init_int(subject, 1);
    int calls = 0;
    var observer = lv_subject_add_observer(subject, (o, sub) => {
        if (lv_observer_get_user_data(o) != "tag") throw new Exception("observer user data lost");
        calls++;
    }, "tag");
    lv_subject_set_int(subject, 2);
    if (calls != 2) throw new Exception("observer callback failed");
    lv_observer_remove(observer);
    if (LVGL_VERSION_MINOR >= 2) lv_subject_deinit(subject);
    if (LVGL_VERSION_MINOR == 0) {
        try {
            lv_obj_remove_event_dsc(s, null);
            throw new Exception("descriptor removal should not exist in LVGL 9.0");
        } catch (MissingMemberException e) { Console.WriteLine("descriptor removal unavailable"); }
    }
    Console.WriteLine("observer works");
}
