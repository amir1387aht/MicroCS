// Echo everything that arrives on UART 1 back to the sender (and print it).
UART.Open(1, 115200);
UART.OnReceive(1, (int available) =>
{
    string s = UART.ReadString(1, available);
    Console.Write(s);
    UART.Write(1, s);
});
Console.WriteLine("echoing UART 1 (Stop ends the script)");
while (true) Thread.Sleep(100);
