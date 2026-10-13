// WebSocket client: ws://host[:port]/path, wss:// where the board has TLS (Net.Has("Tls"))
if (!Hal.Has("Wifi"))
{
    Console.WriteLine("This firmware lacks: Wifi. Enable the needed modules or use a supported board.");
    return;
}
Wifi.Connect("MicroCS-Lab", "microcs123");
try
{
    var ws = new WebSocket();
    ws.Connect("ws://192.168.1.10:8765/chat", 5000);    // your server (python -m websockets ...)
    ws.OnMessage(m => Console.WriteLine("got: " + m));    // string, or byte[] for binary frames
    ws.Send("hello from " + Hal.Board);
    ws.Send(new byte[] { 1, 2, 3 });
    for (int i = 0; i < 10 && ws.IsOpen; i++) ws.Poll(200);   // runs OnMessage, answers pings
    ws.Close(1000, "bye");
}
catch (IOException e) { Console.WriteLine("WebSocket: " + e.Message); }
