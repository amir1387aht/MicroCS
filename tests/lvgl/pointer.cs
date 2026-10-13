var gui = new LvGui(120, 80);
var root = gui.Screen;
var button = root.Create("button");
button.SetPos(0, 0);
button.SetSize(50, 30);
gui.Pump(40);
gui.Input(10, 10, true);
gui.Pump(40);
gui.Input(10, 10, false);
gui.Pump(40);
bool clicked = false;
while (true) {
    var ev = gui.PollEvent();
    if (ev == null) break;
    if (ev[0].Kind == "button" && ev[1] == LvGui.Clicked) clicked = true;
}
Console.WriteLine(clicked);
gui.Dispose();
