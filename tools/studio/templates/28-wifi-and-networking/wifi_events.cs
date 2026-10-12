// Wi-Fi events arrive on the script thread, like GPIO interrupts
Wifi.OnEvent((ev, value) =>
{
    Console.WriteLine($"[{Environment.TickCount,6} ms] {Wifi.EventName(ev)}" +
        (ev == Wifi.EventDisconnected ? $"  reason {Wifi.ReasonName(value)}" : ""));
    if (ev == Wifi.EventGotIp) Console.WriteLine($"           ip {Wifi.Ip}");
});

Wifi.AutoReconnect = true;            // retry after the link drops
Wifi.Begin("MicroCS-Lab", "microcs123");   // returns at once; events tell the story
Console.WriteLine("connected: " + Wifi.WaitConnected(15000));

for (int i = 0; i < 3; i++)
{
    Console.WriteLine($"status {Wifi.StatusName}  rssi {Wifi.Rssi} dBm");
    Thread.Sleep(500);
}
Wifi.Disconnect();
Thread.Sleep(50);
