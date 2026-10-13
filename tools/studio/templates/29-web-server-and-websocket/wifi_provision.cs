// Both save the network (Wifi.SavedSsid) and connect; next boot Wifi.Connect() is enough
if (!Hal.Has("Wifi"))
{
    Console.WriteLine("This firmware lacks: Wifi. Enable the needed modules or use a supported board.");
    return;
}
if (Wifi.SavedSsid != "" && Wifi.Connect()) { Console.WriteLine("online: " + Wifi.Ssid); return; }

if (Net.Has("Wifi.Wps"))
{
    Console.WriteLine("press the WPS button on your router (2 minutes) ...");
    if (Wifi.Wps(120000)) { Console.WriteLine($"joined {Wifi.Ssid}, ip {Wifi.Ip}"); return; }
}
if (Net.Has("Wifi.SmartConfig"))
{
    Console.WriteLine("open the ESP-Touch app and send your network ...");
    if (Wifi.SmartConfig(120000)) { Console.WriteLine($"joined {Wifi.Ssid}, ip {Wifi.Ip}"); return; }
}
Console.WriteLine("no network yet - try the captive portal template");
