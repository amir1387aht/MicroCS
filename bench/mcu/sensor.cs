// typical firmware work: filter samples in an array, floats, string output
double[] buf = new double[64];
int seed = 12345;
int Next() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed; }
double Avg(double[] a) { double s = 0; for (int i = 0; i < a.Length; i++) s += a[i]; return s / a.Length; }
double ema = 0;
int alarms = 0;
for (int t = 0; t < 300; t++) {
    double v = 20.0 + (Next() % 1000) / 100.0;
    buf[t % buf.Length] = v;
    ema = ema * 0.9 + v * 0.1;
    if (v > 29.5) alarms++;
}
Console.WriteLine($"avg={Avg(buf):F2} ema={ema:F2} alarms={alarms}");
