// ESP32 / RP2: any pins at run time. STM32: pins come from CubeMX - leave Sda/Scl unset.
var i2c = new I2cConfig(400_000, 21, 22)    // frequency, SDA, SCL
{
    PullUps = true,                           // false when the board has external resistors
    TimeoutMs = 20,
};
Console.WriteLine(i2c);
I2C.Open(0, i2c);
var found = I2C.Scan(0);
Console.WriteLine($"{found.Count} device(s): " + string.Join(", ", found.ConvertAll(a => $"0x{a:X2}")));
