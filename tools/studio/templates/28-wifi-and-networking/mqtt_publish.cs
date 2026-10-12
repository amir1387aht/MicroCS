// MQTT CONNECT + PUBLISH (QoS 0) written by hand - about 30 lines of C#
byte[] Str(string s) { var b = Encoding.UTF8.GetBytes(s); var r = new byte[b.Length + 2]; r[0] = (byte)(b.Length >> 8); r[1] = (byte)b.Length; Array.Copy(b, 0, r, 2, b.Length); return r; }
byte[] Packet(int type, List<byte> body)
{
    var p = new List<byte> { (byte)type };
    int len = body.Count;
    do { int d = len % 128; len /= 128; p.Add((byte)(len > 0 ? d | 128 : d)); } while (len > 0);
    p.AddRange(body);
    return p.ToArray();
}

Wifi.Connect("MicroCS-Lab", "microcs123");
try
{
    var tcp = new TcpClient("test.mosquitto.org", 1883, 5000);
    var c = new List<byte>(Str("MQTT")) { 4, 2, 0, 60 };      // level 4, clean session, keep-alive 60 s
    c.AddRange(Str("microcs-" + Environment.TickCount));
    tcp.Write(Packet(0x10, c));
    var ack = tcp.Read(4);
    Console.WriteLine("CONNACK code " + (ack.Length == 4 ? ack[3] : -1));

    var pub = new List<byte>(Str("microcs/demo/temp"));
    pub.AddRange(Encoding.UTF8.GetBytes("21.5"));
    tcp.Write(Packet(0x30, pub));
    tcp.Write(new byte[] { 0xE0, 0 });                        // DISCONNECT
    tcp.Close();
    Console.WriteLine("published microcs/demo/temp = 21.5");
}
catch (IOException e) { Console.WriteLine("broker unreachable: " + e.Message); }
catch (TimeoutException) { Console.WriteLine("timed out"); }
