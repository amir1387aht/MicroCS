// What is this board and what can it do?
Console.WriteLine($"board     {Hal.Board}");
Console.WriteLine($"HAL API   v{Hal.ApiVersion}");
Console.WriteLine($"CPU       {Hal.CpuHz / 1000000} MHz");
Console.WriteLine($"unique id {Hal.UniqueId}");
Console.WriteLine($"heap use  {GC.GetTotalMemory(false)} bytes");
var all = new[] { "GPIO", "GPIO.IRQ", "UART", "I2C", "SPI", "ADC", "DAC", "PWM", "Timer", "I2S", "QSPI", "CAN", "Watchdog", "RTC" };
Console.WriteLine("has       " + string.Join(" ", all.Where(n => Hal.Has(n))));
Console.WriteLine("missing   " + string.Join(" ", all.Where(n => !Hal.Has(n))));
if (Hal.Has("Drivers")) Console.WriteLine("drivers   " + string.Join(" ", Drivers.List));   // e.g. ws2812 (LedStrip)
