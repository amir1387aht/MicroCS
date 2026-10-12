// UdpClient: Send(data, host, port) / Receive(timeoutMs) -> null on timeout
var listener = new UdpClient(0);              // bound to a free port
var sender = new UdpClient();
sender.Send("hello udp", "127.0.0.1", listener.LocalPort);
Console.WriteLine($"got '{listener.ReceiveString(500)}' from {listener.RemoteAddress}:{listener.RemotePort}");

sender.Send(new byte[] { 1, 2, 3, 4 }, "127.0.0.1", listener.LocalPort);
var data = listener.Receive(500);
Console.WriteLine("bytes: " + string.Join(",", data));
Console.WriteLine("nothing else: " + (listener.Receive(50) == null));

sender.EnableBroadcast = true;               // e.g. Send("who is there?", "255.255.255.255", 5000)
listener.Close();
sender.Close();
