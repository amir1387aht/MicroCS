// A small command shell: type commands in the console (C# tab) after Run.
var led = new Pin(2, GPIO.Output);
var commands = new Dictionary<string, Action<string[]>>
{
    ["help"] = a => Console.WriteLine("commands: led on|off, blink <n>, pwm <0-100>, uptime, quit"),
    ["led"] = a => { led.Write(a.Length > 1 && a[1] == "on"); Console.WriteLine("led " + (led.Read() ? "on" : "off")); },
    ["blink"] = a => { int n = a.Length > 1 ? int.Parse(a[1]) : 3; for (int i = 0; i < n * 2; i++) { led.Toggle(); Thread.Sleep(150); } },
    ["pwm"] = a => { PWM.Set(0, 1000, int.Parse(a[1]) / 100.0); Console.WriteLine("duty " + a[1] + "%"); },
    ["uptime"] = a => Console.WriteLine($"{Environment.TickCount / 1000} s"),
};

Console.WriteLine("type 'help'");
for (;;)
{
    string line = Console.ReadLine();
    if (line == null || line.Trim() == "quit") break;
    string[] args = line.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
    if (args.Length == 0) continue;
    try
    {
        if (commands.TryGetValue(args[0].ToLower(), out var cmd)) cmd(args);
        else Console.WriteLine("unknown command - try 'help'");
    }
    catch (Exception e) { Console.WriteLine("error: " + e.Message); }
}
