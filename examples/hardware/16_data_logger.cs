// 16 · Background jobs: a sensor logger + heartbeat LED using the scheduler.
// Scheduler jobs keep running after the script body ends (like a firmware main loop).
// Run: ./mcs --sim --ramfs 16384 --run-for 1200 examples/hardware/16_data_logger.cs
I2C.Open(0, 400000);
var sensor = new I2cDevice(0, 0x48);
var led = new Pin("LED", GPIO.Output);
File.WriteAllText("/log.csv", "ms,celsius\n");

Scheduler.Every(100, () => led.Toggle());             // heartbeat
Scheduler.Every(250, () =>
{
    byte[] r = sensor.ReadRegisters(0, 2);
    double c = ((r[0] << 4) | (r[1] >> 4)) * 0.0625;
    File.AppendAllText("/log.csv", $"{Environment.TickCount},{c:F2}\n");
});
Scheduler.After(1100, () =>
{
    string[] lines = File.ReadAllLines("/log.csv");
    Console.WriteLine($"logged {lines.Length - 1} samples; last: {lines[^1]}");
});
