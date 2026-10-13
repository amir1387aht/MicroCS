// Central: connection timing and RSSI
if (!Hal.Has("Ble") || !Hal.Has("Ble.Scan") || !Hal.Has("Ble.Central") || !Hal.Has("Ble.Parameters") || !Hal.Has("Ble.Rssi"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Scan, Ble.Central, Ble.Parameters, Ble.Rssi. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
foreach (var d in Ble.Scan(3000)) {
    if (!d.Connectable || !d.HasService("180D")) continue;
    var conn = d.Connect();
    try {
        // interval units 1.25 ms; supervision timeout units 10 ms.
        conn.UpdateParameters(24, 40, 0, 400);
        Console.WriteLine("MTU " + conn.Mtu);
        for (int i = 0; i < 5 && conn.Connected; i++) {
            Console.WriteLine(conn.Address + " " + conn.Rssi + " dBm interval " + conn.Interval);
            Thread.Sleep(500);
        }
    } finally { conn.Disconnect(); Ble.Disable(); }
    return;
}
Console.WriteLine("no heart-rate sensor found");
Ble.Disable();
