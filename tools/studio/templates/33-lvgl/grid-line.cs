// Native LVGL retains point/track arrays: these methods COPY them safely.
if (!Hal.Has("Lvgl")) {
    Console.WriteLine("Host LVGL attachment is unavailable.");
} else if (!Lvgl.Ready || !Lvgl.Supports("Line") || !Lvgl.HasConstant("LV_GRID_CONTENT")) {
    Console.WriteLine("Host GUI, Grid or Line is unavailable.");
} else {
    var panel = new LvObject("Object");
    panel.SetSize(180, 100);
    panel.Center();
    panel.SetGrid(new int[] { 60, Lvgl.GridFraction(1) }, new int[] { 70 });
    var line = new LvObject("Line", panel);
    int[] points = { 0, 40, 20, 10, 40, 25, 55, 0 };
    line.SetPoints(points);
    line.Call("lv_obj_set_grid_cell", Lvgl.Constant("LV_GRID_ALIGN_CENTER"), 0, 1,
        Lvgl.Constant("LV_GRID_ALIGN_CENTER"), 0, 1);
    line.Call("lv_obj_set_style_line_color", 0x339966, 0);
    line.Call("lv_obj_set_style_line_width", 3, 0);
    // Later VM mutations/GC cannot alter the native copy.
    points[0] = 100;
    points = null;
    GC.Collect();
    Console.WriteLine("Copied points: " + line.PointCount.ToString());
    // Host firmware owns ticks/rendering; Dispose or VM teardown frees copies.
}
