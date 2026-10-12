// Step response of a damped spring (second-order system) - for the Plotter view
double x = 0, v = 0, target = 0;
const double k = 4.0, c = 0.8, dt = 0.05;
for (int i = 0; i < 300; i++)
{
    if (i == 20) target = 1.0;
    if (i == 160) target = -0.5;
    double a = k * (target - x) - c * v;
    v += a * dt; x += v * dt;
    Console.WriteLine($"target:{target:F2} position:{x:F3}");
    Thread.Sleep(15);
}
