// args: --sim
// Peripheral API against the simulator board
Console.WriteLine("board " + Hal.Board + " gpio=" + Hal.Has("GPIO") + " can=" + Hal.Has("CAN"));
const int Led = 13, Button = 2;
GPIO.Mode(Led, GPIO.Output);
GPIO.Mode(Button, GPIO.InputPullUp);
GPIO.Write(Led, true);
Console.WriteLine("led " + GPIO.Read(Led) + " button " + GPIO.Read(Button));
GPIO.Toggle(Led);
Console.WriteLine("led " + (GPIO.Read(Led) == GPIO.Low));

UART.Open(1, 115200);
UART.Write(1, "ping");
Console.WriteLine("uart avail " + UART.Available(1) + " -> " + UART.ReadString(1, 16));
UART.Write(1, new byte[] { 0x41, 0x42 });
byte[] rx = UART.Read(1, 8);
Console.WriteLine("uart bytes " + rx.Length + " " + rx[0] + " " + rx[1]);

// TMP102-style sensor: 12-bit, 0.0625 C per LSB
byte[] t = I2C.WriteRead(0, 0x48, new byte[] { 0 }, 2);
int rawT = (t[0] << 4) | (t[1] >> 4);
Console.WriteLine($"temp {rawT * 0.0625:F2} C");
I2C.Write(0, 0x50, new byte[] { 0x10, 7, 8, 9 });
byte[] ee = I2C.WriteRead(0, 0x50, new byte[] { 0x10 }, 3);
Console.WriteLine("eeprom " + ee[0] + ee[1] + ee[2]);
try { I2C.Read(0, 0x33, 1); }
catch (IOException e) { Console.WriteLine("nack: " + e.Message); }

byte[] spi = SPI.Transfer(0, new byte[] { 1, 2, 3 });
Console.WriteLine("spi " + string.Join(",", spi));
Console.WriteLine("adc " + ADC.Read(3) + " bits " + ADC.Resolution);
PWM.Set(0, 1000, 0.25);
PWM.SetPermille(1, 50, 75);
try { PWM.Set(0, 1000, 1.5); }
catch (ArgumentOutOfRangeException) { Console.WriteLine("duty rejected"); }
try { GPIO.Write(99, true); }
catch (NotSupportedException e) { Console.WriteLine(e.Message); }
