// DAC channel 0 (ESP32: GPIO 25, STM32: PA4).
DAC.WriteMillivolts(0, 1650);
Console.WriteLine($"{DAC.Resolution}-bit DAC at 1.65 V");
Thread.Sleep(1000);

int max = (1 << DAC.Resolution) - 1;
for (int i = 0; i < 400; i++)
{
    DAC.Write(0, (int)((Math.Sin(i * Math.PI / 50) + 1) / 2 * max));
    Thread.Sleep(5);
}
