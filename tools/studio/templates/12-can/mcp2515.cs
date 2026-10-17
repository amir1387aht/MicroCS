// External MCP2515 + CAN transceiver, not just a transceiver!
// Example Pico / RP2040 Zero wiring: SPI0 SCK=GP2 MOSI=GP3 MISO=GP4 CS=GP5.
// Adapt these pins for other boards and check 3.3 V-safe SPI levels.
// Read your module's crystal marking: 8, 16 or 20 MHz (not the MCU clock).
if (!Hal.Has("CAN.Mcp2515")) { Console.WriteLine("MCP2515 SPI/GPIO backend unavailable."); return; }
// STM32 release firmware uses SPI0=SPI2: PB13 SCK / PB14 MISO / PB15 MOSI.
// Its pins are fixed in firmware. CS below is GPIO 5 (PA5 on STM32); adapt it.
if (Hal.Has("SPI.Configure")) SPI.Open(0, new SpiConfig { Sck = 2, Mosi = 3, Miso = 4 });
else SPI.Open(0, 1000000, 0);
CAN.OpenMcp2515(0, 0, 5, 16000000, 500000, CAN.Normal);
try
{
    CAN.Send(0, 0x123, new byte[] { 0xAA, 0x55 });
    for (int i = 0; i < 20; i++)
    {
        var f = CAN.Receive(0, 50);
        if (f != null) Console.WriteLine($"rx {f}");
    }
}
finally { CAN.Close(0); }
