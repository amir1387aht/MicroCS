// Motor driver input: 20 kHz, 10-bit duty steps, active-low output.
var cfg = new PwmConfig(20_000) { ResolutionBits = 10, Inverted = true };
try { PWM.Configure(0, cfg); }
catch (NotSupportedException e) { Console.WriteLine("board says: " + e.Message); }
for (double d = 0; d <= 1.0; d += 0.25)
{
    PWM.Set(0, 20_000, d);                    // keeps the configuration
    Console.WriteLine($"duty {d:P0}");
    Thread.Sleep(100);
}
PWM.Stop(0);
