// Start an access point (password: 8+ characters, empty = open network)
Wifi.OnEvent((ev, v) =>
{
    if (ev == Wifi.EventApJoined || ev == Wifi.EventApLeft || ev == Wifi.EventApAssigned)
        Console.WriteLine("ap: " + Wifi.EventName(ev));
});
Wifi.StartAp("MicroCS-Setup", "12345678", 6);
Console.WriteLine($"hotspot up at {Wifi.ApIp}  (mac {Wifi.ApMac})");

for (int i = 0; i < 3; i++)
{
    Thread.Sleep(300);
    Console.WriteLine($"{Wifi.ApClientCount} client(s)");
    foreach (var c in Wifi.ApClients()) Console.WriteLine("  " + c.Mac + "  " + c.Ip + "  " + c.Rssi + " dBm");
}
Wifi.StopAp();
