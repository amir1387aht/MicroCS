// Isolated native controller test. STM32 uses internal loopback;
// legacy ESP32 TWAI still needs TX fed to RX (transceiver/bench wiring).
if (!Hal.Has("CAN") || !Hal.Has("CAN.Configure")) { Console.WriteLine("Native configurable CAN unavailable."); return; }
// ESP32: add Tx/Rx to CanConfig below, or set firmware board defaults.
CAN.Open(0, new CanConfig { Bitrate = 500000, Mode = CAN.Loopback });
try
{
    CAN.Send(0, new CanFrame(0x18FF50E5, new byte[] { 1, 2, 3 }, true));
    var f = CAN.Receive(0, 100);
    Console.WriteLine(f == null ? "No echo: check loopback wiring." : f.ToString());
    // Remote request for four bytes; Data is empty on reception, Length is 4.
    CAN.Send(0, new CanFrame(0x123, new byte[4], false, true));
    f = CAN.Receive(0, 100);
    if (f != null) Console.WriteLine($"remote={f.Remote} DLC={f.Length}");
}
finally { CAN.Close(0); }
