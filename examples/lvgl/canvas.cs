// Canvas drawing
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

// The binding allocates the canvas buffer when null is passed.
var scr = lv_screen_active();
var canvas = lv_canvas_create(scr);
int format = LVGL_VERSION_MAJOR >= 9 ? LV_COLOR_FORMAT_RGB565 : LV_IMG_CF_TRUE_COLOR;
lv_canvas_set_buffer(canvas, null, 160, 80, format);
lv_canvas_fill_bg(canvas, lv_color_hex(0x2378a8), LV_OPA_COVER);
lv_obj_center(canvas);

while (true) { lv_timer_handler(); Thread.Sleep(5); }
