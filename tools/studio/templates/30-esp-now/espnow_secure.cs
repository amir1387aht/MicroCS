// Encrypted ESP-NOW: both boards set the same primary key and the peer's local key
if (!Hal.Has("GPIO") || !Hal.Has("EspNow"))
{
    Console.WriteLine("This firmware lacks: GPIO, EspNow. Enable the needed modules or use a supported board.");
    return;
}
EspNow.Start();
EspNow.SetPmk("pmk-1234567890ab");               // 16 characters, same on every board
const string Peer = "24:0A:C4:00:00:99";
EspNow.AddPeer(Peer, 0, "lmk-abcdef123456");    // channel 0 = current, 16-character key = encrypted
var led = new Pin(2, GPIO.Output);

EspNow.OnReceive((mac, data) =>
{
    string cmd = Encoding.UTF8.GetString(data);
    if (cmd == "toggle") led.Toggle();
    Console.WriteLine($"{mac}: {cmd}  (led {led.Read()})");
});
for (int i = 0; i < 3; i++)
{
    bool ok = EspNow.Send(Peer, "toggle");      // true = the peer's radio acknowledged it
    Console.WriteLine("sent toggle: " + (ok ? "acked" : "no ack"));
    Thread.Sleep(200);
}
Console.WriteLine("peers: " + string.Join(", ", EspNow.Peers));
EspNow.Stop();
