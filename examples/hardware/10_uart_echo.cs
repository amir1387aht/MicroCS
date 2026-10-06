// 10 · UART: line-based command protocol with a receive callback.
// Run: ./mcs --sim examples/hardware/10_uart_echo.cs
// (the simulator's UART is a loopback, so what we send comes back)
const int Port = 1;
UART.Open(Port, 115200);                  // 8N1 by default; UART.Open(p, baud, 8, UART.ParityEven, 1)

UART.OnReceive(Port, (int count) => Console.WriteLine($"  [irq] {count} byte(s) waiting"));

UART.WriteLine(Port, "LED ON");
UART.WriteLine(Port, "TEMP?");

string line;
while ((line = UART.ReadLine(Port, 100)) != null)   // null after 100 ms of silence
{
    string reply = line switch
    {
        "LED ON" => "ok, LED on",
        "TEMP?" => "25.0",
        _ => "unknown command",
    };
    Console.WriteLine($"got '{line}' -> {reply}");
}
