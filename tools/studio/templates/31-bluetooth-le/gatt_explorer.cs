// Central: discover services and descriptors
if (!Hal.Has("Ble") || !Hal.Has("Ble.Scan") || !Hal.Has("Ble.Central") || !Hal.Has("Ble.Read") || !Hal.Has("BitConverter"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Scan, Ble.Central, Ble.Read, BitConverter. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
foreach (var d in Ble.Scan(3000)) {
    if (!d.Connectable || !d.HasService("180D")) continue;
    var conn = d.Connect();
    try {
        foreach (var svc in conn.Services()) {
            Console.WriteLine("service " + svc.Uuid + " " + svc.Name);
            foreach (var chr in svc.Characteristics()) {
                Console.WriteLine("  " + chr.Uuid + " read=" + chr.CanRead + " notify=" + chr.CanNotify);
                if (chr.CanRead) Console.WriteLine("    " + BitConverter.ToString(chr.Read()));
                foreach (var desc in chr.Descriptors()) Console.WriteLine("    descriptor " + desc.Uuid);
            }
        }
    } finally { conn.Disconnect(); Ble.Disable(); }
    return;
}
Console.WriteLine("no heart-rate sensor found");
Ble.Disable();
