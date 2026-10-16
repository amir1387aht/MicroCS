// Choose a safe board input after checking its pinout. No universal wake GPIO.
// Sampled wake on RP2/STM32: hold the input level; short pulses can be missed.
const int WAKE_PIN = -1; // replace -1; never choose a flash/USB/radio pin
if (!Hal.Has("Power.LightSleep.Pin") || !Hal.Has("Power.LightSleep.Timer")) return;
if (WAKE_PIN < 0) { Console.WriteLine("Select WAKE_PIN first (docs/POWER.md)"); return; }
GPIO.Mode(WAKE_PIN, GPIO.InputPullUp);
Power.SetWakePin(WAKE_PIN, false); // active low, button to GND
Console.WriteLine("Sleeping until input goes low or five seconds elapse");
Power.LightSleep(5000); // timeout prevents an accidental indefinite wait
Power.ClearWakePin();
Console.WriteLine("Resumed");
