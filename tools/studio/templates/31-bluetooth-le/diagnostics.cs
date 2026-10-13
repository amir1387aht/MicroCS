// BLE capabilities and settings
if (!Hal.Has("Ble"))
{
    Console.WriteLine("This firmware lacks: Ble. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
Console.WriteLine("backend " + Ble.Backend + " API " + Ble.ApiVersion);
foreach (string option in Ble.OptionNames)
    Console.WriteLine(option + ": read=" + Ble.CanReadOption(option) + " write=" + Ble.CanSetOption(option));
Console.WriteLine("radio still off: " + !Ble.Enabled);
if (Ble.CanSetOption("Mtu")) Ble.Mtu = 185;
if (Ble.CanSetOption("TxPower")) Ble.TxPower = 0;
Ble.Timeout = 5000;
Ble.AutoAdvertise = true;
Console.WriteLine("scan interval " + Ble.ScanInterval + " window " + Ble.ScanWindow + " ms");
if (Ble.CanReadOption("Mtu")) Console.WriteLine("MTU " + Ble.Mtu);
Console.WriteLine("dropped events " + Ble.DroppedEvents);
Ble.Disable();
