// A plain HTTP smoke test; no dependency on a JSON / time API.
// Use your own endpoint or https://example.com/ if your network blocks port 80.
if (!Hal.Has("Wifi") || !Hal.Has("Http"))
{
    Console.WriteLine("This firmware lacks: Wifi, Http. Enable the needed modules or use a supported board.");
    return;
}
Wifi.Connect("MicroCS-Lab", "microcs123");
Http.Timeout = 15000;
try
{
    var r = Http.Get("http://example.com/");
    Console.WriteLine($"{r.StatusCode} {r.Reason}  {r.ContentType}  {r.Body.Length} bytes");
    Console.WriteLine(r.Body);
}
catch (IOException e) { Console.WriteLine("network error: " + e.Message); }
catch (TimeoutException) { Console.WriteLine("timed out"); }
