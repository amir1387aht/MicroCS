// MAX7219 on SPI bus 0, CS = GPIO 5. One 8x8 matrix.
var max = new SpiDevice(0, 5, 1_000_000, 0);
void Reg(int reg, int value) => max.Write(new byte[] { (byte)reg, (byte)value });

Reg(0x0F, 0);      // display test off
Reg(0x0C, 1);      // normal operation
Reg(0x0B, 7);      // scan all 8 rows
Reg(0x09, 0);      // no BCD decoding (matrix)
Reg(0x0A, 4);      // brightness 0..15

byte[] heart = { 0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };
for (int frame = 0; frame < 6; frame++)
{
    for (int row = 0; row < 8; row++) Reg(row + 1, frame % 2 == 0 ? heart[row] : heart[row] & 0x3C);
    Thread.Sleep(300);
}
for (int row = 1; row <= 8; row++) Reg(row, 0);   // clear
