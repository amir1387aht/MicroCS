// Text input and keyboard
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

// Touch the text area to enter text with the on-screen keyboard.
var scr = lv_screen_active();
var text = lv_textarea_create(scr);
lv_obj_set_width(text, 280);
lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 5);
lv_textarea_set_one_line(text, true);
lv_textarea_set_placeholder_text(text, "Type a message");
var keyboard = lv_keyboard_create(scr);
lv_keyboard_set_textarea(keyboard, text);
lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);

while (true) { lv_timer_handler(); Thread.Sleep(5); }
