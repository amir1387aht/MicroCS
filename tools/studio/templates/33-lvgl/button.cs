// Host firmware initializes LVGL and attaches MicroCS. No LCD setup here.
if (!Hal.Has("Lvgl")) {
    Console.WriteLine("Host LVGL attachment is unavailable.");
} else if (!Lvgl.Ready || !Lvgl.Supports("Button") || !Lvgl.Supports("Label")) {
    Console.WriteLine("Host GUI or required widgets are unavailable.");
} else {
    var button = new LvObject("Button");
    button.SetSize(110, 40);
    button.Center();
    var text = new LvObject("Label", button);
    text.Text = "Click me";
    text.Center();
    button.OnClicked(() => { text.Text = "Clicked"; });
    // Add Lvgl.Dispatch(16) to your existing application loop.
    // A persistent native GUI alone cannot execute C# delegates.
}
