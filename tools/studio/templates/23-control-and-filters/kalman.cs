// 1-D Kalman filter: estimate a value from noisy measurements
class Kalman
{
    double x, p = 1;                  // estimate and its variance
    readonly double q, r;             // process noise, measurement noise
    public Kalman(double q, double r, double initial) { this.q = q; this.r = r; x = initial; }
    public double Update(double z)
    {
        p += q;                       // predict
        double k = p / (p + r);       // gain
        x += k * (z - x);             // correct
        p *= 1 - k;
        return x;
    }
}

var kf = new Kalman(0.01, 4, 25);
var rnd = new Random(7);
for (int i = 0; i < 50; i++)
{
    double truth = 25 + i * 0.05;
    double z = truth + (rnd.NextDouble() - 0.5) * 6;
    Console.WriteLine($"truth:{truth:F2} measured:{z:F2} kalman:{kf.Update(z):F2}");
}
