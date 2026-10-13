// Mdns needs the espressif/mdns component on ESP32 (Net.Has("Mdns"))
if (!Hal.Has("Wifi") || !Hal.Has("Mdns") || !Hal.Has("Ping"))
{
    Console.WriteLine("This firmware lacks: Wifi, Mdns, Ping. Enable the needed modules or use a supported board.");
    return;
}
Wifi.Connect("MicroCS-Lab", "microcs123");
if (Net.Has("Mdns"))
{
    Mdns.Start("greenhouse");
    Mdns.AddService("_http", "_tcp", 80, new string[] { "path=/" });
    Console.WriteLine("I am greenhouse.local, printer.local is " + (Mdns.Resolve("printer.local") ?? "not found"));
}
if (Net.Has("Ping"))
{
    foreach (var host in new[] { Wifi.Gateway, "8.8.8.8" })
    {
        int ms = Ping.Send(host, 1000);
        Console.WriteLine($"ping {host}: {(ms < 0 ? "timeout" : ms + " ms")}");
    }
}
