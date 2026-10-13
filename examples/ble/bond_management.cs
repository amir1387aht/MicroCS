// Inspect stored BLE bonds
if (!Hal.Has("Ble") || !Hal.Has("Ble.Bonds"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Bonds. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
const bool FORGET_ALL = false;
foreach (string address in Ble.Bonds) Console.WriteLine("bonded peer " + address);
if (FORGET_ALL) { Ble.ClearBonds(); Console.WriteLine("all bonds erased"); }
else Console.WriteLine("bonds preserved (FORGET_ALL=false)");
Ble.Disable();
