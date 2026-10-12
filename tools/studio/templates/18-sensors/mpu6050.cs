// MPU-6050 / MPU-9250 (I2C address 0x68): acceleration in g, rotation in deg/s
I2C.Open(0, 400000);
var imu = new I2cDevice(0, 0x68);
imu.WriteRegister(0x6B, 0x00);                  // PWR_MGMT_1: wake up
Console.WriteLine($"WHO_AM_I = 0x{imu.ReadRegister(0x75):X2}");

short S16(byte[] b, int i) => (short)((b[i] << 8) | b[i + 1]);

for (int n = 0; n < 20; n++)
{
    byte[] r = imu.ReadRegisters(0x3B, 14);     // accel x,y,z, temp, gyro x,y,z
    double ax = S16(r, 0) / 16384.0, ay = S16(r, 2) / 16384.0, az = S16(r, 4) / 16384.0;
    double t = S16(r, 6) / 340.0 + 36.53;
    double gx = S16(r, 8) / 131.0, gy = S16(r, 10) / 131.0, gz = S16(r, 12) / 131.0;
    Console.WriteLine($"ax:{ax:F2} ay:{ay:F2} az:{az:F2} gx:{gx:F1} gy:{gy:F1} gz:{gz:F1} t:{t:F1}");
    Thread.Sleep(100);
}
