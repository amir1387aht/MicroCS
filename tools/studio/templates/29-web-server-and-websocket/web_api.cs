// A small REST API: curl http://<ip>/api/notes, curl -d '{"text":"hi"}' http://<ip>/api/notes
Wifi.Connect("MicroCS-Lab", "microcs123");
var notes = new List<string> { "water the plants" };
var web = new HttpServer(80);
web.Cors = true;                                // browsers on other origins may call it

web.Get("/api/notes", req => notes);            // -> ["water the plants"]
web.Get("/api/notes/{id}", req =>
{
    int id = int.Parse(req.Param("id"));
    if (id < 0 || id >= notes.Count) { req.Send(404, "no such note"); return null; }
    return new Dictionary<string, object> { ["id"] = id, ["text"] = notes[id] };
});
web.Post("/api/notes", req =>
{
    // JSON body {"text": "..."} or a form field text=...
    string text = req.ContentType.StartsWith("application/json") ? (string)req.Json["text"] : req.Param("text", "");
    notes.Add(text);
    req.SendJson(new Dictionary<string, object> { ["id"] = notes.Count - 1 }, 201);
    return null;
});
web.Delete("/api/notes", req =>
{
    if (!req.Authenticate("admin", "secret")) return null;   // 401 + browser login box
    notes.Clear();
    return "cleared";
});
web.NotFound(req => req.Send(404, "text/plain", "nothing at " + req.Path));

web.Start();
Console.WriteLine($"API on http://{Wifi.Ip}:{web.Port}/api/notes");
var until = Environment.TickCount + 2000;       // demo: 2 s; web.Run() on a board
while (Environment.TickCount < until) web.Poll(100);
