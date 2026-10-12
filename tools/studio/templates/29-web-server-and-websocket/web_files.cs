// Put your web app into /www (MicroCS Studio: Files panel, or File.WriteAllText)
Directory.CreateDirectory("/www");
File.WriteAllText("/www/index.html", "<!doctype html><link rel=stylesheet href=style.css><h1>Hello from flash</h1>");
File.WriteAllText("/www/style.css", "body { font-family: sans-serif; background: #eef; }");

Wifi.Connect("MicroCS-Lab", "microcs123");
var web = new HttpServer(80);
web.Get("/api/files", req => Directory.GetFiles("/www"));       // a route next to the files
web.ServeFiles("/", "/www");                    // GET /  -> /www/index.html, /style.css ...
web.Start();
Console.WriteLine($"open http://{Wifi.Ip}:{web.Port}/");
var until = Environment.TickCount + 2000;       // demo: 2 s; web.Run() on a board
while (Environment.TickCount < until) web.Poll(100);
