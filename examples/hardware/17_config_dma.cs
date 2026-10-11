// 17 · Peripheral settings + DMA: an SPI display frame sent in the background
// while a button (pull-up, hysteresis) and an I2C sensor on chosen pins are read.
// Run: ./mcs --sim --sim-virtual examples/hardware/17_config_dma.cs
Console.WriteLine($"{Hal.Board}: SPI.Configure={Hal.Has("SPI.Configure")} DMA.SPI={Hal.Has("DMA.SPI")}");

// pins and electrical settings in one place (unset = board default)
var button = new Pin(9, new GpioConfig(GPIO.Input, GPIO.PullUp) { Schmitt = true });
var led = new Pin("LED", new GpioConfig(GPIO.Output) { Initial = 0, Speed = GPIO.SpeedLow });
I2C.Open(0, new I2cConfig(400_000, 21, 22) { TimeoutMs = 20 });
SPI.Open(0, new SpiConfig(20_000_000, 0, 18, 23) { Cs = 5 });
var sensor = new I2cDevice(0, 0x48);

// two frame buffers: draw one while the other is sent
var frames = new[] { new DmaBuffer(1024), new DmaBuffer(1024) };
DmaTransfer sending = null;
for (int f = 0; f < 8; f++)
{
    var frame = frames[f % 2];
    frame.Fill(0);
    frame[f] = 0xFF;                                   // "draw"
    byte[] r = sensor.ReadRegisters(0, 2);              // runs while the last frame goes out
    double c = ((r[0] << 4) | (r[1] >> 4)) * 0.0625;
    led.Value = !button.Value;                          // pressed = low
    if (sending != null) sending.Wait();
    sending = SPI.WriteAsync(0, frame);
    Console.WriteLine($"frame {f}: {c:F2} C");
}
sending.OnComplete(n => Console.WriteLine($"last frame: {n} bytes"));
Dma.WaitAll();
Hal.Poll();
