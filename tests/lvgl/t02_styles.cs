// args: --lvgl=100x60
// Styles: lv_style_t as a C# object, local styles, colours, getters, font and pixel check.
var scr = lv_screen_active();
lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

var st = new lv_style_t();
lv_style_init(st);
lv_style_set_bg_color(st, lv_color_hex(0xFF0000));
lv_style_set_bg_opa(st, LV_OPA_COVER);
lv_style_set_radius(st, 0);
lv_style_set_border_width(st, 0);
lv_style_set_text_font(st, lv_font_montserrat_14);

var o = lv_obj_create(scr);
lv_obj_remove_style_all(o);
lv_obj_set_size(o, 40, 20);
lv_obj_set_pos(o, 10, 10);
lv_obj_add_style(o, st, 0);
lvgl_host_run(100);
Console.WriteLine("inside  0x" + lvgl_host_pixel(20, 15).ToString("X6"));
Console.WriteLine("outside 0x" + lvgl_host_pixel(70, 40).ToString("X6"));

// changing the style changes every object that uses it
lv_style_set_bg_color(st, lv_color_hex(0x00FF00));
lv_obj_report_style_change(st);
lvgl_host_run(100);
Console.WriteLine("green   0x" + lvgl_host_pixel(20, 15).ToString("X6"));

// local style properties win over the shared style
lv_obj_set_style_bg_color(o, lv_color_hex(0x0000FF), 0);
lvgl_host_run(100);
Console.WriteLine("local   0x" + lvgl_host_pixel(20, 15).ToString("X6"));
Console.WriteLine("getter  0x" + lv_obj_get_style_bg_color(o, 0).ToString("X6"));
Console.WriteLine("opa " + lv_obj_get_style_bg_opa(o, 0) + " radius " + lv_obj_get_style_radius(o, 0));

// colours are plain 0xRRGGBB ints
var c = lv_color_hex(0x123456);
Console.WriteLine("0x" + c.ToString("X6"));
Console.WriteLine("mix 0x" + lv_color_mix(lv_color_hex(0xFF0000), lv_color_hex(0x0000FF), 128).ToString("X6"));

// fonts are named exactly like in C
var f = lv_obj_get_style_text_font(o, 0);
Console.WriteLine(f != null);
Console.WriteLine("line height " + lv_font_get_line_height(lv_font_montserrat_14));
lv_style_reset(st);
Console.WriteLine("done");
