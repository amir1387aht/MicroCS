// Json.Parse / Json.Stringify / JsonSerializer
var d = Json.Parse("{\"a\": 1, \"b\": [true, null, 2.5, \"x\\u00e9\\n\"], \"c\": {\"d\": -3e2}, \"big\": 12345678901}");
Console.WriteLine(d["a"] + " " + d["b"][2] + " " + d["c"]["d"] + " " + d["big"] + " " + d["b"].Count);
Console.WriteLine(Json.Stringify(d));
Console.WriteLine(Json.Stringify(d, true));
class P { public string Name = "x"; public int Age { get; set; } = 3; private int _secret = 9; public List<int> L = new List<int> { 1, 2 }; }
Console.WriteLine(JsonSerializer.Serialize(new P()));
Console.WriteLine(Json.Stringify(new HashSet<int> { 1, 2 }) + Json.Stringify(new int[0]) + Json.Stringify(new Dictionary<string,int>()) + Json.Escape("q\"\t"));
try { Json.Parse("{\"a\":}"); } catch (FormatException e) { Console.WriteLine(e.Message); }
try { Json.Parse("[1,2] x"); } catch (FormatException e) { Console.WriteLine(e.Message); }
Console.WriteLine(Json.Stringify(Json.Parse("\"\\ud83d\\ude00\"")) + " " + Json.Stringify(double.NaN) + " " + Json.Stringify('c'));
Console.WriteLine(Json.Stringify(new Dictionary<string,int>()) + Json.Stringify(new Stack<int>(new[]{1,2})) + Json.Stringify(new Queue<string>()));
