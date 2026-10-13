// No pairing: broadcast reaches every ESP-NOW board on the same channel
if (!Hal.Has("Json") || !Hal.Has("Wifi") || !Hal.Has("EspNow"))
{
    Console.WriteLine("This firmware lacks: Json, Wifi, EspNow. Enable the needed modules or use a supported board.");
    return;
}
EspNow.Start();
Console.WriteLine("my mac " + Wifi.Mac + ", channel " + Wifi.Channel);
EspNow.AddPeer("24:0A:C4:00:00:99");             // the simulator's echo board (optional)

for (int i = 0; i < 5; i++)
{
    string reading = Json.Stringify(new Dictionary<string, object> { ["n"] = i, ["temp"] = 20 + i * 0.5 });
    EspNow.Broadcast(reading);                   // to FF:FF:FF:FF:FF:FF
    EspNow.Send("24:0A:C4:00:00:99", reading);
    var m = EspNow.Receive(300);                 // EspNowMessage or null
    if (m != null) Console.WriteLine($"{m.Mac} ({m.Rssi} dBm): {m.Text}");
}
EspNow.Stop();
