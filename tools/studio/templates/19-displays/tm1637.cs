// TM1637 4-digit display: CLK and DIO on two GPIOs (bit-banged protocol)
const int CLK = 18, DIO = 19;
byte[] digits = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
GPIO.Mode(CLK, GPIO.Output); GPIO.Mode(DIO, GPIO.Output);

void Bit(int clk, int dio) { GPIO.Write(DIO, dio); GPIO.Write(CLK, clk); Hal.DelayMicroseconds(5); }
void Start() { Bit(1, 1); Bit(1, 0); Bit(0, 0); }
void Stop() { Bit(0, 0); Bit(1, 0); Bit(1, 1); }
void Byte(int b)
{
    for (int i = 0; i < 8; i++) { Bit(0, (b >> i) & 1); Bit(1, (b >> i) & 1); }
    Bit(0, 1); Bit(1, 1); Bit(0, 1);                 // ack clock
}
void Show(int value, bool colon)
{
    Start(); Byte(0x40); Stop();                     // auto-increment
    Start(); Byte(0xC0);
    for (int i = 3; i >= 0; i--)
    {
        int d = digits[(value / (int)Math.Pow(10, i)) % 10];
        Byte(i == 2 && colon ? d | 0x80 : d);
    }
    Stop();
    Start(); Byte(0x88 | 4); Stop();                 // display on, brightness 4
}

for (int s = 0; s < 10; s++) { Show(1200 + s, s % 2 == 0); Thread.Sleep(500); }
