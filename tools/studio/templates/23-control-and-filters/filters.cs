// Three classic filters for noisy sensor values
class MovingAverage
{
    readonly double[] buf; int n, i; double sum;
    public MovingAverage(int size) { buf = new double[size]; }
    public double Add(double v) { sum += v - buf[i]; buf[i] = v; i = (i + 1) % buf.Length; if (n < buf.Length) n++; return sum / n; }
}
class Median
{
    readonly List<double> win = new List<double>(); readonly int size;
    public Median(int size) { this.size = size; }
    public double Add(double v) { win.Add(v); if (win.Count > size) win.RemoveAt(0); var s = win.Order().ToList(); return s[s.Count / 2]; }
}
class Ema
{
    readonly double alpha; double y; bool init;
    public Ema(double alpha) { this.alpha = alpha; }
    public double Add(double v) { y = init ? y + alpha * (v - y) : v; init = true; return y; }
}

var rnd = new Random(1);
var avg = new MovingAverage(8); var med = new Median(5); var ema = new Ema(0.2);
for (int t = 0; t < 60; t++)
{
    double raw = 50 + 10 * Math.Sin(t / 8.0) + (rnd.NextDouble() - 0.5) * 8 + (t % 17 == 0 ? 40 : 0);  // noise + spikes
    Console.WriteLine($"raw:{raw:F1} avg:{avg.Add(raw):F1} median:{med.Add(raw):F1} ema:{ema.Add(raw):F1}");
}
