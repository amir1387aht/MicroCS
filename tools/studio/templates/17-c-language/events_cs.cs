class Thermostat
{
    public event Action<double> TooHot;
    public double Limit = 30;
    public void Report(double t) { if (t > Limit) TooHot?.Invoke(t); }
}

Func<int, int> square = x => x * x;
Action<string> log = msg => Console.WriteLine($"[log] {msg}");
log($"square(7) = {square(7)}");

int alarms = 0;
var th = new Thermostat();
th.TooHot += t => { alarms++; log($"too hot: {t}"); };
foreach (var t in new[] { 25.0, 31.5, 29.0, 33.0 }) th.Report(t);
log($"{alarms} alarms");
