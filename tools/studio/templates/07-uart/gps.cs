// GPS module on UART 1 at 9600 baud: print fix, satellites and position.
UART.Open(1, 9600);
double ToDeg(string v, string hemi)
{
    if (v.Length < 4) return 0;
    int dot = v.IndexOf('.');
    double deg = double.Parse(v.Substring(0, dot - 2)) + double.Parse(v.Substring(dot - 2)) / 60.0;
    return hemi == "S" || hemi == "W" ? -deg : deg;
}
for (int i = 0; i < 60; i++)
{
    string line = UART.ReadLine(1, 2000);
    if (line == null || !(line.StartsWith("$GPGGA") || line.StartsWith("$GNGGA"))) continue;
    var f = line.Split(',');
    if (f.Length < 8 || f[6] == "0") { Console.WriteLine("no fix yet"); continue; }
    Console.WriteLine($"{ToDeg(f[2], f[3]):F5}, {ToDeg(f[4], f[5]):F5}  sats {f[7]}");
}
