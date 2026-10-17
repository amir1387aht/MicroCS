// args: --sim --sim-virtual
Console.WriteLine($"can {Hal.Has("CAN")} configure={Hal.Has("CAN.Configure")} mcp={Hal.Has("CAN.Mcp2515")}");
var cfg = new CanConfig { Bitrate = 250000, Mode = CAN.Loopback };
Console.WriteLine($"config {cfg.Bitrate} tx={cfg.Tx} rx={cfg.Rx} mode={cfg.Mode}");
CAN.Open(0, cfg);
var source = new byte[] { 1, 2, 3 };
var tx = new CanFrame(0x7ff, source);
source[0] = 99;
CAN.Send(0, tx, 0);
var rx = CAN.Receive(0);
Console.WriteLine($"standard {rx} copy={rx.Data[0] == 1}");
CAN.Send(0, new CanFrame(0x1fffffff, new byte[8], true, true));
rx = CAN.Receive(0);
Console.WriteLine($"remote {rx} ext={rx.Extended} data={rx.Data.Length} dlc={rx.Length}");
Console.WriteLine($"empty {CAN.Receive(0, 0) == null}");
int received = 0;
CAN.OnReceive(0, (int pending) => { while (CAN.Receive(0) != null) received++; });
CAN.Send(0, 1, null); CAN.Send(0, 2, new byte[8]); Hal.Poll();
Console.WriteLine($"callbacks {received}");
CAN.Close(0); CAN.Close(0);
try { CAN.Receive(0); } catch (IOException e) { Console.WriteLine("closed"); }
CAN.Open(0, new CanConfig { Mode = CAN.ListenOnly });
try { CAN.Send(0, 1, null); } catch (NotSupportedException e) { Console.WriteLine("listen-only"); }
CAN.Close(0);
try { CAN.Receive(0, -1); } catch (ArgumentOutOfRangeException e) { Console.WriteLine("negative timeout"); }
try { new CanFrame(0x800, null); } catch (ArgumentOutOfRangeException e) { Console.WriteLine("bad id"); }
try { new CanFrame(1, new byte[9]); } catch (ArgumentOutOfRangeException e) { Console.WriteLine("long payload"); }
