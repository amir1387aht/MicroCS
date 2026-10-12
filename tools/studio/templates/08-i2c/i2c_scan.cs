// List every device on I2C bus 0 (set the SDA/SCL pins in the firmware's pin table).
I2C.Open(0, 400000);
var found = I2C.Scan(0);
Console.WriteLine($"{found.Count} device(s):");
foreach (int addr in found) Console.WriteLine($"  0x{addr:X2}");
