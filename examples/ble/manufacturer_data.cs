// Manufacturer and service-data advertising
if (!Hal.Has("Ble") || !Hal.Has("Ble.Advertise"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Advertise. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
var adv = new BleAdvertisement("MicroCS-Data");
adv.Connectable = false;
adv.Interval = 250;
// 0xFFFF is a demo ID, not a registered company assignment.
adv.SetManufacturerData(0xFFFF, new byte[] { 1, 2, 3 });
adv.AddServiceData("180F", new byte[] { 87 });
Ble.StartAdvertising(adv);
Console.WriteLine("advertising demo data; Stop ends the script");
try { while (true) Thread.Sleep(250); }
finally { Ble.Disable(); }
