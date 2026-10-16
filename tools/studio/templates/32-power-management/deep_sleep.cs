// Waking resets the firmware and loses RAM/VM state. USB may disconnect.
// Do NOT run unconditionally from boot.cs/main.cs/jobs.cfg (sleep loop).
// Save/close files and stop radios before enabling this example.
const bool ENTER_DEEP_SLEEP = false;
if (!Hal.Has("Power.DeepSleep.Timer")) return;
if (!ENTER_DEEP_SLEEP) { Console.WriteLine("Read docs/POWER.md, then enable ENTER_DEEP_SLEEP"); return; }
Power.ClearWakePin();
Power.Sleep(1000); // leave time to read the warning
Power.DeepSleep(5000);
// No continuation on physical hardware. --sim records the request and returns.
