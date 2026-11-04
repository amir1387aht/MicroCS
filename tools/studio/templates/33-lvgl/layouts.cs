// Flex and grid layouts: LV_PCT(), LV_GRID_FR() and the int[] descriptor arrays.
if (!Hal.Has("Lvgl")) { Console.WriteLine("This firmware has no LVGL (docs/LVGL.md)"); return; }

var scr = lv_screen_active();

// a flex row that wraps
var row = lv_obj_create(scr);
lv_obj_set_size(row, LV_PCT(100), 90);
lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
for (int i = 1; i <= 6; i++)
{
    var b = lv_button_create(row);
    lv_obj_set_size(b, 60, 30);
    var l = lv_label_create(b);
    lv_label_set_text(l, "B" + i);
    lv_obj_center(l);
}

// a 3x2 grid: two fixed columns and one that takes the rest
var grid = lv_obj_create(scr);
lv_obj_set_size(grid, LV_PCT(100), 110);
lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
lv_obj_set_layout(grid, LV_LAYOUT_GRID);
lv_obj_set_grid_dsc_array(grid, new int[] { 60, 60, LV_GRID_FR(1) }, new int[] { 40, 40 });
for (int r = 0; r < 2; r++)
    for (int c = 0; c < 3; c++)
    {
        var cell = lv_label_create(grid);
        lv_label_set_text(cell, "r" + r + "c" + c);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_CENTER, c, 1, LV_GRID_ALIGN_CENTER, r, 1);
    }

while (true)
{
    lv_timer_handler();
    Thread.Sleep(5);
}
