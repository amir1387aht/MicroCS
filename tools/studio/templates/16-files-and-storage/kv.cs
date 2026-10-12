// A tiny persistent key/value store (one "key=value" per line).
Dictionary<string, string> Load(string path)
{
    var d = new Dictionary<string, string>();
    if (!File.Exists(path)) return d;
    foreach (var l in File.ReadAllLines(path)) { int i = l.IndexOf('='); if (i > 0) d[l.Substring(0, i)] = l.Substring(i + 1); }
    return d;
}
void Save(string path, Dictionary<string, string> d) => File.WriteAllLines(path, d.Select(kv => kv.Key + "=" + kv.Value).ToArray());

var store = Load("/store.txt");
int runs = store.ContainsKey("runs") ? int.Parse(store["runs"]) : 0;
store["runs"] = (runs + 1).ToString();
store["last"] = Environment.TickCount.ToString();
Save("/store.txt", store);
Console.WriteLine($"this script ran {runs + 1} time(s)");
