// Explicit allowlisted native APIs, not raw pointers or arbitrary C FFI.
if (!Hal.Has("Lvgl")) {
    Console.WriteLine("Host LVGL attachment is unavailable.");
} else if (!Lvgl.Ready || !Lvgl.Supports("Slider") || !Lvgl.Supports("Label")) {
    Console.WriteLine("Host GUI or required widgets are unavailable.");
} else {
    var panel = new LvObject("Object");
    panel.SetSize(180, 100);
    panel.Center();
    panel.Call("lv_obj_set_style_radius", 10, 0);
    panel.Call("lv_obj_set_style_border_width", 2, 0);
    panel.Call("lv_obj_set_style_border_color", 0x336699, 0);
    var text = new LvObject("Label", panel);
    text.Text = "Native range: 125";
    text.SetPosition(8, 8);
    var slider = new LvObject("Slider", panel);
    slider.SetSize(140, 16);
    slider.SetPosition(8, 45);
    slider.Call("lv_slider_set_range", -50, 150);
    // Resolve once for loops; IDs are specific to this firmware build.
    int setValue = Lvgl.Api("lv_slider_set_value");
    slider.Call(setValue, 125, Lvgl.Constant("LV_ANIM_OFF"));
    slider.OnEvent(Lvgl.Constant("LV_EVENT_VALUE_CHANGED"), () => {
        text.Text = "Native range: " + slider.Call("lv_slider_get_value").ToString();
    });
    // Add Lvgl.Dispatch(16) to your existing VM application loop.
    // Native firmware still owns rendering, ticks and GUI synchronization.
}
