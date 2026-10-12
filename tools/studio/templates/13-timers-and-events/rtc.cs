// RTC.Now is Unix time in seconds. Set it once (e.g. from NTP / GPS / the PC).
if (RTC.Now < 1700000000) RTC.Set(1735689600);          // 2025-01-01 00:00:00 UTC
int now = RTC.Now;
int days = now / 86400, secs = now % 86400;
Console.WriteLine($"unix {now}: day {days} since 1970, {secs / 3600:D2}:{secs / 60 % 60:D2}:{secs % 60:D2} UTC");
