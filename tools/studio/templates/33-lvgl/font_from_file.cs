// Fonts from the file system: an LVGL binary font made by lv_font_conv (lv_binfont_create; lv_font_load on
// LVGL 8) or a TrueType font through TinyTTF (lv_tiny_ttf_create_file). Upload them to /fonts/ first.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var label = lv_label_create(scr);
lv_label_set_text(label, "Custom font text");
lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);

// 1) binary font:  npx lv_font_conv --font x.ttf --size 20 --bpp 4 --format bin -r 0x20-0x7F -o face20.bin
var bin = lv_binfont_create("/fonts/face20.bin");
if (bin != null) lv_obj_set_style_text_font(label, bin, 0);
else Console.WriteLine("no /fonts/face20.bin: using the default font");

// 2) TrueType font (needs LV_USE_TINY_TTF with file support in lv_conf.h)
var ttf = lv_tiny_ttf_create_file("/fonts/face.ttf", 28);
var big = lv_label_create(scr);
lv_label_set_text(big, "TrueType 28");
lv_obj_align(big, LV_ALIGN_CENTER, 0, 30);
if (ttf != null) lv_obj_set_style_text_font(big, ttf, 0);
else Console.WriteLine("no /fonts/face.ttf: using the default font");

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
