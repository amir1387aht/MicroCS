// BLE central: finds a heart-rate sensor, reads its battery level and prints
// heart-rate notifications. Reconnects if the sensor goes away.
if (!Hal.Has("Ble") || !Hal.Has("Ble.Scan") || !Hal.Has("Ble.Central") || !Hal.Has("Ble.Read") || !Hal.Has("Ble.Subscribe"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Scan, Ble.Central. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset();
while (true) {
    Console.WriteLine("scanning for a heart-rate sensor...");
    BleDevice sensor = null;
    foreach (var d in Ble.Scan(3000))
        if (d.Connectable && d.HasService("180D")) { sensor = d; break; }
    if (sensor == null) { Thread.Sleep(2000); continue; }

    Console.WriteLine("connecting to " + sensor);
    try {
        var conn = sensor.Connect();
        var battery = conn.Characteristic("180F", "2A19");
        if (battery != null) Console.WriteLine("battery " + battery.Read()[0] + "%");
        conn.Characteristic("180D", "2A37").Subscribe(data => {
            int bpm = (data[0] & 1) != 0 ? data[1] | data[2] << 8 : data[1];
            Console.WriteLine("heart rate " + bpm + " bpm");
        });
        while (conn.Connected) Thread.Sleep(500);
        Console.WriteLine("sensor disconnected");
    } catch (TimeoutException e) {
        Console.WriteLine("failed: " + e.Message);
    }
}
