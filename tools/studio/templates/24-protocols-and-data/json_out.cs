// Build JSON by hand (MicroCS has no JSON library) - escape strings properly
string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n");
string Json(Dictionary<string, object> d)
{
    var parts = d.Select(kv => $"\"{Esc(kv.Key)}\":" + kv.Value switch
    {
        null => "null",
        string s => $"\"{Esc(s)}\"",
        bool b => b ? "true" : "false",
        double x => x.ToString("F3"),
        _ => kv.Value.ToString(),
    });
    return "{" + string.Join(",", parts) + "}";
}

for (int i = 0; i < 3; i++)
{
    var msg = new Dictionary<string, object>
    {
        ["board"] = Hal.Board, ["uptime"] = Environment.TickCount, ["adc"] = ADC.Read(0),
        ["volts"] = ADC.ReadVoltage(0), ["ok"] = true, ["note"] = "line \"" + i + "\"",
    };
    Console.WriteLine(Json(msg));
    Thread.Sleep(500);
}
