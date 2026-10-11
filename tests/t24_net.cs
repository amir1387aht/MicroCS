// args: --sim
// Wi-Fi, ESP-NOW, NTP, mDNS and Ping on the simulated radio; TCP / UDP over loopback.
Console.WriteLine(Net.Backend + " api " + Net.ApiVersion + " wifi=" + Net.Has("Wifi") + " ap=" + Hal.Has("Wifi.AP") + " tls=" + Net.Has("Tls"));
var events = new List<string>();
Wifi.OnEvent((ev, v) => events.Add(Wifi.EventName(ev)));
Console.WriteLine("mode " + Wifi.Mode + " status " + Wifi.StatusName);

// ---- scan
var nets = Wifi.Scan();
foreach (var n in nets) Console.WriteLine("  " + n + " q=" + n.Quality + " secure=" + n.Secure);
Console.WriteLine("hidden too: " + Wifi.Scan(true).Length);

// ---- connect: wrong password, unknown SSID, success
Console.WriteLine(Wifi.Connect("MicroCS-Lab", "wrongpass1") + " " + Wifi.StatusName + " " + Wifi.ReasonName());
Console.WriteLine(Wifi.Connect("NoSuchNet", "whatever12") + " " + Wifi.StatusName + " " + Wifi.ReasonName(Wifi.DisconnectReason));
try { Wifi.Connect("MicroCS-Lab", "abc"); } catch (ArgumentException e) { Console.WriteLine("ArgumentException " + e.Message); }
Console.WriteLine(Wifi.Connect("MicroCS-Lab", "microcs123") + " " + Wifi.IsConnected + " " + Wifi.Status);
Console.WriteLine(Wifi.Ssid + " " + Wifi.Bssid + " " + Wifi.Rssi + " dBm ch " + Wifi.Channel + " " + Wifi.AuthName(Wifi.Auth));
Console.WriteLine(Wifi.Ip + " / " + Wifi.Netmask + " gw " + Wifi.Gateway + " dns " + Wifi.Dns + " " + Wifi.Dns2 + " dhcp " + Wifi.Dhcp);
Console.WriteLine("mac " + Wifi.Mac + " ap mac " + Wifi.ApMac + " ipv6 " + Wifi.Ipv6);

// ---- options
Wifi.Hostname = "greenhouse"; Wifi.Country = "DE";
Wifi.TxPower = 13; Wifi.PowerSave = Wifi.PowerMax; Wifi.Bandwidth = 40; Wifi.Protocol = Wifi.Proto11b | Wifi.Proto11g | Wifi.Proto11n;
Console.WriteLine(Wifi.Hostname + " " + Wifi.Country + " tx " + Wifi.TxPower + " ps " + Wifi.PowerSave + " bw " + Wifi.Bandwidth + " proto " + Wifi.Protocol);
try { Wifi.Band = Wifi.Band5G; } catch (NotSupportedException) { Console.WriteLine("no 5 GHz"); }
try { Wifi.TxPower = 50; } catch (ArgumentException) { Console.WriteLine("tx power out of range"); }
Wifi.AutoReconnect = false; Console.WriteLine("auto " + Wifi.AutoReconnect);

// ---- static IP and back
Wifi.SetStaticIp("192.168.1.77", "192.168.1.1", "255.255.255.0", "1.1.1.1");
Console.WriteLine("static " + Wifi.Ip + " dns " + Wifi.Dns + " dhcp " + Wifi.Dhcp);
Wifi.UseDhcp(); Console.WriteLine("dhcp " + Wifi.Ip);

// ---- saved credentials
Wifi.Save("CoffeeShop", "");
Console.WriteLine("saved " + Wifi.SavedSsid);
Wifi.Disconnect();
Console.WriteLine(Wifi.Connect() + " " + Wifi.Ssid + " " + Wifi.AuthName(Wifi.Auth));
Wifi.Forget(); Console.WriteLine("saved '" + Wifi.SavedSsid + "'");
Console.WriteLine(Wifi.Connect("MicroCS-Lab", "microcs123"));

// ---- soft access point
Wifi.StartAp("MicroCS-Setup", "12345678", 6);
Console.WriteLine("mode " + (Wifi.Mode == Wifi.ModeStationAp) + " ap " + Wifi.ApIp + " clients " + Wifi.ApClientCount);
foreach (var c in Wifi.ApClients()) Console.WriteLine("  " + c);
Wifi.StopAp();

