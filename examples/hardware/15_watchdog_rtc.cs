// 15 · Watchdog + real-time clock + board information.
// Run: ./mcs --sim examples/hardware/15_watchdog_rtc.cs
Console.WriteLine($"board {Hal.Board}, {Hal.CpuHz / 1_000_000} MHz, id {Hal.UniqueId}");

Watchdog.Start(2000);                    // reset the MCU if not fed for 2 s
RTC.Set(1735689600);                     // 2025-01-01 00:00:00 UTC (Unix seconds)

for (int i = 0; i < 3; i++)
{
    Watchdog.Feed();
    Console.WriteLine($"alive, RTC = {RTC.Now} s");
    Thread.Sleep(500);
}
foreach (string f in new[] { "I2S", "QSPI", "CAN", "DAC", "GPIO.IRQ" })
    Console.WriteLine($"  has {f,-8} {Hal.Has(f)}");
