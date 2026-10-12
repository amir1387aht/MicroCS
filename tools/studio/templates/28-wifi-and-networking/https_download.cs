// https:// works when the port has TLS (ESP32: esp-tls + certificate bundle)
Console.WriteLine("TLS available: " + Net.Has("Tls"));
Wifi.Connect("MicroCS-Lab", "microcs123");
try
{
    var r = Http.Get("https://example.com/");
    Console.WriteLine($"{r.StatusCode}  {r.ContentLength} bytes from {r.Url}");
    long n = Http.Download("https://example.com/", "/page.html");
    Console.WriteLine($"saved {n} bytes, file has {File.ReadAllText("/page.html").Length} chars");
}
catch (IOException e) { Console.WriteLine("failed: " + e.Message); }
catch (TimeoutException) { Console.WriteLine("timed out"); }
catch (NotSupportedException e) { Console.WriteLine(e.Message); }
