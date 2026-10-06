// 06 · Register-style I2C device (MPU-6050 IMU): identify, configure, burst-read.
// Run: ./mcs --sim examples/hardware/06_i2c_imu_registers.cs
const int WhoAmI = 0x75, PowerMgmt = 0x6B, AccelX = 0x3B;

I2C.Open(0, 400000);
var imu = new I2cDevice(0, 0x68);

int id = imu.ReadRegister(WhoAmI);
Console.WriteLine($"WHO_AM_I = 0x{id:X2} {(id == 0x68 ? "(MPU-6050 found)" : "(unexpected)")}");

imu.WriteRegister(PowerMgmt, 0x00);           // wake up
byte[] raw = imu.ReadRegisters(AccelX, 6);    // X, Y, Z as big-endian int16
short Axis(int i) => (short)((raw[i] << 8) | raw[i + 1]);
Console.WriteLine($"accel x={Axis(0)} y={Axis(2)} z={Axis(4)}");
