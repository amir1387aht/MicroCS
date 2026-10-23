// Strict JSON, mixed data and computed properties; no network required.
if (!Hal.Has("Json.TryParse") || !Hal.Has("Json.ComputedProperties")) {
    Console.WriteLine("This example needs the extended JSON API.");
    return;
}
class Sample {
    public string Name = "temperature";
    public int Raw = 21;
    public int Doubled => Raw * 2;
    private string _internal = "not included";
}
var packet = new Dictionary<string, object> {
    ["sample"] = new Sample(),
    ["values"] = new object[] {1, true, null, "text"}
};
string text = Json.Serialize(packet, true);
Console.WriteLine(text);
object decoded;
if (Json.TryParse(text, out decoded))
    Console.WriteLine(decoded["sample"]["Doubled"]);
if (!Json.TryParse("{broken}", out decoded))
    Console.WriteLine("Invalid input rejected without throwing.");
