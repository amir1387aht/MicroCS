// BLE beacon: alternates between an iBeacon and an Eddystone-URL frame.
// Non-connectable advertising only; phones see it in any beacon scanner app.
if (!Hal.Has("Ble") || !Hal.Has("Ble.Advertise"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Advertise. Enable the needed modules or use a supported board.");
    return;
}
var ibeacon = BleAdvertisement.IBeacon("E2C56DB5-DFFB-48D2-B060-D0F5A71096E0", 1, 42);
var eddystone = BleAdvertisement.EddystoneUrl("https://github.com"); // the encoded URL must fit 17 bytes
ibeacon.Interval = 200;
eddystone.Interval = 200;
Console.WriteLine("beacon on " + Ble.Address);
bool flip = false;
while (true) {
    Ble.StopAdvertising();
    Ble.StartAdvertising(flip ? eddystone : ibeacon);
    Console.WriteLine(flip ? "advertising Eddystone-URL" : "advertising iBeacon");
    flip = !flip;
    Thread.Sleep(2000);
}
