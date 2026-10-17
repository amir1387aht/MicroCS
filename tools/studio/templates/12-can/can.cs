// Native CAN needs a transceiver and board-configured pins (see docs/CAN.md).
if (!Hal.Has("CAN")) { Console.WriteLine("No native CAN; use the MCP2515 template."); return; }
CAN.Open(0, 500000);
try
{
    CAN.OnReceive(0, (int pending) =>
    {
        CanFrame f;
        while ((f = CAN.Receive(0)) != null) Console.WriteLine($"rx {f}");
    });
    for (int i = 0; i < 5; i++)
    {
        CAN.Send(0, 0x123, new byte[] { (byte)i, 0xAB });
        Thread.Sleep(200);
    }
    CAN.Send(0, new CanFrame(0x18FF50E5, new byte[] { 1, 2, 3 }, true));
    Hal.Run(500);
}
finally { CAN.Close(0); }
