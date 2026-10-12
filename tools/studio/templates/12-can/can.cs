// CAN bus 0 at 500 kbit/s.
CAN.Open(0, 500000);
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
CAN.Send(0, new CanFrame(0x18FF50E5, new byte[] { 1, 2, 3 }, true));   // extended id
Hal.Run(500);
