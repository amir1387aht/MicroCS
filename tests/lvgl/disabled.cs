var gui = new LvGui(33, 17);
try { gui.Screen.Create("slider"); }
catch (NotSupportedException e) { Console.WriteLine("disabled slider rejected"); }
gui.Screen.Create("label").Text = "enabled label";
gui.Pump(40);
Console.WriteLine(gui.FlushCount > 0);
gui.Dispose();
