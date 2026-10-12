// GPS / modem on UART 1: 9600 baud, TX 17, RX 16, a 1 KB receive buffer.
var cfg = new UartConfig(9600, 17, 16) { Parity = UART.ParityNone, RxBuffer = 1024 };
// cfg.Rs485 = true; cfg.Rts = 4;            // half duplex: RTS drives the transceiver DE pin
Console.WriteLine(cfg);
UART.Open(1, cfg);
UART.WriteLine(1, "$PMTK000*32");
string reply = UART.ReadLine(1, 500);
Console.WriteLine(reply ?? "(no reply)");
UART.Close(1);
