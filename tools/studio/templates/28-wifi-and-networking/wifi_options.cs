// Set options before (or after) connecting
if (!Hal.Has("Wifi"))
{
    Console.WriteLine("This firmware lacks: Wifi. Enable the needed modules or use a supported board.");
    return;
}
Wifi.Hostname = "greenhouse";      // shown in the router's client list / DHCP
Wifi.Country = "DE";               // allowed channels and power
Wifi.TxPower = 15;                 // dBm (2..20)
Wifi.PowerSave = Wifi.PowerMin;    // PowerNone = lowest latency, PowerMax = lowest current
Wifi.Protocol = Wifi.Proto11b | Wifi.Proto11g | Wifi.Proto11n;

Wifi.Connect("MicroCS-Lab", "microcs123");
Wifi.SetStaticIp("192.168.1.77", "192.168.1.1", "255.255.255.0", "1.1.1.1");
Console.WriteLine($"{Wifi.Hostname} static {Wifi.Ip} gw {Wifi.Gateway} dns {Wifi.Dns} dhcp={Wifi.Dhcp}");

Wifi.UseDhcp();                    // back to automatic addressing
Console.WriteLine($"dhcp {Wifi.Ip}  tx {Wifi.TxPower} dBm  ps {Wifi.PowerSave}  bw {Wifi.Bandwidth} MHz");
