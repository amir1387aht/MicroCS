// Battery through a 100k/100k divider into ADC channel 0 (so the pin sees half the voltage).
const double Divider = 2.0;
double v = ADC.ReadAverage(0, 32) * ADC.ReferenceMillivolts / (double)((1 << ADC.Resolution) - 1) / 1000.0 * Divider;
double pct = Math.Clamp((v - 3.3) / (4.2 - 3.3) * 100, 0, 100);
Console.WriteLine($"battery {v:F2} V, about {pct:F0} %");
