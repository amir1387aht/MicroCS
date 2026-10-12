// 16x2 / 20x4 character LCD with a PCF8574 backpack at 0x27 (some are 0x3F)
I2C.Open(0, 100000);
var lcd = new I2cDevice(0, 0x27);
const int BL = 0x08, EN = 0x04, RS = 0x01;

void Pulse(int b) { lcd.Write(new byte[] { (byte)(b | EN | BL) }); lcd.Write(new byte[] { (byte)((b & ~EN) | BL) }); }
void Send(int value, int mode) { Pulse((value & 0xF0) | mode); Pulse(((value << 4) & 0xF0) | mode); }
void Cmd(int c) => Send(c, 0);
void Text(string s) { foreach (char ch in s) Send(ch, RS); }
void Goto(int col, int row) => Cmd(0x80 | (col + (row == 0 ? 0x00 : 0x40)));

Thread.Sleep(50);
Pulse(0x30); Thread.Sleep(5); Pulse(0x30); Pulse(0x30); Pulse(0x20);   // 4-bit mode
Cmd(0x28); Cmd(0x0C); Cmd(0x06); Cmd(0x01); Thread.Sleep(2);
Goto(0, 0); Text("Hello MicroCS!");
for (int i = 0; i < 5; i++) { Goto(0, 1); Text($"uptime {Environment.TickCount / 1000} s   "); Thread.Sleep(1000); }
