// Scan and print a small table (pass true to include hidden networks)
if (!Hal.Has("Wifi") || !Hal.Has("Wifi.Scan") || !Hal.Has("ArrayExtra"))
{
    Console.WriteLine("This firmware lacks: Wifi, Wifi.Scan, ArrayExtra. Enable the needed modules or use a supported board.");
    return;
}
var nets = Wifi.Scan(true);
Array.Sort(nets, (a, b) => b.Rssi - a.Rssi);
Console.WriteLine($"{nets.Length} networks");
Console.WriteLine("SSID                 RSSI  CH  QUAL  SECURITY");
foreach (var n in nets)
{
    string name = n.Ssid.Length == 0 ? "(hidden)" : n.Ssid;
    Console.WriteLine($"{name,-20} {n.Rssi,4}  {n.Channel,2}  {n.Quality,3}%  {Wifi.AuthName(n.Auth)}");
}
