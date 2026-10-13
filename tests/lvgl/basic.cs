// Real upstream headless rendering, version-independent public wrapper.
var gui = new LvGui(160, 96);
Console.WriteLine(gui.Backend);
var screen = gui.Screen;
var box = screen.Create("object");
box.SetSize(120, 70);
box.SetPos(5, 5);
box.SetStyle("bg_color", 0x103060);
box.SetStyle("pad_left", 4);
var label = box.Create("label");
label.Text = "Hello MicroCS";
Console.WriteLine(label.Text);
var slider = box.Create("slider");
slider.SetRange(-10, 90);
slider.Value = 42;
Console.WriteLine(slider.Value);
slider.SendEvent(LvGui.Clicked);
var ev = gui.PollEvent();
Console.WriteLine(ev[0].Kind);
Console.WriteLine(ev[1] == LvGui.Clicked);
Console.WriteLine(gui.PollEvent() == null);
gui.Pump(40);
Console.WriteLine(gui.FlushCount > 0);
box.Clean();
try { Console.WriteLine(label.Text); }
catch (ObjectDisposedException e) { Console.WriteLine("stale child rejected"); }
var fresh = box.Create("label");
fresh.Text = "new generation";
try { label.Text = "must not alter fresh"; }
catch (ObjectDisposedException e) { Console.WriteLine("slot reuse rejected"); }
Console.WriteLine(fresh.Text);
try { fresh.Value = 1; }
catch (NotSupportedException e) { Console.WriteLine("wrong widget rejected"); }
try { screen.Delete(); }
catch (InvalidOperationException e) { Console.WriteLine("active screen protected"); }
gui.Dispose();
gui.Dispose();
try { Console.WriteLine(fresh.Text); }
catch (ObjectDisposedException e) { Console.WriteLine("disposed GUI rejected"); }
var nextGui = new LvGui(33, 17);
nextGui.Screen.SetStyle("bg_color", 0xFF0000);
nextGui.Pump(40);
Console.WriteLine(nextGui.FlushCount > 0);
nextGui.Dispose();
