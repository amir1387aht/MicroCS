// ESP-NOW: up to EspNow.MaxData bytes per packet, ~1 ms latency, no access point needed
EspNow.Start();
Console.WriteLine("my mac: " + Wifi.Mac + "   max payload " + EspNow.MaxData);

const string Peer = "24:0A:C4:00:00:99";       // the other board's MAC (simulator echoes)
EspNow.AddPeer(Peer);
EspNow.OnReceive((mac, data) =>
    Console.WriteLine($"from {mac}: {Encoding.UTF8.GetString(data)}"));

for (int i = 1; i <= 3; i++)
{
    bool ok = EspNow.Send(Peer, $"reading {i}: {20 + i}.5 C");
    Console.WriteLine($"send {i}: {(ok ? "delivered" : "no ack")}");
    Thread.Sleep(100);
}
EspNow.Broadcast("hello everyone");
Thread.Sleep(50);
EspNow.Stop();
