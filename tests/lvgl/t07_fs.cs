// args: --lvgl=64x40 --fs build/lvglfs
// LVGL reads images and fonts through the MicroCS file system (here: the host folder build/lvglfs).
var scr = lv_screen_active();
lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
lv_obj_set_style_pad_all(scr, 0, 0);

// BMP at (0,0): left half red, right half blue
var bmp = lv_image_create(scr);
lv_obj_set_pos(bmp, 0, 0);
lv_image_set_src(bmp, "/img/two.bmp");
// PNG at (20,0): top green, bottom yellow
var png = lv_image_create(scr);
lv_obj_set_pos(png, 20, 0);
lv_image_set_src(png, "/img/grad.png");
lv_obj_update_layout(scr);
lvgl_host_run(50);
Console.WriteLine("bmp size " + lv_obj_get_width(bmp) + "x" + lv_obj_get_height(bmp));
Console.WriteLine("png size " + lv_obj_get_width(png) + "x" + lv_obj_get_height(png));
Console.WriteLine("bmp " + lvgl_host_pixel(2, 2).ToString("X6") + " " + lvgl_host_pixel(12, 2).ToString("X6"));
Console.WriteLine("png " + lvgl_host_pixel(22, 2).ToString("X6") + " " + lvgl_host_pixel(22, 12).ToString("X6"));

// a file that does not exist: LVGL shows nothing and MicroCS keeps running
var missing = lv_image_create(scr);
lv_obj_set_pos(missing, 40, 0);
lv_image_set_src(missing, "/img/none.png");
lvgl_host_run(50);
Console.WriteLine("missing " + lvgl_host_pixel(42, 2).ToString("X6"));

// LVGL binary font and TrueType font from the file system
var bf = lv_binfont_create("/fonts/face16.bin");
Console.WriteLine("binfont " + (bf != null) + " height>=14 " + (lv_font_get_line_height(bf) >= 14));
var tf = lv_tiny_ttf_create_file("/fonts/face.ttf", 24);
Console.WriteLine("ttf " + (tf != null) + " height>=24 " + (lv_font_get_line_height(tf) >= 24));
var l = lv_label_create(scr);
lv_obj_set_pos(l, 0, 20);
lv_obj_set_style_text_font(l, tf, 0);
lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
lv_label_set_text(l, "Hi");
lvgl_host_run(50);
int white = 0;
for (int y = 20; y < 40; y++) for (int x = 0; x < 30; x++) if (lvgl_host_pixel(x, y) != 0) white++;
Console.WriteLine("ttf text drawn " + (white > 20));
Console.WriteLine("done");
