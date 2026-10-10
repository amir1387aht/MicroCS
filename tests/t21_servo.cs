// args: --sim --sim-log
// Servo: hobby servos on a PWM channel (the built-in "servo" driver, HAL PWM backend)
var s = new Servo(0);
Console.WriteLine($"{s.Channel} {s.MinPulse} {s.MaxPulse} {s.MaxAngle} {s.Attached} {s.Angle} {s.Pulse}");
s.Angle = 0; Console.WriteLine($"{s.Angle} {s.Pulse} {s.Attached}");
s.Write(90); Console.WriteLine($"{s.Read()} {s.Pulse}");
s.Angle = 180; Console.WriteLine($"{s.Angle} {s.Pulse}");
s.Pulse = 1500; Console.WriteLine($"{s.Angle} {s.Pulse}");
s.WritePulse(600); Console.WriteLine($"{s.Angle} {s.Pulse}");
// float angles
s.Angle = 45.5; Console.WriteLine($"{s.Angle} {s.Pulse}");
// custom range and travel
var m = new Servo(2, 1000, 2000, 90);
m.Angle = 45; Console.WriteLine($"{m.Pulse} {m.MaxAngle}");
m.Angle = 90; Console.WriteLine($"{m.Pulse}");
// out of range
try { s.Angle = 181; } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { s.Angle = -1; } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { m.Pulse = 2100; } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { new Servo(0, 2000, 1000); } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { new Servo(-1); } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { new Servo(); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
// eased sweep: blocks for the given time, ends exactly on the target
var sw = Stopwatch.StartNew();
s.MoveTo(0, 200);
var took = sw.ElapsedMilliseconds;
Console.WriteLine($"{s.Angle} {s.Pulse} {took >= 190 && took < 400}");
// detach / attach
s.Detach(); Console.WriteLine(s.Attached);
s.Attach(); Console.WriteLine($"{s.Attached} {s.Pulse}");
s.Stop();
s.Dispose();
try { s.Angle = 10; } catch (ObjectDisposedException e) { Console.WriteLine(e.Message); }
// drivers: Servo comes from the built-in "servo" driver
Console.WriteLine($"{Drivers.Has("servo")} {Drivers.Has("Servo")} {Hal.Has("Servo")} {string.Join(",", Drivers.List)}");
