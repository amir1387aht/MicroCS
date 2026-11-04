// args: --lvgl=64x40 --fs build/lvglfs
// Remove all uses before explicitly releasing file fonts.
var bin = lv_binfont_create("/fonts/face16.bin");
var ttf = lv_tiny_ttf_create_file("/fonts/face.ttf", 24);
var label = lv_label_create(lv_screen_active());
lv_obj_set_style_text_font(label, ttf, 0);
lv_label_set_text(label, "test");
lvgl_host_run(50);
lv_obj_delete(label);
lv_binfont_destroy(bin);
lv_tiny_ttf_destroy(ttf);
Console.WriteLine("fonts released");
try { lv_binfont_destroy(bin); Console.WriteLine("bin double free accepted"); }
catch (ObjectDisposedException ex) { Console.WriteLine("bin disposed"); }
try { lv_font_get_line_height(ttf); Console.WriteLine("ttf reuse accepted"); }
catch (ObjectDisposedException ex) { Console.WriteLine("ttf disposed"); }
