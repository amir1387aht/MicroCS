// Ntp.Sync waits until the first SNTP answer (the host already has the right time)
Wifi.Connect("MicroCS-Lab", "microcs123");
Ntp.Timezone = "CET-1CEST,M3.5.0,M10.5.0/3";    // POSIX TZ string (Berlin)
Console.WriteLine("synced: " + Ntp.Sync(10000));
for (int i = 0; i < 3; i++)
{
    Console.WriteLine($"{Ntp.LocalTime}   utc {Ntp.UtcTime}   offset {Ntp.UtcOffset / 3600} h");
    Thread.Sleep(1000);
}
