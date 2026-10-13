// Minimal HTTP/1.0 server: one request per connection
if (!Hal.Has("Wifi") || !Hal.Has("TcpListener"))
{
    Console.WriteLine("This firmware lacks: Wifi, TcpListener. Enable the needed modules or use a supported board.");
    return;
}
Wifi.Connect("MicroCS-Lab", "microcs123");
using var server = new TcpListener(80);
server.Start();
Console.WriteLine($"listening on http://{Wifi.Ip}:{server.LocalPort}/");

int hits = 0;
// Runs until Studio Stop / Ctrl-C. Other shell commands wait until it stops.
while (true)
{
    using var c = server.AcceptTcpClient(200);        // null when nobody connected in 200 ms
    if (c == null) continue;
    try
    {
        c.Timeout = 2000;                         // incomplete browser connections must not wait forever
        string req = c.ReadLine() ?? "";
        while ((c.ReadLine() ?? "") != "") { }      // skip headers
        hits++;
        Console.WriteLine($"request {hits}: {req}");
        bool json = req.StartsWith("GET /api");
        string body = json ? $"{{\"hits\":{hits},\"uptime\":{Environment.TickCount}}}"
                           : $"<h1>MicroCS</h1><p>hits: {hits}</p>";
        c.Write($"HTTP/1.0 200 OK\r\nContent-Type: {(json ? "application/json" : "text/html")}\r\n" +
                $"Content-Length: {body.Length}\r\nConnection: close\r\n\r\n{body}");
        c.Close();
    }
    catch (TimeoutException) { }               // browser preconnect / partial request
    catch (IOException) { }                    // browser disconnected early
}
