// Software loopback with --sim. A real board must supply an actual CAN FD backend.
if (!Hal.Has("CAN.FD") || !CANFD.IsSupported(0)) {
    Console.WriteLine("CAN FD is unavailable on bus 0.");
} else {
    CANFD.Open(0, 500000, 2000000);
    try {
        var data = new byte[64];
        for (int i = 0; i < data.Length; i++) data[i] = (byte)i;
        var frame = new CanFdFrame(0x123, data, false, true);
        CANFD.Send(0, frame);
        var received = CANFD.Receive(0);
        if (received != null) {
            Console.WriteLine($"CAN FD bytes={received.Length} dlc={received.Dlc} brs={received.BitRateSwitch}");
            Console.WriteLine(received.Data[63]);
        }
    } finally {
        CANFD.Close(0);
    }
}
