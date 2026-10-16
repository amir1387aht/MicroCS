// Actual hardware shutdown/poweroff, not a deep-sleep alias. USB can disappear.
// STM32 L4: select PA0/WKUP1 (0), active high, external pulldown as needed.
// Zephyr CONFIG_POWEROFF: no portable GPIO setup; recovery is board-specific.
// Never use unconditionally in boot.cs/main.cs; save files and stop radios first.
const bool ENTER_SHUTDOWN = false;
const int WAKE_PIN = -1;
if (!Hal.Has("Power.Shutdown")) return;
if (!ENTER_SHUTDOWN) { Console.WriteLine("Read board recovery procedure, then enable ENTER_SHUTDOWN"); return; }
Power.ClearWakePin();
if (Hal.Has("Power.Shutdown.Pin")) {
    if (WAKE_PIN < 0) { Console.WriteLine("Select the board's supported high-level wake input"); return; }
    GPIO.Mode(WAKE_PIN, GPIO.InputPullDown);
    Power.SetWakePin(WAKE_PIN, true);
}
Power.Sleep(1000);
Power.Shutdown(); // no continuation on hardware; --sim only records the request
