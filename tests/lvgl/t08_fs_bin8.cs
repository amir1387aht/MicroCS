// args: --lvgl=32x20 --fs build/lvglfs
// only: v8
// LVGL 8 binary image (lv_img_conv / LVGLImage.py format) read from the MicroCS file system.
var scr = lv_scr_act();
lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
var im = lv_img_create(scr);
lv_obj_set_pos(im, 0, 0);
lv_img_set_src(im, "/img/two8.bin");
lvgl_host_run(50);
Console.WriteLine("size " + lv_obj_get_width(im) + "x" + lv_obj_get_height(im));
Console.WriteLine(lvgl_host_pixel(2, 2).ToString("X6") + " " + lvgl_host_pixel(12, 2).ToString("X6"));