// ---- ESP-NOW
EspNow.Start();
EspNow.AddPeer("24:0A:C4:00:00:99");
Console.WriteLine("peers " + EspNow.Peers.Length + " max " + EspNow.MaxData);
Console.WriteLine("sent " + EspNow.Send("24:0A:C4:00:00:99", "ping"));
var m = EspNow.Receive(100);
Console.WriteLine(m.Mac + " '" + m.Text + "' " + m.Length + " rssi " + m.Rssi);
try { EspNow.Send("11:22:33:44:55:66", "x"); } catch (ArgumentException e) { Console.WriteLine("not a peer: " + e.GetType().Name); }
var got = 0;
EspNow.OnReceive((mac, data) => { got += data.Length; });
EspNow.Broadcast(new byte[] { 1, 2, 3 });
Thread.Sleep(5);
Console.WriteLine("callback bytes " + got);
EspNow.Stop();

// ---- NTP, mDNS, ping
Console.WriteLine("ntp " + Ntp.Sync() + " synced " + Ntp.Synced + " sane " + (Ntp.Time > 1700000000));
Ntp.Timezone = "UTC0"; Console.WriteLine("tz " + Ntp.Timezone + " offset " + Ntp.UtcOffset + " len " + Ntp.LocalTime.Length);
Mdns.Start("greenhouse"); Mdns.AddService("_http", "_tcp", 80, new string[] { "path=/" });
Console.WriteLine("mdns " + Mdns.Resolve("greenhouse.local") + " " + Mdns.Resolve("printer.local") + " " + (Mdns.Resolve("nobody.local") == null));
Console.WriteLine("ping gw " + Ping.Send(Wifi.Gateway) + " ms, 10.9.9.9 " + Ping.Send("10.9.9.9", 50));

Thread.Sleep(5);
Console.WriteLine(string.Join(",", events));
Wifi.Off();
Console.WriteLine("off: mode " + Wifi.Mode + " " + Wifi.StatusName);

// ---- TCP over loopback
var server = new TcpListener("127.0.0.1", 0);
server.Start();
var client = new TcpClient("127.0.0.1", server.LocalPort);
Console.WriteLine("pending " + server.Pending());
var peer = server.AcceptTcpClient(1000);
client.NoDelay = true;
client.WriteLine("GET /status");
client.Write(new byte[] { 65, 66, 10 });
Console.WriteLine("server got '" + peer.ReadLine() + "' '" + peer.ReadLine() + "' from " + peer.RemoteAddress);
peer.Write("HTTP/1.0 200 OK\r\n\r\nhello");
peer.Close();
Console.WriteLine("client: " + client.ReadLine() + " | " + client.ReadLine() + " | " + client.ReadToEnd());
Console.WriteLine("closed: " + (client.ReadLine() == null) + " " + client.Read().Length + " " + client.ReadByte());
client.Close();
Console.WriteLine("accept timeout null: " + (server.AcceptTcpClient(30) == null));
client = new TcpClient("127.0.0.1", server.LocalPort);
peer = server.AcceptTcpClient(1000);
client.Timeout = 40;
try { client.Read(); } catch (TimeoutException) { Console.WriteLine("read timeout"); }
client.Close(); peer.Close();
server.Stop();
try { new TcpClient("127.0.0.1", server.LocalPort, 500); } catch (SocketException e) { Console.WriteLine("refused, IOException: " + (e is IOException)); }
try { new TcpClient().Write("x"); } catch (InvalidOperationException) { Console.WriteLine("not connected"); }

// ---- UDP over loopback
var a = new UdpClient(0);
var b = new UdpClient();
b.Send("datagram", "127.0.0.1", a.LocalPort);
Console.WriteLine("udp '" + a.ReceiveString(500) + "' from " + a.RemoteAddress + " back to sender: " + (a.RemotePort == b.LocalPort));
b.Connect("127.0.0.1", a.LocalPort);
b.Send(new byte[] { 7, 8, 9 });
var d = a.Receive(500);
Console.WriteLine("udp bytes " + d.Length + " sum " + (d[0] + d[1] + d[2]) + ", nothing more: " + (a.Receive(20) == null));
a.Close(); b.Close();

// ---- Dns, Http helpers
Console.WriteLine("dns " + Dns.Resolve("localhost") + " " + Dns.Resolve("10.1.2.3") + " " + Dns.GetHostAddresses("127.0.0.1")[0]);
Console.WriteLine(Http.UrlEncode("a b&c=d/x") + " timeout " + Http.Timeout + " redirects " + Http.FollowRedirects);
try { Http.Get("ftp://example.com/"); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
