// Your own GATT service: a phone app (nRF Connect, LightBlue) switches the LED and reads
// a counter. Writing 01 / 00 to characteristic ...0002 turns the LED on / off.
var led = new Pin("LED", GPIO.Output);
var svc = new BleService("19B10000-E8F2-537E-4F6C-D104768A1214");
var sw = svc.AddCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BleProperty.Read | BleProperty.Write, new byte[] { 0 });
var count = svc.AddCharacteristic("19B10002-E8F2-537E-4F6C-D104768A1214", BleProperty.Read | BleProperty.Notify, new byte[] { 0 });
sw.Description = "LED (01 = on)";
sw.OnWrite((data, conn) => {
    bool on = data.Length > 0 && data[0] != 0;
    led.Write(on);
    Console.WriteLine($"{conn.Address} turned the LED {(on ? "on" : "off")}");
});
Ble.Name = "MicroCS-LED";
Ble.AddService(svc);
Ble.StartAdvertising();
Console.WriteLine("advertising as " + Ble.Name + " (" + Ble.Address + ")");
int n = 0;
while (true) {
    Thread.Sleep(1000);
    count.Notify(new byte[] { (byte)++n });
}
