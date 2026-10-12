// Read 6 accelerometer bytes from an MPU-6050 (0x68) in the background.
I2C.Open(0, 400000);
var imu = new I2cDevice(0, 0x68);
imu.WriteRegister(0x6B, 0);
var regs = new DmaBuffer(6);
var t = imu.WriteReadAsync(new byte[] { 0x3B }, regs);
// ... other work here ...
t.Wait();
Console.WriteLine("accel raw: " + BitConverter.ToString(regs.ToArray()));
// a missing device: the NACK is reported by Wait / Succeeded / Error
var none = I2C.WriteAsync(0, 0x33, new byte[] { 1 });
try { none.Wait(); } catch (IOException e) { Console.WriteLine("0x33: " + e.Message); }
Console.WriteLine($"succeeded={none.Succeeded} error={none.Error}");
