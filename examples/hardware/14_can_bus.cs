// 14 · CAN bus: send standard and extended frames, receive with a callback.
// Run: ./mcs --sim examples/hardware/14_can_bus.cs
// (the simulator's CAN controller is in loopback mode)
CAN.Open(0, 500_000);

// the callback gets the number of pending frames; drain them with CAN.Receive
CAN.OnReceive(0, (int pending) =>
{
    CanFrame f;
    while ((f = CAN.Receive(0)) != null)
        Console.WriteLine($"rx id=0x{f.Id:X} ext={f.Extended} data={BitConverter.ToString(f.Data)}");
});

CAN.Send(0, 0x123, new byte[] { 0x01, 0x02, 0x03 });             // 11-bit id
CAN.Send(0, new CanFrame(0x18FEF100, new byte[] { 0xAA, 0x55 }, true));   // 29-bit J1939-style id
Thread.Sleep(10);                                                // let the callbacks run
