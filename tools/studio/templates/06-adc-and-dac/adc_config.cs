// ESP32: Atten12dB = full ~0..3.1 V range. Oversample = samples averaged per reading.
var cfg = new AdcConfig(ADC.Atten12dB) { Oversample = 4 };
try { ADC.Configure(0, cfg); Console.WriteLine(cfg); }
catch (NotSupportedException e) { Console.WriteLine("board says: " + e.Message); }
for (int i = 0; i < 5; i++)
{
    Console.WriteLine($"raw {ADC.Read(0)}  {ADC.ReadMillivolts(0)} mV");
    Thread.Sleep(100);
}
