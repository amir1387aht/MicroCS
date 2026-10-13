// Passive scan with duty-cycle settings
if (!Hal.Has("Ble") || !Hal.Has("Ble.Scan") || !Hal.Has("Ble.ScanParameters"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Scan, Ble.ScanParameters. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
Ble.ScanInterval = 200;
Ble.ScanWindow = 50;
int reports = 0;
Ble.StartScan(d => { reports++; Console.WriteLine(d.Address + " " + d.Name + " " + d.Rssi + " dBm"); }, false, true);
try { Thread.Sleep(5000); }
finally { Ble.StopScan(); Ble.Disable(); }
Console.WriteLine("reports " + reports);
