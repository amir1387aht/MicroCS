// Host LVGL must already be initialized and attached. No GUI loop is started.
if (!Hal.Has("Lvgl")) {
    Console.WriteLine("Host LVGL attachment is unavailable.");
} else if (!Lvgl.Ready || !Lvgl.HasApi("lv_textarea_set_accepted_chars") ||
        !Lvgl.HasApi("lv_dropdown_get_selected_str")) {
    Console.WriteLine("Host TextArea or Dropdown support is unavailable.");
} else {
    var panel = new LvObject("Object");
    panel.SetSize(220, 180);
    panel.Center();
    var input = new LvObject("TextArea", panel);
    input.SetSize(180, 45);
    input.Call("lv_textarea_set_one_line", true);
    // The wrapper owns this filter even after the VM string is collected.
    string digits = "01234" + "56789";
    input.Call("lv_textarea_set_accepted_chars", digits);
    digits = null;
    GC.Collect();
    input.Text = "a12b3"; // Only 123 is accepted.
    var choices = new LvObject("Dropdown", panel);
    choices.SetPosition(0, 70);
    choices.Call("lv_dropdown_set_options", "Red\nGreen\nBlue");
    choices.Call("lv_dropdown_set_text", "Choose " + "a color");
    choices.Call("lv_dropdown_set_symbol", "▼");
    choices.Call("lv_dropdown_set_selected", 1);
    string selected = choices.Call("lv_dropdown_get_selected_str");
    Console.WriteLine("Digits: " + input.Text + ", selection: " + selected);
    // null resets the title/filter/symbol; empty filters also reset on both majors.
    input.Call("lv_textarea_set_accepted_chars", null);
    // panel.Dispose() or VM teardown releases all copied text and widgets.
}
