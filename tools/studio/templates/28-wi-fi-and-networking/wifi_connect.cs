// Connect the station interface and show what DHCP gave us
if (!Net.Has("Wifi")) { Console.WriteLine("this board has no Wi-Fi"); return; }

const string Ssid = "MicroCS-Lab", Password = "microcs123";
Console.WriteLine($"connecting to {Ssid} ...");
if (!Wifi.Connect(Ssid, Password, 15000))
{
    Console.WriteLine($"failed: {Wifi.StatusName} ({Wifi.ReasonName()})");
    return;
}
Console.WriteLine($"connected  {Wifi.Ssid}  {Wifi.Rssi} dBm  channel {Wifi.Channel}  {Wifi.AuthName(Wifi.Auth)}");
Console.WriteLine($"ip {Wifi.Ip}  mask {Wifi.Netmask}  gateway {Wifi.Gateway}  dns {Wifi.Dns}");
Console.WriteLine($"mac {Wifi.Mac}  hostname {Wifi.Hostname}");
