// Wifi.Save keeps SSID + password in NVS (ESP32) so no script has to contain them
if (Wifi.SavedSsid.Length == 0)
{
    Console.WriteLine("nothing saved yet, saving MicroCS-Lab");
    Wifi.Save("MicroCS-Lab", "microcs123");
}
Console.WriteLine("saved network: " + Wifi.SavedSsid);
Console.WriteLine("connect: " + Wifi.Connect() + "  ip " + Wifi.Ip);
// Wifi.Forget();   // erase the stored credentials
