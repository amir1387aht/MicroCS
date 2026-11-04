// Calendar with highlighted dates
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

// Set the date explicitly; use an RTC or network clock on a real device.
var scr = lv_screen_active();
var cal = lv_calendar_create(scr);
lv_obj_set_size(cal, 220, 200);
lv_obj_center(cal);
lv_calendar_set_today_date(cal, 2024, 5, 3);
lv_calendar_set_showed_date(cal, 2024, 5);
var day = new lv_calendar_date_t();
day.year = 2024; day.month = 5; day.day = 3;
lv_calendar_set_highlighted_dates(cal, new lv_calendar_date_t[] { day }, 1);

while (true) { lv_timer_handler(); Thread.Sleep(5); }
