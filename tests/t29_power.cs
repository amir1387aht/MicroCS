// args: --sim
// Simulator records hardware transitions but intentionally does not reset or power off.
Console.WriteLine("power " + Hal.Has("Power.Sleep") + " timer " + Hal.Has("Power.LightSleep.Timer"));
Console.WriteLine("initial " + Power.WakePin + " " + Power.WakeHigh);
Power.Sleep(0);
Power.SetWakePin(2, true);
Console.WriteLine("selected " + Power.WakePin + " " + Power.WakeHigh);
Power.LightSleep(1);
Power.DeepSleep();
Power.ClearWakePin();
Power.DeepSleep(1);
Power.Shutdown();
Console.WriteLine("simulated transitions returned");
try { Power.Sleep(-1); } catch (ArgumentOutOfRangeException) { Console.WriteLine("negative rejected"); }
try { Power.LightSleep(); } catch (ArgumentOutOfRangeException) { Console.WriteLine("no wake rejected"); }
try { Power.SetWakePin(64, false); } catch (ArgumentException) { Console.WriteLine("pin rejected"); }
Console.WriteLine("cleared " + Power.WakePin + " " + Power.WakeHigh);
