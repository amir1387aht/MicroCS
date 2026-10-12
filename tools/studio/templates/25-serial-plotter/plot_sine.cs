// Open the console's "Plotter" view, then Run: each line is one sample, "name:value" pairs are series.
for (int i = 0; i < 400; i++)
{
    double t = i / 20.0;
    Console.WriteLine($"sin:{Math.Sin(t):F3} cos:{Math.Cos(t):F3} mix:{0.5 * Math.Sin(3 * t) + 0.3 * Math.Cos(t):F3}");
    Thread.Sleep(20);
}
