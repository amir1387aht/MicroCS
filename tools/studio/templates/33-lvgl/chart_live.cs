// A live line chart: one series, a timer adds a point every 100 ms.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();
var chart = lv_chart_create(scr);
lv_obj_set_size(chart, 280, 160);
lv_obj_center(chart);
lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
lv_chart_set_point_count(chart, 40);
lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
var series = lv_chart_add_series(chart, lv_color_hex(0xe53e3e), LV_CHART_AXIS_PRIMARY_Y);

int t = 0;
lv_timer_create(tm =>
{
    t++;
    int value = 50 + (int)(40 * Math.Sin(t / 6.0));
    lv_chart_set_next_value(chart, series, value);
}, 100, null);

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
