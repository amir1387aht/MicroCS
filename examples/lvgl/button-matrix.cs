// Host LVGL must already be initialized and attached; no GUI loop is started.
if (!Hal.Has("Lvgl") || !Lvgl.Ready || !Lvgl.Supports("ButtonMatrix")) {
    Console.WriteLine("Host ButtonMatrix support is unavailable.");
} else {
    var panel = new LvObject("Object");
    panel.SetSize(220, 150);
    panel.Center();
    var choices = new LvObject("ButtonMatrix", panel);
    choices.SetSize(200, 110);
    string[] labels = { "Red", "Green", "\n", "Blue" };
    choices.SetButtonMap(labels); // No empty terminator: the wrapper appends it.
    labels = null;
    GC.Collect(); // Native labels and pointer table remain valid.
    int toggle = Lvgl.Constant("LV_BTNMATRIX_CTRL_CHECKABLE");
    choices.SetButtonControls(new int[] { toggle | 1, toggle | 1, 2 });
    choices.Call("lv_btnmatrix_set_selected_btn", 1);
    string selected = choices.Call("lv_btnmatrix_get_btn_text", 1);
    Console.WriteLine(choices.ButtonCount + " buttons; selected: " + selected);
    // choices.Call("lv_btnmatrix_set_selected_btn", -1) clears selection.
    // Replacing the map also clears selection; disposal/VM teardown frees it.
}
