// Device information and custom descriptors
if (!Hal.Has("Ble") || !Hal.Has("Ble.Server") || !Hal.Has("Ble.Advertise"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Server, Ble.Advertise. Enable the needed modules or use a supported board.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
Ble.Name = "MicroCS-Info";
var info = new BleService("180A");
info.AddCharacteristic("2A29", BleProperty.Read, "MicroCS");
info.AddCharacteristic("2A24", BleProperty.Read, Hal.Board);
Ble.AddService(info);
var service = new BleService("FFF0");
var value = service.AddCharacteristic("FFF1", BleProperty.Read | BleProperty.Write | BleProperty.Notify, "ready", 64);
value.AddDescriptor("2901", "Demo value");
value.OnWrite((conn, data) => Console.WriteLine("new value: " + value.StringValue));
Ble.AddService(service);
Ble.StartAdvertising();
Console.WriteLine("open MicroCS-Info in nRF Connect; Stop to exit");
try { while (true) Thread.Sleep(250); }
finally { Ble.Disable(); }
