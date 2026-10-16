// Wake boots firmware from scratch. Do not install as an unconditional startup job.
// STM32 F4/L4/G4: WAKE_PIN = 0 (PA0/WKUP1), WAKE_HIGH = true ONLY.
// ESP32: choose an SDK deep-wake-capable GPIO, not just any light-wake pin.
// Use an appropriate external resistor for deep sleep; stop radios/save files first.
const bool ENTER_DEEP_SLEEP = false;
const int WAKE_PIN = -1;
const bool WAKE_HIGH = true;
if (!Hal.Has("Power.DeepSleep.Pin")) return;
if (!ENTER_DEEP_SLEEP || WAKE_PIN < 0) { Console.WriteLine("Choose pin/polarity and opt in (docs/POWER.md)"); return; }
GPIO.Mode(WAKE_PIN, WAKE_HIGH ? GPIO.InputPullDown : GPIO.InputPullUp);
Power.SetWakePin(WAKE_PIN, WAKE_HIGH);
Power.Sleep(1000);
Power.DeepSleep(); // pin-only: cannot service Studio until wake/reboot
