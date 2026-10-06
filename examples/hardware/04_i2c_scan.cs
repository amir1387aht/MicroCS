// 04 · I2C bus scan — the first thing to run on a new board.
// Run: ./mcs --sim examples/hardware/04_i2c_scan.cs
I2C.Open(0, 400000);                          // bus 0 at 400 kHz
var found = I2C.Scan(0);                      // List<int> of responding 7-bit addresses
Console.WriteLine($"{found.Count} device(s) on I2C bus 0:");
foreach (int addr in found)
{
    string guess = addr switch
    {
        0x48 => "TMP102 / LM75 temperature sensor",
        0x50 => "24Cxx EEPROM",
        0x68 => "MPU-6050 IMU / DS3231 RTC",
        0x76 or 0x77 => "BME280 / BMP280",
        0x3C => "SSD1306 OLED",
        _ => "unknown",
    };
    Console.WriteLine($"  0x{addr:X2}  {guess}");
}
