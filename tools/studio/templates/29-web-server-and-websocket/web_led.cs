// HttpServer: routes, path parameters and JSON in a few lines (Net.Has("HttpServer"))
Wifi.Connect("MicroCS-Lab", "microcs123");
var led = new Pin(2, GPIO.Output);              // your LED's GPIO (Pico W: "LED")
var web = new HttpServer(80);

web.Get("/", req => $@"<!doctype html><meta name=viewport content='width=device-width'>
<h1>MicroCS</h1><p>LED is <b>{(led.Read() ? "on" : "off")}</b></p>
<form method=post action=/led/on><button>On</button></form>
<form method=post action=/led/off><button>Off</button></form>");

web.Post("/led/{state}", req =>
{
    led.Write(req.Param("state") == "on");
    return req.Redirect("/");                   // back to the page (303)
});
web.Get("/api/status", req => new Dictionary<string, object>
{
    ["led"] = led.Read(), ["uptime"] = Environment.TickCount, ["rssi"] = Wifi.Rssi
});                                             // objects come back as JSON

web.Start();
Console.WriteLine($"open http://{Wifi.Ip}:{web.Port}/");
var until = Environment.TickCount + 2000;       // demo: 2 s; on a board just call web.Run()
while (Environment.TickCount < until) web.Poll(100);
Console.WriteLine($"served {web.Requests} request(s)");
