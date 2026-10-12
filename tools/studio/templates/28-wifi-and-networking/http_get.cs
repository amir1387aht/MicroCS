// Http.Get returns an HttpResponse; network errors throw SocketException / TimeoutException
Wifi.Connect("MicroCS-Lab", "microcs123");
Http.Timeout = 8000;
try
{
    var r = Http.Get("http://worldtimeapi.org/api/timezone/Etc/UTC");
    Console.WriteLine($"{r.StatusCode} {r.Reason}  {r.ContentType}  {r.Body.Length} bytes");
    if (r.IsSuccess)
    {
        var doc = Json.Parse(r.Body);
        Console.WriteLine("utc: " + doc["datetime"]);
    }
}
catch (IOException e) { Console.WriteLine("network error: " + e.Message); }
catch (TimeoutException) { Console.WriteLine("timed out"); }
