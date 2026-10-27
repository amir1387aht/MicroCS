// Host registers an LVGL drive S: mapped to its /assets VFS directory.
// Supply binary fonts/images converted for the host's LVGL version/config.
if (!Hal.Has("Lvgl")) {
    Console.WriteLine("Host LVGL attachment is unavailable.");
} else if (!Lvgl.Ready || !Lvgl.Supports("Label") || !Lvgl.Supports("Image")) {
    Console.WriteLine("Host GUI, Label or Image is unavailable.");
} else {
    var panel = new LvObject("Object");
    panel.SetSize(200, 180);
    panel.Center();
    try {
        panel.SetFont("S:/fonts/ui.fnt");
        var label = new LvObject("Label", panel);
        label.Text = "Filesystem assets";
        label.SetPosition(8, 8);
        var image = new LvObject("Image", panel);
        image.Source = "S:/images/logo.bin";
        image.SetPosition(8, 40);
    } catch (Exception e) {
        panel.Dispose();
        Console.WriteLine("Provide host LVGL drive, font and image assets: " + e.Message);
    }
}
