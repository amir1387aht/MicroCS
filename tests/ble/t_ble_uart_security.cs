var uart = new BleUart("UartBoard");
uart.Start();
Thread.Sleep(10);
Console.WriteLine("connected " + uart.Connected + " avail " + uart.Available);
Console.WriteLine("got '" + uart.ReadString() + "'");
uart.WriteLine("ping");
Thread.Sleep(10);
Console.WriteLine("line '" + uart.ReadLine() + "'");
uart.OnReceive(text => Console.WriteLine("rx " + text.Trim()));
uart.Write("abc");
Thread.Sleep(10);
Ble.Disable();
Ble.ClearServices();
// central + security
Ble.IoCapability = Ble.IoDisplayYesNo;
Ble.Mitm = true;
Ble.OnPasskey((conn, kind, value) => { Console.WriteLine("passkey kind " + kind + " value " + value); return true; });
var t = Ble.Connect("C0:FF:EE:00:00:05");
var hum = t.Characteristic("181A", "2A6F");
try { hum.Read(); } catch (UnauthorizedAccessException e) { Console.WriteLine("denied: " + e.Message); }
Console.WriteLine("paired " + t.Pair() + " enc " + t.Encrypted + " auth " + t.Authenticated + " bonded " + t.Bonded);
Console.WriteLine("humidity " + BitConverter.ToUInt16(hum.Read(), 0));
var temp = t.Characteristic("181A", "2A6E");
foreach (var d in temp.Descriptors()) Console.WriteLine(d + " = " + (d.Uuid == "2901" ? Encoding.UTF8.GetString(d.Read()) : ""));
Console.WriteLine("bonds " + string.Join(",", Ble.Bonds));
Console.WriteLine("mtu " + t.RequestMtu() + " rssi " + t.Rssi);
var e2 = Ble.Connect("C0:FF:EE:00:00:02");
int got = 0;
e2.Characteristic(BleUuid.Normalize("6e400001-b5a3-f393-e0a9-e50e24dcca9e"), "6E400003-B5A3-F393-E0A9-E50E24DCCA9E").Subscribe(d => { got++; Console.WriteLine("echo " + Encoding.UTF8.GetString(d)); });
e2.Characteristic("6E400001-B5A3-F393-E0A9-E50E24DCCA9E", "6E400002-B5A3-F393-E0A9-E50E24DCCA9E").Write("hi there");
Ble.Poll();
Console.WriteLine("got " + got + " conns " + Ble.Connections.Count);
try { Ble.Connect("12:34:56:78:9A:BC", 500); } catch (TimeoutException e) { Console.WriteLine(e.Message); }
var ib = BleAdvertisement.IBeacon("E2C56DB5-DFFB-48D2-B060-D0F5A71096E0", 7, 9);
Console.WriteLine(BitConverter.ToString(ib.Build()));
var ed = BleAdvertisement.EddystoneUrl("https://www.example.org/x");
Console.WriteLine(BitConverter.ToString(ed.Build()));
Console.WriteLine(BleUuid.Name("180d") + " " + BleUuid.ToLong("180D") + " " + BleUuid.Equals("0000180D-0000-1000-8000-00805F9B34FB", 0x180D));
