// Real function dispatch/readback on multiple common widget families.
var gui = new LvGui(200, 120);
var root = gui.Screen;
var text = root.Create("label");
text.Call("set_text", "abcd");
text.Call("cut_text", 1, 2);
text.Call("ins_text", 1, "BC");
Console.WriteLine(text.Text);
var arc = root.Create("arc");
arc.Call("set_angles", 10, 180);
Console.WriteLine(arc.Call("get_angle_start"));
Console.WriteLine(arc.Call("get_angle_end"));
var slider = root.Create("slider");
slider.Call("set_range", -5, 90);
slider.Call("set_value", 42, 0);
Console.WriteLine(slider.Call("get_min_value"));
Console.WriteLine(slider.Call("get_max_value"));
Console.WriteLine(slider.Call("get_value"));
var dropdown = root.Create("dropdown");
dropdown.Call("set_options", "first\nsecond\nthird");
dropdown.Call("set_selected", 1);
Console.WriteLine(dropdown.Call("get_selected"));
var edit = root.Create("textarea");
edit.Call("set_text", "Hello");
edit.Call("add_text", " world");
edit.Call("set_placeholder_text", "prompt");
Console.WriteLine(edit.Call("get_text"));
Console.WriteLine(edit.Call("get_placeholder_text"));
if (edit.Call("get_accepted_chars") != null) throw new Exception("default accepted chars must be null");
if (dropdown.Call("get_text") != null) throw new Exception("default dropdown text must be null");
if (edit.Child(0).Kind != "label") throw new Exception("native child kind inference");
try { edit.AddState(-1); throw new Exception("bad state accepted"); }
catch (ArgumentOutOfRangeException e) {}
edit.Call("set_password_mode", true);
Console.WriteLine(edit.Call("get_password_mode"));
var spin = root.Create("spinbox");
spin.Call("set_range", 0, 100);
spin.Call("set_value", 10);
spin.Call("set_step", 5);
spin.Call("increment");
Console.WriteLine(spin.Call("get_value"));
spin.Call("decrement");
Console.WriteLine(spin.Call("get_value"));
var led = root.Create("led");
led.Call("set_brightness", 77);
led.Call("set_color", 0xFF3300);
Console.WriteLine(led.Call("get_brightness"));
try { led.Call("set_brightness", 256); }
catch (ArgumentOutOfRangeException e) { Console.WriteLine("numeric bounds checked"); }
try { led.Call("set_brightness"); }
catch (ArgumentException e) { Console.WriteLine("arity checked"); }
try { led.Call("set_text", "wrong widget"); }
catch (NotSupportedException e) { Console.WriteLine("receiver checked"); }
try { spin.Call("set_range", 10, 0); }
catch (ArgumentException e) { Console.WriteLine("range checked"); }
try { spin.Call("set_digit_format", 2, 3); }
catch (ArgumentException e) { Console.WriteLine("separator checked"); }
try { gui.Screen.Create("chart").Call("set_point_count", 0); }
catch (ArgumentOutOfRangeException e) { Console.WriteLine("resource count checked"); }
edit.Call("set_accepted_chars", new string('a', 3));
dropdown.Call("set_text", new string('t', 4));
GC.Collect();
Console.WriteLine(edit.Call("get_accepted_chars"));
Console.WriteLine(dropdown.Call("get_text"));
// More live widget handles than MCS_MAX_PINS (default 16) use one shared owner root.
var handles = new List<object>();
for (int i = 0; i < 40; i++) handles.Add(root.Create("label"));
Console.WriteLine(handles.Count);
gui.Pump(40);
gui.Dispose();
try { handles[0].Text = "disposed"; }
catch (ObjectDisposedException e) { Console.WriteLine("shared owner disposed safely"); }
