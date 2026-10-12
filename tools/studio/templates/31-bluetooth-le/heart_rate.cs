// Heart-rate + battery peripheral using the standard Bluetooth SIG services,
// so generic apps (nRF Connect, LightBlue, fitness apps) understand it.
Ble.Name = "MicroCS-HR";
Ble.Appearance = 0x0341; // Heart Rate Sensor: Belt

var hr = new BleService("180D");
var measurement = hr.AddCharacteristic("2A37", BleProperty.Notify, new byte[] { 0, 60 });
var location = hr.AddCharacteristic("2A38", BleProperty.Read, new byte[] { 1 }); // chest

var battery = new BleService("180F");
var level = battery.AddCharacteristic("2A19", BleProperty.Read | BleProperty.Notify, new byte[] { 100 });

Ble.AddService(hr);
Ble.AddService(battery);
Ble.OnConnect(c => Console.WriteLine("connected: " + c.Address));
Ble.OnDisconnect((c, reason) => Console.WriteLine("disconnected: " + c.Address + " (reason " + reason + ")"));
measurement.OnSubscribe((c, flags) => Console.WriteLine(flags != 0 ? "heart rate notifications on" : "heart rate notifications off"));
Ble.StartAdvertising(); // advertises the name and both service UUIDs; re-advertises after a disconnect

int bpm = 60, pct = 100, tick = 0;
while (true) {
    Thread.Sleep(1000);
    bpm = bpm >= 90 ? 60 : bpm + 1;
    measurement.Notify(new byte[] { 0, (byte)bpm }); // flags 0: 8-bit bpm
    if (++tick % 30 == 0 && pct > 1) level.Notify(new byte[] { (byte)--pct });
}
