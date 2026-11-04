// args: --lvgl=120x80
// Replace retained grid descriptors repeatedly and release each shared style after its object.
var scr = lv_screen_active();
var grid = lv_obj_create(scr);
lv_obj_set_size(grid, 100, 60);
lv_obj_set_layout(grid, LV_LAYOUT_GRID);
for (int i = 0; i < 150; i++) {
    lv_obj_set_grid_dsc_array(grid, new int[] { 20 + i % 10, LV_GRID_FR(1) }, new int[] { 20, 20 });
    var st = new lv_style_t();
    lv_style_init(st);
    lv_style_set_bg_color(st, lv_color_hex(0x224466));
    var o = lv_obj_create(grid);
    lv_obj_add_style(o, st, 0);
    lv_obj_delete(o);
    lv_style_reset(st);
    if (i % 25 == 0) GC.Collect();
}
lv_obj_update_layout(grid);
lv_obj_delete(grid);
lvgl_host_run(20);
Console.WriteLine("arrays and styles done");
