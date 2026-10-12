// Wait up to 1 s for a 16-byte packet, without blocking other code.
UART.Open(1, 115200);
var packet = new DmaBuffer(16);
var rx = UART.ReadAsync(1, packet);
UART.WriteAsync(1, "ping").Wait();
if (rx.Wait(1000)) Console.WriteLine("packet: " + packet.GetString());
else
{
    rx.Cancel();                              // stop waiting, free the buffer
    Console.WriteLine($"no full packet in 1 s (completed={rx.IsCompleted})");
}
