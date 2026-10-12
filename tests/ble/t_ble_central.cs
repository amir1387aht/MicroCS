Console.WriteLine(Ble.Backend + " " + Ble.Has("Ble.Central"));
Console.WriteLine(Ble.Address + " " + Ble.Name);
foreach (var d in Ble.Scan(200)) {
    Console.WriteLine(d);
    Console.WriteLine("  services: " + string.Join(",", d.ServiceUuids) + " beacon=" + d.IsIBeacon + " url=" + d.EddystoneUrl);
}
var hrm = Ble.Connect("C0:FF:EE:00:00:01");
Console.WriteLine(hrm);
foreach (var s in hrm.Services()) {
    Console.WriteLine(s);
    foreach (var c in s.Characteristics()) Console.WriteLine("   " + c);
}
var lvl = hrm.Characteristic("180F", "2A19");
var b = lvl.Read()[0]; Console.WriteLine("battery ok " + (b > 50 && b <= 100));
// the simulated sensor's bpm follows the wall clock (70..79, +1 a second): check the sequence,
// not the values, so slow runs (sanitizers, busy CI machines) print the same
int n = 0, first = 0, second = 0;
hrm.Characteristic("180D", "2A37").Subscribe(data => { n++; if (n == 1) first = data[1]; else if (n == 2) second = data[1]; });
while (n < 2) Thread.Sleep(50);
Console.WriteLine("bpm in range " + (first >= 70 && first <= 79) + ", next " + (second == (first == 79 ? 70 : first + 1)));
hrm.Disconnect();
Console.WriteLine("connected " + Ble.Connected);
