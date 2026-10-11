// args: --sim --sim-virtual
// HAL v3: peripheral configuration objects (pins, pulls, drive, filters, DMA, board options)
Console.WriteLine($"configure gpio={Hal.Has("GPIO.Configure")} spi={Hal.Has("SPI.Configure")} adc={Hal.Has("ADC.Configure")}");

// defaults: nothing set means "board default"
var s = new SpiConfig();
Console.WriteLine($"{s} f={s.Frequency} mode={s.Mode} bits={s.Bits} sck={s.Sck} dma={(s.Dma == null ? "default" : "set")}");
s.Frequency = 20_000_000; s.Sck = 6; s.Mosi = 7; s.Miso = "GPIO2"; s.Dma = true; s.MaxTransfer = 4096;
Console.WriteLine(s);
s.Dma = null;                          // back to the board default
s.Set("queue_size", 4).Set("Mode", 3); // a board option and a property by name
Console.WriteLine($"{s} q={s.Get("queue_size")} mode={s.Get("mode")}");
try { s.Mode = 4; } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { s.Set("queue_size", 99); } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
try { s.Set("speed", 1); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
s.Reset();
Console.WriteLine(s);

// constructor shortcuts + object initializers
var spi = new SpiConfig(8_000_000, 0, 18, 23, 19) { Cs = 5 };
Console.WriteLine(spi);
SPI.Open(0, spi);
Console.WriteLine(string.Join(",", SPI.Transfer(0, new byte[] { 1, 2, 3 })));   // Cs from the config, driven by MicroCS
try { SPI.Open(0, new SpiConfig { Bits = 12 }); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
var dev = new SpiDevice(0, 4, new SpiConfig(1_000_000, 3));
Console.WriteLine($"dev bus={dev.Bus} cs={dev.ChipSelect} {string.Join(",", dev.Transfer(new byte[] { 9 }))}");

// GPIO
var g = new GpioConfig(GPIO.Input, GPIO.PullUp) { Schmitt = true, FilterNs = 100 };
Console.WriteLine(g);
GPIO.Configure(3, g);
Console.WriteLine($"pull-up reads {GPIO.Read(3)}");
var led = new Pin("LED", new GpioConfig(GPIO.Output) { DriveMilliamps = 12, Initial = 1, Speed = GPIO.SpeedHigh });
Console.WriteLine($"led {led.Number} = {led.Value}");
try { GPIO.Configure(4, new GpioConfig(GPIO.Output) { DriveMilliamps = 5 }); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
try { led.Configure(new GpioConfig { Pull = GPIO.PullBoth }); } catch (NotSupportedException e) { Console.WriteLine(e.Message); }

// UART / I2C / QSPI / PWM / ADC
UART.Open(1, new UartConfig(9600, 17, 16) { Parity = UART.ParityEven, RxBuffer = 1024, Rs485 = true });
UART.Write(1, "hi");
Console.WriteLine("uart " + UART.ReadString(1, 2));
var i2c = new I2cConfig(400_000, 21, 22) { PullUps = false, GlitchFilter = 7, TimeoutMs = 50 };
i2c.Set("timing", 0x10909CEC);
Console.WriteLine(i2c);
I2C.Open(0, i2c);
Console.WriteLine("scan " + string.Join(",", I2C.Scan(0)));
try { I2C.Open(0, new I2cConfig(3_400_000)); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
try { QSPI.Open(0, new QspiConfig(80_000_000) { Mode = 1 }); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
QSPI.Open(0, new QspiConfig(80_000_000) { Sck = 30, Cs = 31, Io0 = 32, Io1 = 33, Io2 = 34, Io3 = 35, FlashSizeLog2 = 24 });
Console.WriteLine("jedec " + string.Join(",", QSPI.Read(0, 0x9F, -1, 3)));
PWM.Configure(2, new PwmConfig(20000, 25) { ResolutionBits = 10, Inverted = true });
var adc = new AdcConfig(ADC.Atten12dB) { Oversample = 4 };
ADC.Configure(0, adc);
Console.WriteLine($"{adc} raw={ADC.Read(0)}");
try { ADC.Configure(1, new AdcConfig { ResolutionBits = 10 }); } catch (NotSupportedException e) { Console.WriteLine(e.Message); }

// the board's own options
foreach (var o in Hal.Options("SPI")) Console.WriteLine(o);
Console.WriteLine($"{Hal.Options().Count} options");
