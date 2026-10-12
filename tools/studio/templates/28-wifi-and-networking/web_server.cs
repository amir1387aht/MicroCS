// Minimal HTTP/1.0 server: one request per connection
Wifi.Connect("MicroCS-Lab", "microcs123");
var server = new TcpListener(8080);
server.Start();
Console.WriteLine($"listening on http://{Wifi.Ip}:{server.LocalPort}/");

int hits = 0;
var until = Environment.TickCount + 2000;               // demo: run for 2 s (use while (true) on a board)
while (Environment.TickCount < until)
{
    var c = server.AcceptTcpClient(200);        // null when nobody connected in 200 ms
    if (c == null) continue;
    string req = c.ReadLine() ?? "";
    while ((c.ReadLine() ?? "") != "") { }      // skip headers
    hits++;
    bool json = req.StartsWith("GET /api");
    string body = json ? $"{{\"hits\":{hits},\"uptime\":{Environment.TickCount}}}"
                       : $"<h1>MicroCS</h1><p>hits: {hits}</p>";
    c.Write($"HTTP/1.0 200 OK\r\nContent-Type: {(json ? "application/json" : "text/html")}\r\n" +
            $"Content-Length: {body.Length}\r\nConnection: close\r\n\r\n{body}");
    c.Close();
}
server.Stop();
Console.WriteLine($"served {hits} request(s)");
