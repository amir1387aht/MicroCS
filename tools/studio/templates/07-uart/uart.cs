// UART 1 at 9600 baud (set its TX/RX pins in the firmware's pin table).
UART.Open(1, 9600);
UART.WriteLine(1, "AT");
string reply = UART.ReadLine(1, 2000);      // null after 2 s without a full line
Console.WriteLine(reply == null ? "no answer" : $"got: {reply}");
UART.Close(1);
