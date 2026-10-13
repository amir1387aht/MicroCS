// Side-effect-free option introspection; scan setting validation and reset lifecycle.
Console.WriteLine("API " + Ble.ApiVersion + " enabled " + Ble.Enabled);
Console.WriteLine("options " + Ble.OptionNames.Count + " mtu " + Ble.CanSetOption("Mtu") + " max " + Ble.CanSetOption("MaxConnections"));
Console.WriteLine("unknown " + Ble.CanReadOption("unknown") + " radio " + Ble.Enabled);
Console.WriteLine("roles " + Hal.Has("Ble.Read") + " " + Hal.Has("Ble.Subscribe") + " " + Hal.Has("Ble.ScanParameters"));
Ble.ScanInterval = 200; Ble.ScanWindow = 50;
Console.WriteLine("scan " + Ble.ScanInterval + "/" + Ble.ScanWindow + " radio " + Ble.Enabled);
try { Ble.ScanWindow = 201; } catch (ArgumentOutOfRangeException e) { Console.WriteLine("window rejected"); }
try { Ble.ScanInterval = 49; } catch (ArgumentOutOfRangeException e) { Console.WriteLine("interval rejected"); }
Ble.Passkey = 123456;
Console.WriteLine("passkey " + Ble.Passkey + " radio " + Ble.Enabled);
Ble.OnEvent((ev, conn, value) => { if (ev == Ble.EventConnect) { try { Ble.Reset(); } catch (InvalidOperationException e) { Console.WriteLine("callback reset rejected"); } } });
var svc = new BleService("FFF0"); svc.AddCharacteristic("FFF1", BleProperty.Read, "old"); Ble.AddService(svc);
Ble.StartAdvertising(); Thread.Sleep(40);
Ble.Reset();
Console.WriteLine("reset radio " + Ble.Enabled + " services " + Ble.Services.Count + " connections " + Ble.Connections.Count);
try { svc.AddCharacteristic("FFF2", BleProperty.Read, "stale"); } catch (InvalidOperationException e) { Console.WriteLine("old service invalidated"); }
Console.WriteLine("scan preferences kept " + Ble.ScanInterval + "/" + Ble.ScanWindow);
for (int i = 0; i < 12; i++) {
    var fresh = new BleService("FFF0"); fresh.AddCharacteristic("FFF1", BleProperty.Read, "fresh");
    Ble.AddService(fresh); Ble.StartAdvertising(); Ble.Reset();
}
Console.WriteLine("reruns clean " + (Ble.Services.Count == 0 && !Ble.Enabled));
