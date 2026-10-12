// Read settings from /settings.txt ("key=value" lines, # comments).
// The file is created with defaults the first time.
const string SettingsFile = "/settings.txt";
if (!File.Exists(SettingsFile))
    File.WriteAllText(SettingsFile, "# MicroCS settings\nname=my-board\nled=2\nblink_ms=500\n");

var cfg = new Dictionary<string, string>();
foreach (var raw in File.ReadAllLines(SettingsFile))
{
    var line = raw.Trim();
    if (line.Length == 0 || line.StartsWith("#")) continue;
    int eq = line.IndexOf('=');
    if (eq > 0) cfg[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
}

string Get(string key, string def) => cfg.TryGetValue(key, out var v) ? v : def;

Console.WriteLine($"name = {Get("name", "?")}");
var led = new Pin(int.Parse(Get("led", "2")), GPIO.Output);
Scheduler.Every(int.Parse(Get("blink_ms", "500")), () => led.Toggle());
