// API edge cases and errors
Console.WriteLine(BleUuid.IsValid("180D") + " " + BleUuid.IsValid("xyz") + " " + BleUuid.IsValid("0000180d-0000-1000-8000-00805f9b34fb"));
Console.WriteLine(BleUuid.Normalize("0000180d-0000-1000-8000-00805f9b34fb") + " " + BleUuid.Normalize(0x2a37));
try { BleUuid.Normalize("nope"); } catch (ArgumentException e) { Console.WriteLine("bad uuid: " + e.Message); }
var adv = new BleAdvertisement("A very long device name that cannot fit");
try { adv.Build(); Console.WriteLine("built " + adv.Build().Length + " rsp " + adv.BuildScanResponse().Length); } catch (Exception e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
adv.SetManufacturerData(0x0059, new byte[] { 1, 2, 3 });
Console.WriteLine(BitConverter.ToString(adv.Build()));
Console.WriteLine("enabled " + Ble.Enabled + " adv " + Ble.Advertising + " conns " + Ble.Connections.Count);
var s = new BleService("FFF0");
var c = s.AddCharacteristic("FFF1", BleProperty.Read | BleProperty.Write, 1234);
Console.WriteLine(BitConverter.ToString(c.Value) + " " + c.MaxLength + " " + c.Properties);
c.SetValue(70000); Console.WriteLine(BitConverter.ToString(c.Value));
c.SetValue("txt"); Console.WriteLine(c.StringValue);
try { c.Notify(); } catch (Exception e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
try { s.AddCharacteristic("FFF2", 0); } catch (Exception e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
Ble.AddService(s);
Console.WriteLine("services " + Ble.Services.Count + " enabled " + Ble.Enabled);
try { Ble.Connect("zz"); } catch (Exception e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
try { Ble.Connect("11:22:33:44:55:66", 300); } catch (Exception e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
Ble.TxPower = 4; Ble.Mtu = 100; Ble.Appearance = 0x0341;
Console.WriteLine(Ble.TxPower + " " + Ble.Mtu + " " + Ble.Appearance + " " + Ble.EventName(Ble.EventConnect) + " " + Ble.EventName(99));
var devs = Ble.Scan(100, true);
Console.WriteLine("scan " + devs.Count + " first " + devs[0].Address + " name " + devs[0].Name + " has180D " + devs[0].HasService("180d"));
var ib = devs.Find(d => d.IsIBeacon);
Console.WriteLine(ib.BeaconUuid + " " + ib.BeaconMajor + " " + ib.BeaconMinor + " " + ib.BeaconPower + " company " + ib.CompanyId);
int seen = 0;
Ble.StartScan(d => seen++);
Thread.Sleep(50);
Ble.StopScan();
Console.WriteLine("callback scan saw >= 5: " + (seen >= 5) + " scanning " + Ble.Scanning);
Ble.Disable();
Console.WriteLine("after disable " + Ble.Enabled);
