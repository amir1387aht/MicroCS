// Read a TMP102-style I2C temperature sensor once a second, keep running
// statistics and append CSV lines to the filesystem.
// Run on the host:  ./mcs --sim --ramfs 32768 --run-for 5500 examples/sensor_logger.cs
using System;
using System.Collections.Generic;
using System.Linq;

const int Bus = 0, Addr = 0x48;
const string LogFile = "/temp.csv";

double ReadCelsius() {
    byte[] raw = I2C.WriteRead(Bus, Addr, new byte[] { 0x00 }, 2);
    int counts = (raw[0] << 4) | (raw[1] >> 4);
    if ((counts & 0x800) != 0) counts -= 4096;     // 12-bit two's complement
    return counts * 0.0625;
}

string Classify(double t) => t switch {
    < 0 => "freezing",
    >= 0 and < 18 => "cold",
    >= 18 and <= 26 => "ok",
    _ => "hot",
};

var samples = new List<double>();
File.WriteAllText(LogFile, "ms,celsius\n");

Scheduler.Every(1000, () => {
    double t = ReadCelsius();
    samples.Add(t);
    File.AppendAllText(LogFile, $"{Environment.TickCount},{t:F2}\n");
    Console.WriteLine($"{t,6:F2} °C  {Classify(t)}");
});

Scheduler.After(5200, () => {
    var (min, max, avg) = (samples.Min(), samples.Max(), samples.Average());
    Console.WriteLine($"n={samples.Count} min={min:F2} max={max:F2} avg={avg:F2}");
    Console.WriteLine($"log has {File.ReadAllLines(LogFile).Length - 1} rows");
});
