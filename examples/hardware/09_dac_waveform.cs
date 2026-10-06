// 09 · DAC: generate a triangle wave and set an exact voltage.
// Run: ./mcs --sim examples/hardware/09_dac_waveform.cs
int max = (1 << DAC.Resolution) - 1;
Console.WriteLine($"DAC is {DAC.Resolution}-bit (0..{max})");

for (int step = 0; step <= 8; step++)
{
    int v = step <= 4 ? step * max / 4 : (8 - step) * max / 4;
    DAC.Write(0, v);
}
DAC.WriteMillivolts(0, 1650);                 // mid-rail on a 3.3 V board
Console.WriteLine("channel 0 parked at 1650 mV");
