// Log ADC channel 0 every second to /log.csv, rotate the file at 16 KB.
const string Log = "/log.csv";
if (!File.Exists(Log)) File.WriteAllText(Log, "ms,mV\n");

Scheduler.Every(1000, () =>
{
    int mv = ADC.ReadMillivolts(0);
    File.AppendAllText(Log, $"{Environment.TickCount},{mv}\n");
    if (File.GetLength(Log) > 16 * 1024)
    {
        if (File.Exists("/log.old.csv")) File.Delete("/log.old.csv");
        File.Move(Log, "/log.old.csv");
        File.WriteAllText(Log, "ms,mV\n");
    }
}, 5);
Console.WriteLine($"logging to {Log}");
