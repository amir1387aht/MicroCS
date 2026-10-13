// Push data to browsers without polling: HttpServer + WebSocket endpoint + Broadcast
Wifi.Connect("MicroCS-Lab", "microcs123");
using var web = new HttpServer(80);
const string Page = @"<!doctype html><meta name=viewport content='width=device-width'>
<h1>Live</h1><p id=v>...</p><button onclick=""ws.send('reset')"">Reset</button>
<script>
var ws = new WebSocket('ws://' + location.host + '/ws');
ws.onmessage = e => document.getElementById('v').textContent = e.data;
</script>";
web.Get("/", req => Page);

int count = 0;
web.WebSocket("/ws",
    (ws, msg) => { if ((string)msg == "reset") count = 0; },       // a message from a browser
    ws => Console.WriteLine($"browser {ws.Id} connected from {ws.RemoteAddress}"),
    ws => Console.WriteLine($"browser {ws.Id} left ({ws.CloseStatus})"));
web.Start();
Console.WriteLine($"open http://{Wifi.Ip}:{web.Port}/");

var next = Environment.TickCount;
// Runs until Studio Stop / Ctrl-C; other shell commands wait until it stops.
while (true)
{
    web.Poll(50);                               // HTTP requests + WebSocket frames
    if (Environment.TickCount - next >= 0)
    {
        next += 500;
        count++;
        web.Broadcast($"{{\"count\":{count},\"uptime\":{Environment.TickCount}}}");
    }
}
Console.WriteLine($"{web.WebSockets} browser(s) connected, count {count}");
