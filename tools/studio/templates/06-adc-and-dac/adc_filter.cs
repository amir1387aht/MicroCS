// Smooth a noisy analog signal: oversample, then low-pass filter.
double avg = ADC.ReadAverage(0, 16);
for (int i = 0; i < 50; i++)
{
    int sample = ADC.ReadAverage(0, 16);     // 16 readings averaged in C
    avg = avg * 0.9 + sample * 0.1;          // exponential moving average
    if (i % 5 == 0) Console.WriteLine($"sample {sample,5}  filtered {avg,8:F1}");
    Thread.Sleep(20);
}
