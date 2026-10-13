// Every advertised constructor creates the actual matching native class.
var gui = new LvGui(100, 60);
var root = gui.Screen;
string[] names = new[] { "object", "label", "button", "image", "arc", "bar", "slider", "switch", "checkbox", "dropdown", "roller", "textarea", "table", "chart", "canvas", "line", "list", "menu", "keyboard", "spinbox", "led", "calendar", "tileview", "spangroup", "animimg", "tabview", "win", "spinner" };
foreach (var name in names) {
    var widget = root.Create(name);
    if (widget.Kind != name) throw new Exception("wrong native class: " + name + " -> " + widget.Kind);
    widget.SetSize(50, 30);
    gui.Pump(40);
    Console.WriteLine(widget.Kind);
    root.Clean();
    try { widget.SetSize(1, 1); throw new Exception("stale constructor handle"); }
    catch (ObjectDisposedException e) {}
    GC.Collect();
}
gui.Dispose();
