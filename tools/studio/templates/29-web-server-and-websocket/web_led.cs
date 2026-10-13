// HttpServer: routes, path parameters and JSON in a few lines (Net.Has("HttpServer"))
if (!Hal.Has("GPIO") || !Hal.Has("Wifi") || !Hal.Has("HttpServer"))
{
    Console.WriteLine("This firmware lacks: GPIO, Wifi, HttpServer. Enable the needed modules or use a supported board.");
    return;
}
Wifi.Connect("MicroCS-Lab", "microcs123");
var led = new Pin(2, GPIO.Output);              // your LED's GPIO (Pico W: "LED")
using var web = new HttpServer(80);

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
Console.WriteLine("Serving until Studio Stop / Ctrl-C. Other shell commands wait until stopped.");
web.Run();
