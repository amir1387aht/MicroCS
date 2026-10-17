// Observe an isolated bench bus without transmitting or acknowledging.
if (!Hal.Has("CAN") || !Hal.Has("CAN.Configure")) { Console.WriteLine("Native configurable CAN unavailable."); return; }
// ESP32: add valid Tx/Rx pins here or configure firmware defaults.
// STM32/Zephyr: configure pins in CubeMX/devicetree instead.
CAN.Open(0, new CanConfig { Bitrate = 500000, Mode = CAN.ListenOnly });
try
{
    CAN.OnReceive(0, (int pending) =>
    {
        CanFrame f;
        while ((f = CAN.Receive(0)) != null) Console.WriteLine(f);
    });
    Hal.Run(2000);
}
finally { CAN.Close(0); }
