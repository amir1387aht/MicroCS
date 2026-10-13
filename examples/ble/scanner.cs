// BLE scanner: lists nearby devices every 5 seconds, strongest first.
// Run on the host simulator: build/mcs_ble --sim examples/ble/scanner.cs
// On a board: enable BLE for your port (see docs/BLE.md) and flash as usual.
if (!Hal.Has("Ble") || !Hal.Has("Ble.Scan"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Scan. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset();
Console.WriteLine("BLE scanner on " + Ble.Backend + ", our address " + Ble.Address);
while (true) {
    var devices = Ble.Scan(2000, true);
    Console.WriteLine("--- " + devices.Count + " device(s)");
    foreach (var d in devices) {
        string extra = "";
        if (d.IsIBeacon) extra = " iBeacon " + d.BeaconUuid + " " + d.BeaconMajor + "/" + d.BeaconMinor;
        else if (d.EddystoneUrl != null) extra = " Eddystone " + d.EddystoneUrl;
        else if (d.ServiceUuids.Count > 0) extra = " services " + string.Join(",", d.ServiceUuids);
        if (d.CompanyId >= 0) extra += " company 0x" + d.CompanyId.ToString("X4");
        Console.WriteLine(d.Address + "  " + d.Rssi + " dBm  " + (d.Name ?? "<no name>") + extra);
    }
    Thread.Sleep(3000);
}
