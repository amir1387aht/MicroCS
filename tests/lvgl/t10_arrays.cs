// args: --lvgl=100x60
// Array / struct-pointer arguments: chart external arrays, grid descriptors, calendar dates, canvas buffer,
// style transitions, dropdown/roller selected text.
var scr = lv_screen_active();

var chart = lv_chart_create(scr);
lv_chart_set_point_count(chart, 5);
var ser = lv_chart_add_series(chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
lv_chart_set_ext_y_array(chart, ser, new int[] {10, 20, 30, 40, 50});
lv_chart_refresh(chart);
Console.WriteLine("chart ok");
try { lv_chart_set_ext_x_array(chart, ser, new int[] {1, 2, 3, 4, 5}); Console.WriteLine("x accepted"); }
catch (Exception ex) { Console.WriteLine("x rejected"); }

var g = lv_obj_create(scr);
lv_obj_set_size(g, 100, 40);
lv_obj_set_layout(g, LV_LAYOUT_GRID);
lv_obj_set_grid_dsc_array(g, new int[] {30, LV_GRID_FR(1)}, new int[] {20, 20});
var l = lv_label_create(g);
lv_obj_set_grid_cell(l, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
lv_obj_update_layout(g);
Console.WriteLine("grid cell width " + lv_obj_get_width(l));

var cal = lv_calendar_create(scr);
var d = new lv_calendar_date_t(); d.year = 2024; d.month = 5; d.day = 3;
lv_calendar_set_highlighted_dates(cal, new lv_calendar_date_t[] {d}, 1);
Console.WriteLine("calendar ok");

var cv = lv_canvas_create(scr);
var cf = LVGL_VERSION_MAJOR >= 9 ? LV_COLOR_FORMAT_RGB565 : LV_IMG_CF_TRUE_COLOR;
lv_canvas_set_buffer(cv, null, 16, 8, cf);
lv_canvas_fill_bg(cv, lv_color_hex(0x00FF00), LV_OPA_COVER);
Console.WriteLine("canvas ok");

var dd = lv_dropdown_create(scr);
lv_dropdown_set_options(dd, "Apple\nBanana\nCherry");
lv_dropdown_set_selected(dd, 1);
var buf = new byte[16];
Console.WriteLine(lv_dropdown_get_selected_str(dd, buf, 16) + " " + buf[0]);
var ro = lv_roller_create(scr);
lv_roller_set_options(ro, "One\nTwo\nThree", LV_ROLLER_MODE_NORMAL);
lv_roller_set_selected(ro, 2, LV_ANIM_OFF);
Console.WriteLine(lv_roller_get_selected_str(ro, null, 32));

var tr = new lv_style_transition_dsc_t();
lv_style_transition_dsc_init(tr, new int[] {LV_STYLE_BG_COLOR, LV_STYLE_WIDTH}, lv_anim_path_ease_out, 100, 0, null);
var st = new lv_style_t();
lv_style_init(st);
lv_style_set_transition(st, tr);
Console.WriteLine("transition ok");

lv_style_reset(st);
