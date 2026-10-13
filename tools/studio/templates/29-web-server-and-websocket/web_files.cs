// Put your web app into /www (MicroCS Studio: Files panel, or File.WriteAllText)
if (!Hal.Has("Filesystem") || !Hal.Has("Wifi") || !Hal.Has("HttpServer"))
{
    Console.WriteLine("This firmware lacks: Filesystem, Wifi, HttpServer. Enable the needed modules or use a supported board.");
    return;
}
Directory.CreateDirectory("/www");
File.WriteAllText("/www/index.html", "<!doctype html><link rel=stylesheet href=style.css><h1>Hello from flash</h1>");
File.WriteAllText("/www/style.css", "body { font-family: sans-serif; background: #eef; }");

Wifi.Connect("MicroCS-Lab", "microcs123");
using var web = new HttpServer(80);
web.Get("/api/files", req => Directory.GetFiles("/www"));       // a route next to the files
web.ServeFiles("/", "/www");                    // GET /  -> /www/index.html, /style.css ...
web.Start();
Console.WriteLine($"open http://{Wifi.Ip}:{web.Port}/");
Console.WriteLine("Serving until Studio Stop / Ctrl-C. Other shell commands wait until stopped.");
web.Run();
