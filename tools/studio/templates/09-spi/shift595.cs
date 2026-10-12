// 74HC595: MOSI -> DS, SCK -> SH_CP, GPIO 10 -> ST_CP (latch).
var sr = new SpiDevice(0, 10, 1000000, 0);
for (int i = 0; i < 16; i++)
{
    sr.Write(new byte[] { (byte)(1 << (i % 8)) });
    Thread.Sleep(100);
}
sr.Write(new byte[] { 0 });
