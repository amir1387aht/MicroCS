// Phone reads a text value; notifications are deliberately not enabled.
// 1. Scan in nRF Connect or LightBlue and connect to "MicroCS-Read".
// 2. Open service 19B10000-E8F2-537E-4F6C-D104768A1214.
// 3. Press Read on characteristic 19B10001-E8F2-537E-4F6C-D104768A1214.
// 4. View the bytes as UTF-8 text. Press Read again for the latest value.
// A Read is requested by the phone. SetValue changes what the next Read returns;
// it does not push data to the phone. No subscription or Notify() is needed.
if (!Hal.Has("Ble") || !Hal.Has("Ble.Server") || !Hal.Has("Ble.Advertise") || !Hal.Has("Encoding"))
{
    Console.WriteLine("This firmware needs: Ble, Ble.Server, Ble.Advertise, Encoding.");
    return;
}
Ble.Reset(); // discard previous script services / callbacks; preserve bonds
Ble.Name = "MicroCS-Read";
var service = new BleService("19B10000-E8F2-537E-4F6C-D104768A1214");
var value = service.AddCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214",
    BleProperty.Read, Encoding.UTF8.GetBytes("Ready"), 64);
Ble.AddService(service);
Ble.StartAdvertising();
Console.WriteLine("Connect to MicroCS-Read; press Read and view as UTF-8. Stop to exit.");
try
{
    int temperature = 25; // demo values, not a sensor measurement
    while (true)
    {
        Thread.Sleep(1000);
        value.SetValue(Encoding.UTF8.GetBytes("Temperature: " + temperature));
        // StringValue is a read-only local text getter, not an update method.
        Console.WriteLine("Next phone Read: " + value.StringValue);
        temperature = temperature == 25 ? 26 : 25;
    }
}
finally { Ble.Disable(); }
