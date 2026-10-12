// POST a JSON body (Content-Type is detected; pass it explicitly to be sure)
Wifi.Connect("MicroCS-Lab", "microcs123");
var reading = new Dictionary<string, object> { ["device"] = "greenhouse", ["temp"] = 21.5, ["hum"] = 48 };
string body = Json.Stringify(reading);
var headers = new Dictionary<string, string> { ["X-Api-Key"] = "demo" };
try
{
    var r = Http.Post("http://httpbin.org/post", body, "application/json", headers);
    Console.WriteLine($"{r.StatusCode}  server says: {r.Header("Content-Type")}");
    Console.WriteLine(r.Body.Length > 200 ? r.Body.Substring(0, 200) + "..." : r.Body);
}
catch (IOException e) { Console.WriteLine("post failed: " + e.Message); }
catch (TimeoutException) { Console.WriteLine("timed out"); }
