// args: --lvgl=100x60
// Callbacks beyond events: tree walk, animation custom exec / deleted / get_value callbacks, async cancel, log callback.
var scr = lv_screen_active();
var a = lv_obj_create(scr);
var b = lv_obj_create(a);
var c = lv_label_create(a);
var n = 0;
lv_obj_tree_walk(scr, (o, ud) => { n++; return LV_OBJ_TREE_WALK_NEXT; }, null);
Console.WriteLine("walked " + n);

int last = -1;
int done = 0;
var an = new lv_anim_t();
lv_anim_init(an);
lv_anim_set_var(an, a);
lv_anim_set_values(an, 0, 100);
lv_anim_set_duration(an, 100);
lv_anim_set_custom_exec_cb(an, (anim, v) => { last = v; });
lv_anim_set_deleted_cb(an, anim => { done++; });
lv_anim_start(an);
lvgl_host_run(300);
Console.WriteLine("anim last " + last + " deleted " + done);

int fired = 0;
lv_async_call(ud => { fired++; }, null);
lvgl_host_run(50);
Console.WriteLine("async fired " + fired);

var seen = false;
lv_log_register_print_cb((level, text) => { if (text.Contains("Q:/nope.png")) seen = true; });
var img = lv_image_create(scr);
lv_image_set_src(img, "Q:/nope.png");
lvgl_host_run(50);
Console.WriteLine("log seen " + seen);
lv_log_register_print_cb(null);
