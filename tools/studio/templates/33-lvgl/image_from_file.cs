// Images from the MicroCS file system (LittleFS / YAFFS): LVGL reads "/images/logo.png" through the
// drive "M:" (MCS_LVGL_FS_LETTER). Any format LVGL has a decoder for works: PNG, JPG, BMP, GIF, SVG, .bin.
// Upload an image to /images/ with Studio's file manager first (or change the path).
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var img = lv_image_create(scr);                        // lv_img_create on LVGL 8
lv_image_set_src(img, "/images/logo.png");             // same as "M:/images/logo.png"
lv_obj_center(img);

var hint = lv_label_create(scr);
lv_label_set_text(hint, "/images/logo.png");
lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
Console.WriteLine("image size " + lv_obj_get_width(img) + "x" + lv_obj_get_height(img) + " (0x0 until the file exists)");

// built-in symbols are strings too
var ok = lv_label_create(scr);
lv_label_set_text(ok, LV_SYMBOL_OK + " " + LV_SYMBOL_WIFI + " " + LV_SYMBOL_BATTERY_FULL);
lv_obj_align(ok, LV_ALIGN_TOP_MID, 0, 8);

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
