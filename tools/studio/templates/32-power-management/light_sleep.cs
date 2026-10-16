// docs/POWER.md: ESP32 light sleep; RP2/STM32 CPU idle, not dormant/STOP.
// Stop radios first (Wifi.Off(), Ble.Reset() if you started them).
// Studio commands are not processed until wake; USB may need reconnection.
if (!Hal.Has("Power.LightSleep.Timer")) return;
Power.ClearWakePin();
Console.WriteLine("Sleeping for one second; wait for wake before uploading");
Power.LightSleep(1000);
Console.WriteLine("Resumed");
