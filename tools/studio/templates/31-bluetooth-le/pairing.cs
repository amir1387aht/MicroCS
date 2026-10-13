// Secure link: a characteristic that can only be read after pairing with a passkey.
// The board shows the 6-digit key on the console; type it on the phone.
if (!Hal.Has("Ble") || !Hal.Has("Ble.Server") || !Hal.Has("Ble.Advertise") || !Hal.Has("Ble.Security") || !Hal.Has("Ble.Bonds"))
{
    Console.WriteLine("This firmware lacks: Ble, Ble.Server, Ble.Advertise, Ble.Security, Ble.Bonds. Enable the needed modules or use a supported board.");
    return;
}
Ble.IoCapability = Ble.IoDisplayOnly;
Ble.Bonding = true;
Ble.Mitm = true;
Ble.OnPasskey((conn, kind, key) => {
    if (kind == Ble.PasskeyDisplay) Console.WriteLine("passkey for " + conn.Address + ": " + key);
    return 1;
});
Ble.OnEvent((ev, conn, value) => Console.WriteLine("event " + Ble.EventName(ev) + " " + value));
var svc = new BleService("FFF0");
svc.AddCharacteristic("FFF1", BleProperty.Read | BleProperty.Authenticated, "secret value");
Ble.AddService(svc);
Ble.StartAdvertising("MicroCS-Secure");
foreach (var a in Ble.Bonds) Console.WriteLine("bonded: " + a);
while (true) Thread.Sleep(1000);
