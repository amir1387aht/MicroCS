// Read ADC channel 0 a few times.
Console.WriteLine($"{ADC.Resolution}-bit ADC, reference {ADC.ReferenceMillivolts} mV");
for (int i = 0; i < 5; i++)
{
    int raw = ADC.Read(0);
    int mv = ADC.ReadMillivolts(0);
    double v = ADC.ReadVoltage(0);
    Console.WriteLine($"raw {raw,5}  {mv,5} mV  {v:F3} V");
    Thread.Sleep(200);
}
