// Read successive 50-byte blocks without loading the whole file into RAM.
if (!Hal.Has("Filesystem.Streams")) { Console.WriteLine("File streams need newer firmware."); return; }
const string path = "/chunks-demo.bin";
if (!File.Exists(path)) File.WriteAllText(path, new string('x', 150));
var file = File.OpenRead(path);
try
{
    byte[] buffer = new byte[50];
    int first = file.Read(buffer, 0, buffer.Length);
    int second = file.Read(buffer, 0, buffer.Length);
    Console.WriteLine($"first={first}, second={second}, position={file.Position}");
    file.Seek(50, SeekOrigin.Begin);
    int again = file.Read(buffer, 0, buffer.Length);
    Console.WriteLine($"second block again: {again} bytes");
}
finally { file.Dispose(); }
