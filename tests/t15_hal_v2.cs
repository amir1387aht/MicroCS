// args: --sim --sim-virtual
// HAL v2: object API, interrupts, timers, DAC, I2S, QSPI, CAN, watchdog, RTC
Console.WriteLine($"api {Hal.ApiVersion} irq={Hal.Has("GPIO.IRQ")} i2s={Hal.Has("I2S")} qspi={Hal.Has("QSPI")}");

// ---- pins: numbers, names, int levels
Console.WriteLine($"pins {GPIO.Pin("PA5")} {GPIO.Pin("PB12")} {GPIO.Pin("P1.03")} {GPIO.Pin("GPIO21")} {GPIO.Pin("LED")}");
GPIO.Mode(4, GPIO.Output);
GPIO.Write(4, 1); bool a = GPIO.Read(4);
GPIO.Write(4, 0); bool b = GPIO.Read(4);
Console.WriteLine($"int levels {a} {b}");
try { GPIO.Pin("nope"); } catch (ArgumentException e) { Console.WriteLine(e.Message); }

var led = new Pin("LED", GPIO.Output);
led.High(); Console.WriteLine($"led {led.Number} {led.Value}");
led.Toggle(); Console.WriteLine($"led {led.Read()}");
led.Value = true; Console.WriteLine($"led {led.Value}");

// ---- interrupts: an output with an armed IRQ fires on its own edges
int edges = 0;
var irq = new Pin(7, GPIO.Output);
irq.OnChange(GPIO.Both, (int p, bool level) => { edges++; Console.WriteLine($"  edge pin {p} -> {level}"); });
irq.High(); irq.Low(); irq.Low();
Console.WriteLine($"queued, dispatched {Hal.Poll()} edges={edges}");
GPIO.OnChange(8, GPIO.Rising, (bool level) => Console.WriteLine($"  rising {level}"));
GPIO.Mode(8, GPIO.Output);
GPIO.Write(8, true); GPIO.Write(8, false);
Thread.Sleep(0);                       // callbacks also run while the script sleeps
GPIO.Off(8);
GPIO.Write(8, true);
Console.WriteLine($"after off {Hal.Poll()}");

// ---- timers (virtual 1 ms per poll on the simulator)
int ticks = 0;
Timer.Start(0, 2000, () => ticks++);
for (int i = 0; i < 6; i++) Hal.Poll();
Timer.Stop(0);
Console.WriteLine($"timer ticks {ticks}");
bool once = false;
Timer.Once(1, 1000, () => once = true);
Hal.Poll(); Hal.Poll();
Console.WriteLine($"once {once}");

// ---- user events
Hal.OnEvent(3, (int src, int val) => Console.WriteLine($"  user event {src} {val}"));
Hal.Post(3, 9, 42);
Hal.Poll();

// ---- UART: frame format, WriteLine/ReadLine, receive callback
UART.Open(2, 9600, 8, UART.ParityEven, 1);
int got = 0;
UART.OnReceive(2, (int n) => got += n);
UART.WriteLine(2, "hello");
Hal.Poll();
Console.WriteLine($"line '{UART.ReadLine(2, 50)}' rx={got} next={UART.ReadLine(2, 5) == null}");

// ---- I2C helpers and device objects
I2C.Open(0, 400000);
Console.WriteLine("scan " + string.Join(",", I2C.Scan(0).Select(x => $"0x{x:X2}")));
var imu = new I2cDevice(0, 0x68);
Console.WriteLine($"whoami 0x{imu.ReadRegister(0x75):X2}");
imu.WriteRegister(0x6B, 0x01);
imu.WriteRegister(0x10, new byte[] { 5, 6, 7 });
Console.WriteLine($"regs {imu.ReadRegister(0x6B)} {string.Join(",", imu.ReadRegisters(0x10, 3))}");
Console.WriteLine($"static {I2C.ReadRegister(0, 0x68, 0x11)}");

// ---- SPI device with chip select
var spi = new SpiDevice(0, 10, 8000000, 3);
Console.WriteLine($"spi {string.Join(",", spi.Transfer(new byte[] { 9, 8 }))} cs={GPIO.Read(10)}");
Console.WriteLine($"spi wr {string.Join(",", spi.WriteRead(new byte[] { 0x9F }, 2))} rd {spi.Read(2).Length}");

// ---- ADC / DAC
Console.WriteLine($"adc {ADC.Read(1)} raw = {ADC.ReadMillivolts(1)} mV, avg {ADC.ReadAverage(1, 4)}, vref {ADC.ReferenceMillivolts}");
DAC.Write(0, 4095); DAC.WriteMillivolts(1, 1650);
Console.WriteLine($"dac bits {DAC.Resolution}");
try { DAC.Write(0, 5000); } catch (ArgumentOutOfRangeException) { Console.WriteLine("dac range"); }

// ---- PWM extras
PWM.Servo(2, 90); PWM.SetPulse(3, 50, 1500); PWM.Tone(4, 440); PWM.Stop(4);
Console.WriteLine("pwm ok");

// ---- I2S loopback
I2S.Open(0, 16000, 16, 1, I2S.Duplex);
Console.WriteLine($"i2s wrote {I2S.WriteSamples(0, new int[] { 1000, -1000, 32767, -32768 })} samples");
Console.WriteLine("i2s read " + string.Join(",", I2S.ReadSamples(0, 4)));

// ---- QSPI NOR flash
QSPI.Open(0, 50000000);
Console.WriteLine("jedec " + string.Join(" ", QSPI.Read(0, 0x9F, -1, 3).Select(x => x.ToString("X2"))));
QSPI.Command(0, 0x06);
QSPI.Write(0, 0x02, 0x100, new byte[] { 0xDE, 0xAD, 0xBE, 0xEF });
Console.WriteLine("flash " + string.Join(" ", QSPI.Read(0, 0xEB, 0x100, 4, 6, 4).Select(x => x.ToString("X2"))));
QSPI.Command(0, 0x06); QSPI.Command(0, 0x20, 0);
Console.WriteLine($"erased {QSPI.Read(0, 0x03, 0x100, 1)[0]:X2}");

// ---- CAN loopback
CAN.Open(0, 500000);
CAN.Send(0, 0x123, new byte[] { 1, 2, 3 });
CAN.Send(0, new CanFrame(0x1ABCDEF, new byte[] { 0xFF }, true));
var f1 = CAN.Receive(0); var f2 = CAN.Receive(0);
Console.WriteLine($"can {f1} | {f2} ext={f2.Extended} next={CAN.Receive(0) == null}");

// ---- watchdog, RTC, system
Watchdog.Start(2000); Watchdog.Feed();
RTC.Set(1700000000);
Console.WriteLine($"rtc {RTC.Now >= 1700000000} uid {Hal.UniqueId} cpu {Hal.CpuHz / 1000000} MHz");
long t0 = Hal.Micros; Hal.DelayMicroseconds(250);
Console.WriteLine($"delay {Hal.Micros - t0 >= 250}");
try { Timer.Start(9, 100, () => {}); } catch (NotSupportedException e) { Console.WriteLine(e.Message); }
