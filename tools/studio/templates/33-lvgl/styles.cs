// Styles: lv_style_t is created with new and filled with lv_style_set_*; the script keeps it alive for LVGL.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();

var card = new lv_style_t();
lv_style_init(card);
lv_style_set_bg_color(card, lv_color_hex(0x2b6cb0));
lv_style_set_bg_opa(card, LV_OPA_COVER);
lv_style_set_radius(card, 16);
lv_style_set_border_width(card, 3);
lv_style_set_border_color(card, lv_color_white());
lv_style_set_shadow_width(card, 20);
lv_style_set_shadow_color(card, lv_color_hex(0x000000));
lv_style_set_pad_all(card, 12);

var pressed = new lv_style_t();
lv_style_init(pressed);
lv_style_set_bg_color(pressed, lv_color_hex(0xe53e3e));

var obj = lv_obj_create(scr);
lv_obj_set_size(obj, 180, 100);
lv_obj_center(obj);
lv_obj_add_style(obj, card, 0);                       // default state
lv_obj_add_style(obj, pressed, LV_STATE_PRESSED);     // while pressed

var text = lv_label_create(obj);
lv_label_set_text(text, "Styled object");
lv_obj_set_style_text_color(text, lv_color_white(), 0);   // local style, no lv_style_t needed
lv_obj_center(text);

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
