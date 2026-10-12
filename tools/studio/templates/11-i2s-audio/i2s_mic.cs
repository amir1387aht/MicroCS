// INMP441-style microphone on I2S bus 0 (receive).
I2S.Open(0, 16000, 32, 1, I2S.Receive);
for (int n = 0; n < 40; n++)
{
    int[] s = I2S.ReadSamples(0, 64);                    // 64 x 32-bit = 256 bytes per call
    double sum = 0;
    foreach (int x in s) sum += (double)x * x;
    Console.WriteLine($"level {Math.Sqrt(sum / s.Length):F0}");
}
I2S.Close(0);
