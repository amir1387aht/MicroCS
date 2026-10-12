// Use what the board has, skip what it does not.
if (Hal.Has("DAC")) { DAC.WriteMillivolts(0, 1000); Console.WriteLine("DAC set to 1 V"); }
else Console.WriteLine("no DAC on this board");
if (Hal.Has("CAN")) Console.WriteLine("CAN available");
if (Hal.Has("RTC")) Console.WriteLine($"RTC time {RTC.Now}");
