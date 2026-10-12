// Nordic UART Service console: talk to the board from a phone with any
// "BLE UART" / "Serial Bluetooth Terminal" app. Commands: led on, led off, time.
var uart = new BleUart("MicroCS-UART");
bool led = false;
uart.OnReceive(text => {
    string cmd = text.Trim().ToLower();
    if (cmd == "led on") { led = true; uart.WriteLine("led is on"); }
    else if (cmd == "led off") { led = false; uart.WriteLine("led is off"); }
    else if (cmd == "time") uart.WriteLine("uptime " + Environment.TickCount + " ms");
    else uart.WriteLine("echo: " + text.Trim());
});
uart.Start();
Console.WriteLine("BLE UART advertising as MicroCS-UART on " + Ble.Address);
while (true) {
    Thread.Sleep(5000);
    if (uart.Connected) uart.WriteLine("alive, led " + (led ? "on" : "off"));
}
