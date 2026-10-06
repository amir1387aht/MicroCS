// 08 · ADC voltmeter with averaging and a battery-level bar.
// Run: ./mcs --sim examples/hardware/08_adc_voltmeter.cs
const int BatteryChannel = 1;
Console.WriteLine($"ADC: {ADC.Resolution}-bit, reference {ADC.ReferenceMillivolts} mV");

for (int i = 0; i < 3; i++)
{
    int raw = ADC.ReadAverage(BatteryChannel, 8);       // average 8 samples
    int mv = ADC.ReadMillivolts(BatteryChannel);
    int bars = Math.Clamp(mv * 10 / ADC.ReferenceMillivolts, 0, 10);
    Console.WriteLine($"raw {raw,5}  {mv,5} mV  [{new string('#', bars)}{new string('.', 10 - bars)}]");
    Thread.Sleep(100);
}
