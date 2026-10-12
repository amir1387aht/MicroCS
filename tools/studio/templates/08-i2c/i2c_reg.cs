// Register access on a device at 0x68 (e.g. MPU-6050: WHO_AM_I = 0x75, PWR_MGMT_1 = 0x6B).
I2C.Open(0, 400000);
const int Addr = 0x68;
I2C.WriteRegister(0, Addr, 0x6B, 0x00);            // wake up
int who = I2C.ReadRegister(0, Addr, 0x75);
byte[] six = I2C.ReadRegisters(0, Addr, 0x3B, 6);   // 6 bytes from 0x3B
Console.WriteLine($"WHO_AM_I = 0x{who:X2}, data {BitConverter.ToString(six)}");
