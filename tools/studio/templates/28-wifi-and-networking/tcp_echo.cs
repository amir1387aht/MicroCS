// Server and client in one script (loopback); on a board point the client at a PC
var server = new TcpListener("127.0.0.1", 0);   // port 0 = pick a free port
server.Start();

var client = new TcpClient("127.0.0.1", server.LocalPort, 3000);
client.NoDelay = true;
var peer = server.AcceptTcpClient(1000);
Console.WriteLine("accepted " + peer.RemoteAddress + ":" + peer.RemotePort);

foreach (var msg in new[] { "hello", "temp 21.5", "bye" })
{
    client.WriteLine(msg);
    string line = peer.ReadLine();
    peer.WriteLine(line.ToUpper());
    Console.WriteLine($"sent '{msg}'  echo '{client.ReadLine()}'");
}
peer.Close();
client.Close();
server.Stop();
