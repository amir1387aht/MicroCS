/* MicroCS Studio - templates and examples ("Templates" button, Ctrl+Shift+N).
 * Add your own: T(category, name, file name, description, String.raw`code`).
 * Every C# template is compiled and run on the simulator by tests/studio/test_templates.sh. */
const TEMPLATES = [];
function T(cat, name, file, desc, text) { TEMPLATES.push({ cat, name, file, desc, text: text.replace(/^\n/, "") }); }

/* ---------------------------------------------------------------- Getting started */
T("Getting started", "Empty C# file", "program.cs",
  "An empty script with a comment header.", String.raw`
// program.cs - runs on the device: Run (F5) saves it to the board and starts it.
`);
T("Getting started", "Hello world", "hello.cs",
  "Print a greeting and the uptime.", String.raw`
// Hello from the device
Console.WriteLine("Hello from MicroCS!");
Console.WriteLine($"Board: {Hal.Board}, uptime {Environment.TickCount} ms");
`);
T("Getting started", "Main loop (Arduino style)", "loop.cs",
  "setup() once, then loop() forever - Stop ends it.", String.raw`
// The classic firmware shape: setup() once, then loop() forever.
// Press Stop (or Ctrl+C in the console) to end it.
var led = new Pin(2, GPIO.Output);      // 2 = your LED's GPIO (ESP32-S3 DevKit: 48, Pico: 25)
int count = 0;

void Setup()
{
    Console.WriteLine("setup done");
}

void Loop()
{
    led.Toggle();
    count++;
    if (count % 10 == 0) Console.WriteLine($"loop #{count}");
    Thread.Sleep(100);
}

Setup();
while (true) Loop();
`);
T("Getting started", "Console output and formatting", "format.cs",
  "Write, WriteLine, interpolation, alignment and number formats.", String.raw`
// Console output and string formatting
int n = 42;
double v = 3.14159;
Console.Write("no newline, ");
Console.WriteLine("then a newline");
Console.WriteLine($"int {n}, hex 0x{n:X4}, padded [{n,6}] [{n,-6}]");
Console.WriteLine($"fixed {v:F2}, percent {0.256:P1}, thousands {1234567:N0}");
Console.WriteLine(string.Format("{0} + {1} = {2}", 2, 3, 2 + 3));
Console.WriteLine("a,b,c".Split(',').Length + " parts");
Console.WriteLine(string.Join(" | ", new[] { "left", "middle", "right" }));
`);
T("Getting started", "Read input from the console", "input.cs",
  "Ask a question with Console.ReadLine (type the answer in the console).", String.raw`
// Type the answer in the console box below and press Enter
Console.Write("What is your name? ");
string name = Console.ReadLine();
Console.WriteLine($"Hello, {name}!");

Console.Write("Pick a number: ");
if (int.TryParse(Console.ReadLine(), out int n))
    Console.WriteLine($"{n} squared is {n * n}");
else
    Console.WriteLine("that was not a number");
`);
T("Getting started", "Uptime and timing", "timing.cs",
  "Environment.TickCount, Stopwatch and Thread.Sleep.", String.raw`
// Measure how long things take
var sw = Stopwatch.StartNew();
Thread.Sleep(250);
Console.WriteLine($"slept {sw.ElapsedMilliseconds} ms");

sw.Restart();
int sum = 0;
for (int i = 0; i < 100000; i++) sum += i & 7;
sw.Stop();
Console.WriteLine($"100k loop iterations: {sw.ElapsedMilliseconds} ms (sum {sum})");
Console.WriteLine($"uptime: {Environment.TickCount / 1000.0:F1} s, {Hal.Micros} us");
`);
/* ---------------------------------------------------------------- Boot and startup */
T("Boot and startup", "boot.cs - first script at power-up", "boot.cs",
  "Runs first at every boot: hardware setup and a banner. Save as /boot.cs.", String.raw`
// /boot.cs - the first script after power-up / reset (before /jobs.cfg and /main.cs).
// Keep it short and safe: set up pins and buses, print a banner, load settings.
// Globals do not survive into main.cs - each script is its own program.
Console.WriteLine($"MicroCS on {Hal.Board} - boot.cs");

GPIO.Mode(2, GPIO.Output);              // status LED (change 2 to your LED pin)
GPIO.Write(2, true);

if (Hal.Has("I2C")) I2C.Open(0, 400000);    // buses stay configured for later scripts

// count boots in a file
int boots = File.Exists("/boots.txt") ? int.Parse(File.ReadAllText("/boots.txt").Trim()) : 0;
boots++;
File.WriteAllText("/boots.txt", boots.ToString());
Console.WriteLine($"boot #{boots}, {GC.GetTotalMemory(false)} bytes in use");
`);
T("Boot and startup", "main.cs - the application", "main.cs",
  "Runs at every boot after boot.cs and jobs.cfg; starts background jobs. Save as /main.cs.", String.raw`
// /main.cs - your application, started at every boot (after /boot.cs and /jobs.cfg).
// It may run forever (a main loop) or start background jobs and return:
// jobs keep running and the shell / REPL stays usable over USB.
Console.WriteLine("main.cs: starting the application");

var led = new Pin(2, GPIO.Output);      // change 2 to your LED pin
Scheduler.Every(500, () => led.Toggle());                      // heartbeat
Scheduler.Every(10000, () => Console.WriteLine($"[{Environment.TickCount / 1000}s] alive, {GC.GetTotalMemory(false)} B used"));

Console.WriteLine($"main.cs: {Scheduler.Count} jobs running - type 'jobs' in the Shell console");
`);
T("Boot and startup", "main.cs with safe mode", "main.cs",
  "Hold a button at boot to skip the application (recovery when a script misbehaves).", String.raw`
// /main.cs with a "safe mode": hold the BOOT button (GPIO 0, to GND) while the
// board starts and the application is skipped - handy when a script misbehaves.
var button = new Pin(0, GPIO.InputPullUp);
if (!button.Read())
{
    Console.WriteLine("safe mode: button held - application not started");
    return;
}

Console.WriteLine("starting the application");
var led = new Pin(2, GPIO.Output);
Scheduler.Every(1000, () => led.Toggle());
`);
T("Boot and startup", "main.cs with settings file", "main.cs",
  "Read key=value settings from /settings.txt (created with defaults on first boot).", String.raw`
// Read settings from /settings.txt ("key=value" lines, # comments).
// The file is created with defaults the first time.
const string SettingsFile = "/settings.txt";
if (!File.Exists(SettingsFile))
    File.WriteAllText(SettingsFile, "# MicroCS settings\nname=my-board\nled=2\nblink_ms=500\n");

var cfg = new Dictionary<string, string>();
foreach (var raw in File.ReadAllLines(SettingsFile))
{
    var line = raw.Trim();
    if (line.Length == 0 || line.StartsWith("#")) continue;
    int eq = line.IndexOf('=');
    if (eq > 0) cfg[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
}

string Get(string key, string def) => cfg.TryGetValue(key, out var v) ? v : def;

Console.WriteLine($"name = {Get("name", "?")}");
var led = new Pin(int.Parse(Get("led", "2")), GPIO.Output);
Scheduler.Every(int.Parse(Get("blink_ms", "500")), () => led.Toggle());
`);
T("Boot and startup", "main.cs with a watchdog", "main.cs",
  "A main loop guarded by the watchdog: a hang resets the board.", String.raw`
// A main loop guarded by the hardware watchdog: if the loop hangs for more than
// 3 s the board resets and starts again. Feed it from the loop, never from a timer.
Watchdog.Start(3000);
Console.WriteLine("watchdog armed (3 s)");

int i = 0;
while (true)
{
    // ... the real work ...
    if (++i % 20 == 0) Console.WriteLine($"working, loop {i}");
    Watchdog.Feed();
    Thread.Sleep(100);
}
`);
T("Boot and startup", "boot log to a file", "boot.cs",
  "Append one line per boot to /boot.log and keep the file small.", String.raw`
// Append a line per boot to /boot.log; keep only the last 50 lines.
string line = $"boot at tick {Environment.TickCount}, heap {GC.GetTotalMemory(false)} B, board {Hal.Board}";
File.AppendAllText("/boot.log", line + "\n");

var lines = File.ReadAllLines("/boot.log");
if (lines.Length > 50)
    File.WriteAllLines("/boot.log", lines.Skip(lines.Length - 50).ToArray());
Console.WriteLine($"boot.log: {Math.Min(lines.Length, 50)} entries");
`);
T("Boot and startup", "jobs.cfg - scheduled scripts", "jobs.cfg",
  "The jobs table read at boot: run scripts at startup, after a delay or periodically.", String.raw`
# /jobs.cfg - read at boot, after /boot.cs and before /main.cs.
# Each line: kind  time  script  [policy]
#   kind     startup | after | every
#   time     number with ms / s / m / h (not used by startup)
#   policy   restart=never (default: stop after the first failure)
#            restart=always | restart=N (stop after N failures in a row)
# Every job runs a script file; the scripts can be ordinary .cs files or .mcsb images.
startup          /init.cs
after     5s     /selftest.cs
every     500ms  /blink.cs       restart=always
every     1m     /report.cs      restart=3
`);
T("Boot and startup", "Job script: blink (for jobs.cfg)", "blink.cs",
  "A tiny script run every 500 ms by jobs.cfg - each run toggles the LED once.", String.raw`
// /blink.cs - started by jobs.cfg ("every 500ms /blink.cs"): it must finish quickly.
// Each run is a fresh program, so keep state in the pin itself (or in a file).
GPIO.Mode(2, GPIO.Output);
GPIO.Toggle(2);
`);
T("Boot and startup", "Job script: report (for jobs.cfg)", "report.cs",
  "Run every minute by jobs.cfg: log uptime, memory and storage to a file.", String.raw`
// /report.cs - started by jobs.cfg ("every 1m /report.cs restart=3")
var d = new DriveInfo("/");
string line = $"{Environment.TickCount / 1000}s heap={GC.GetTotalMemory(false)} free={d.AvailableFreeSpace / 1024}KB";
File.AppendAllText("/report.log", line + "\n");
Console.WriteLine(line);
`);
/* ---------------------------------------------------------------- Scheduler */
T("Scheduler", "Background jobs (Every / After)", "jobs.cs",
  "Jobs keep running after the script ends - like a firmware main loop.", String.raw`
// Jobs keep running after the script ends; the shell and REPL stay responsive.
var led = new Pin(2, GPIO.Output);
int blinkId = Scheduler.Every(500, () => led.Toggle());
Scheduler.Every(5000, () => Console.WriteLine($"alive, {GC.GetTotalMemory(false)} bytes in use"));
Scheduler.After(2000, () => Console.WriteLine("this runs once, 2 s after start"));
Console.WriteLine($"{Scheduler.Count} jobs started (blink id {blinkId}) - 'jobs' in the Shell lists them");
`);
T("Scheduler", "Cancel jobs", "cancel.cs",
  "Stop one job by id, or all of them, from another job.", String.raw`
// Start a fast blink, slow it down after 3 s, and stop everything after 8 s.
var led = new Pin(2, GPIO.Output);
int fast = Scheduler.Every(100, () => led.Toggle());

Scheduler.After(3000, () =>
{
    Scheduler.Cancel(fast);
    Scheduler.Every(500, () => led.Toggle());
    Console.WriteLine("slow blink");
});
Scheduler.After(8000, () =>
{
    Scheduler.CancelAll();
    led.Low();
    Console.WriteLine("all jobs stopped");
});
`);
T("Scheduler", "Failure policy (maxFailures)", "retry.cs",
  "A job that throws is stopped after N failures in a row.", String.raw`
// The 3rd argument of Scheduler.Every is the number of consecutive failures
// after which the job is stopped (default: the first exception stops it).
int tries = 0;
Scheduler.Every(1000, () =>
{
    tries++;
    Console.WriteLine($"attempt {tries}");
    if (tries % 2 == 1) throw new IOException("sensor did not answer");
    Console.WriteLine("  ok");
}, 3);
`);
T("Scheduler", "Sensor logger job", "logger_job.cs",
  "Sample the ADC every second and append CSV rows to a file; rotate when large.", String.raw`
// Log ADC channel 0 every second to /log.csv, rotate the file at 16 KB.
const string Log = "/log.csv";
if (!File.Exists(Log)) File.WriteAllText(Log, "ms,mV\n");

Scheduler.Every(1000, () =>
{
    int mv = ADC.ReadMillivolts(0);
    File.AppendAllText(Log, $"{Environment.TickCount},{mv}\n");
    if (File.GetLength(Log) > 16 * 1024)
    {
        if (File.Exists("/log.old.csv")) File.Delete("/log.old.csv");
        File.Move(Log, "/log.old.csv");
        File.WriteAllText(Log, "ms,mV\n");
    }
}, 5);
Console.WriteLine($"logging to {Log}");
`);
T("Scheduler", "State machine with jobs", "traffic.cs",
  "A traffic light driven by After() - no blocking, no Thread.Sleep.", String.raw`
// A traffic light as a state machine: each state schedules the next one.
var red = new Pin(2, GPIO.Output);
var yellow = new Pin(3, GPIO.Output);
var green = new Pin(4, GPIO.Output);

void Show(bool r, bool y, bool g) { red.Write(r); yellow.Write(y); green.Write(g); }

void Red()    { Show(true, false, false); Console.WriteLine("red");    Scheduler.After(3000, Green); }
void Green()  { Show(false, false, true); Console.WriteLine("green");  Scheduler.After(3000, Yellow); }
void Yellow() { Show(false, true, false); Console.WriteLine("yellow"); Scheduler.After(1000, Red); }

Red();
`);
T("Scheduler", "Debounced button + job", "debounce.cs",
  "Interrupt marks the press, a scheduled job handles it after the bounce.", String.raw`
// Handle a bouncy button: the interrupt only records the time, a 20 ms job
// later decides whether it was a real press.
var button = new Pin(0, GPIO.InputPullUp);     // button between GPIO 0 and GND
int lastEdge = 0, presses = 0;
bool pending = false;

button.OnChange(GPIO.Falling, (bool level) =>
{
    lastEdge = Environment.TickCount;
    if (pending) return;
    pending = true;
    Scheduler.After(20, () =>
    {
        pending = false;
        if (!button.Read()) Console.WriteLine($"pressed ({++presses})");
    });
});
Console.WriteLine("press the button");
`);
T("Scheduler", "Watchdog fed by a job", "wdt_job.cs",
  "Keep the watchdog happy from a background job while the app runs.", String.raw`
// Feed the watchdog from a job: if the scheduler stops running (a script
// hangs in a tight loop), the board resets.
Watchdog.Start(5000);
Scheduler.Every(1000, () => Watchdog.Feed());
Console.WriteLine("watchdog armed, fed every second by a job");
`);
T("Scheduler", "Timeout with After", "timeout.cs",
  "Wait for an event, give up after a deadline.", String.raw`
// Wait up to 5 s for a button press, otherwise time out.
var button = new Pin(0, GPIO.InputPullUp);
bool done = false;

int timeout = Scheduler.After(5000, () =>
{
    if (done) return;
    done = true;
    Console.WriteLine("timed out - no press");
});
button.OnChange(GPIO.Falling, (bool level) =>
{
    if (done) return;
    done = true;
    Scheduler.Cancel(timeout);
    Console.WriteLine("pressed in time!");
});
Console.WriteLine("press the button within 5 s");
`);
T("Scheduler", "Periodic report of all jobs", "jobstats.cs",
  "Count runs of several jobs and print a summary.", String.raw`
// Three jobs at different rates; a fourth prints how often each one ran.
var runs = new int[3];
Scheduler.Every(100, () => runs[0]++);
Scheduler.Every(250, () => runs[1]++);
Scheduler.Every(1000, () => runs[2]++);
Scheduler.Every(3000, () => Console.WriteLine($"100ms:{runs[0]} 250ms:{runs[1]} 1s:{runs[2]} jobs:{Scheduler.Count}"));
`);
/* ---------------------------------------------------------------- GPIO */
T("GPIO", "Blink an LED", "blink.cs",
  "Toggle an output pin with the Pin object.", String.raw`
// Blink the on-board LED. Use a GPIO number (ESP32: 2, ESP32-S3 DevKit: 48 (RGB), Pico: 25)
// or a board pin name such as "LED", "PA5", "GPIO21", "GP15".
var led = new Pin(2, GPIO.Output);

for (int i = 0; i < 10; i++)
{
    led.Toggle();
    Console.WriteLine($"LED {(led.Value ? "on" : "off")}");
    Thread.Sleep(250);
}
led.Low();
`);
T("GPIO", "Static GPIO calls", "gpio.cs",
  "GPIO.Mode / Write / Read / Toggle without objects.", String.raw`
// The static GPIO API: no objects, just pin numbers.
const int Led = 2, Button = 0;
GPIO.Mode(Led, GPIO.Output);
GPIO.Mode(Button, GPIO.InputPullUp);

GPIO.Write(Led, 1);
Thread.Sleep(200);
GPIO.Toggle(Led);
Console.WriteLine($"LED is {GPIO.Read(Led)}, button is {(GPIO.Read(Button) ? "released" : "pressed")}");
`);
T("GPIO", "Pin names", "pinnames.cs",
  "Resolve board pin names to numbers with GPIO.Pin.", String.raw`
// Pin names are resolved by the port: "LED", "PA5", "GPIO21", "GP15", "P0.13", "A0"...
foreach (var name in new[] { "GPIO2", "GPIO21", "LED" })
{
    try { Console.WriteLine($"{name,-8} -> {GPIO.Pin(name)}"); }
    catch (Exception e) { Console.WriteLine($"{name,-8} -> not on this board ({e.Message})"); }
}
`);
T("GPIO", "Read a button (polling)", "button_poll.cs",
  "Poll an input and detect presses with edge detection.", String.raw`
// Poll a button between GPIO 0 and GND (BOOT button on most ESP32 boards).
var button = new Pin(0, GPIO.InputPullUp);
bool last = true;
int presses = 0;
Console.WriteLine("press the button (Stop ends the script)");
while (true)
{
    bool now = button.Read();
    if (last && !now) Console.WriteLine($"pressed {++presses}x");
    last = now;
    Thread.Sleep(10);
}
`);
T("GPIO", "Button interrupt", "button.cs",
  "React to a button with OnChange - callbacks run safely between statements.", String.raw`
// Count presses of a button between GPIO 0 (BOOT on most ESP32 boards) and GND.
int presses = 0;
var button = new Pin(0, GPIO.InputPullUp);
button.OnChange(GPIO.Falling, (bool level) =>
{
    presses++;
    Console.WriteLine($"pressed {presses}x");
});
Console.WriteLine("Press the button... (Stop ends the script)");
while (true) Thread.Sleep(100);
`);
T("GPIO", "Interrupts on several pins", "multi_irq.cs",
  "One handler for several pins with GPIO.OnChange(pin, edge, (pin, level) => ...).", String.raw`
// One handler for several inputs: the 2-argument callback gets the pin and its level.
int[] pins = { 4, 5, 6 };
foreach (int p in pins)
{
    GPIO.Mode(p, GPIO.InputPullUp);
    GPIO.OnChange(p, GPIO.Both, (int pin, bool level) => Console.WriteLine($"GPIO {pin} -> {(level ? "high" : "low")}"));
}
Console.WriteLine("watching GPIO 4, 5, 6 for 30 s");
Hal.Run(30000);                       // dispatch events for 30 s, then end
foreach (int p in pins) GPIO.Off(p);
`);
T("GPIO", "Ultrasonic distance (HC-SR04)", "ultrasonic.cs",
  "Trigger pulse + GPIO.PulseIn to measure distance.", String.raw`
// HC-SR04: TRIG on GPIO 5, ECHO on GPIO 18 (use a divider on ECHO for 3.3 V chips).
const int Trig = 5, Echo = 18;
GPIO.Mode(Trig, GPIO.Output);
GPIO.Mode(Echo, GPIO.Input);

for (int i = 0; i < 10; i++)
{
    GPIO.Write(Trig, 1);
    Hal.DelayMicroseconds(10);
    GPIO.Write(Trig, 0);
    int us = GPIO.PulseIn(Echo, true, 30000);    // echo pulse length, 30 ms timeout
    if (us <= 0) Console.WriteLine("no echo");
    else Console.WriteLine($"{us / 58.0:F1} cm");
    Thread.Sleep(200);
}
`);
T("GPIO", "Knight rider (LED chaser)", "chaser.cs",
  "Run a light back and forth over several LEDs.", String.raw`
// LEDs on GPIO 4..9 light up one after another, back and forth.
var leds = new List<Pin>();
for (int p = 4; p <= 9; p++) leds.Add(new Pin(p, GPIO.Output));

for (int round = 0; round < 5; round++)
{
    for (int i = 0; i < leds.Count; i++) { leds[i].High(); Thread.Sleep(60); leds[i].Low(); }
    for (int i = leds.Count - 2; i > 0; i--) { leds[i].High(); Thread.Sleep(60); leds[i].Low(); }
}
`);
/* ---------------------------------------------------------------- PWM */
T("PWM", "Fade an LED (PWM)", "fade.cs",
  "Breathe an LED with PWM.Set(channel, hz, duty).", String.raw`
// Fade an LED on PWM channel 0 (the port maps channels to pins, see its README).
for (int round = 0; round < 3; round++)
{
    for (int i = 0; i <= 100; i += 2) { PWM.Set(0, 1000, i / 100.0); Thread.Sleep(10); }
    for (int i = 100; i >= 0; i -= 2) { PWM.Set(0, 1000, i / 100.0); Thread.Sleep(10); }
}
PWM.Stop(0);
`);
T("PWM", "Servo sweep", "servo.cs",
  "Move a hobby servo from 0 to 180 degrees and back.", String.raw`
// Servo signal on PWM channel 1 (50 Hz, 500-2500 us by default).
for (int deg = 0; deg <= 180; deg += 10) { PWM.Servo(1, deg); Thread.Sleep(80); }
for (int deg = 180; deg >= 0; deg -= 10) { PWM.Servo(1, deg); Thread.Sleep(80); }
PWM.SetPulse(1, 50, 1500);      // centre, as a raw pulse width
Thread.Sleep(500);
PWM.Stop(1);
`);
T("PWM", "Buzzer melody", "melody.cs",
  "Play notes on a passive buzzer with PWM.Tone.", String.raw`
// Passive buzzer on PWM channel 2.
int[] notes = { 262, 294, 330, 349, 392, 440, 494, 523 };   // C4 .. C5
int[] beats = { 1, 1, 1, 1, 1, 1, 1, 2 };
for (int i = 0; i < notes.Length; i++)
{
    PWM.Tone(2, notes[i]);
    Thread.Sleep(200 * beats[i]);
    PWM.Stop(2);
    Thread.Sleep(30);
}
`);
T("PWM", "Potentiometer dims an LED", "dimmer.cs",
  "ADC reading controls PWM duty - analog in, analog out.", String.raw`
// Potentiometer on ADC channel 0 sets the brightness of the LED on PWM channel 0.
int max = (1 << ADC.Resolution) - 1;
Console.WriteLine("turn the knob (Stop ends the script)");
while (true)
{
    int raw = ADC.ReadAverage(0, 8);
    PWM.SetPermille(0, 1000, raw * 1000 / max);
    Thread.Sleep(20);
}
`);
/* ---------------------------------------------------------------- ADC and DAC */
T("ADC and DAC", "Read an analog input", "adc.cs",
  "Raw value, millivolts and volts from an ADC channel.", String.raw`
// Read ADC channel 0 a few times.
Console.WriteLine($"{ADC.Resolution}-bit ADC, reference {ADC.ReferenceMillivolts} mV");
for (int i = 0; i < 5; i++)
{
    int raw = ADC.Read(0);
    int mv = ADC.ReadMillivolts(0);
    double v = ADC.ReadVoltage(0);
    Console.WriteLine($"raw {raw,5}  {mv,5} mV  {v:F3} V");
    Thread.Sleep(200);
}
`);
T("ADC and DAC", "Averaged, filtered ADC", "adc_filter.cs",
  "Oversampling plus an exponential moving average.", String.raw`
// Smooth a noisy analog signal: oversample, then low-pass filter.
double avg = ADC.ReadAverage(0, 16);
for (int i = 0; i < 50; i++)
{
    int sample = ADC.ReadAverage(0, 16);     // 16 readings averaged in C
    avg = avg * 0.9 + sample * 0.1;          // exponential moving average
    if (i % 5 == 0) Console.WriteLine($"sample {sample,5}  filtered {avg,8:F1}");
    Thread.Sleep(20);
}
`);
T("ADC and DAC", "Battery voltage (divider)", "battery.cs",
  "Scale a voltage divider reading and estimate a Li-ion charge level.", String.raw`
// Battery through a 100k/100k divider into ADC channel 0 (so the pin sees half the voltage).
const double Divider = 2.0;
double v = ADC.ReadAverage(0, 32) * ADC.ReferenceMillivolts / (double)((1 << ADC.Resolution) - 1) / 1000.0 * Divider;
double pct = Math.Clamp((v - 3.3) / (4.2 - 3.3) * 100, 0, 100);
Console.WriteLine($"battery {v:F2} V, about {pct:F0} %");
`);
T("ADC and DAC", "DAC output and sine wave", "dac.cs",
  "Set a voltage, then output a slow sine wave on a DAC channel.", String.raw`
// DAC channel 0 (ESP32: GPIO 25, STM32: PA4).
DAC.WriteMillivolts(0, 1650);
Console.WriteLine($"{DAC.Resolution}-bit DAC at 1.65 V");
Thread.Sleep(1000);

int max = (1 << DAC.Resolution) - 1;
for (int i = 0; i < 400; i++)
{
    DAC.Write(0, (int)((Math.Sin(i * Math.PI / 50) + 1) / 2 * max));
    Thread.Sleep(5);
}
`);
/* ---------------------------------------------------------------- UART */
T("UART", "Serial port: send and receive", "uart.cs",
  "Open a UART, write a line and wait for an answer.", String.raw`
// UART 1 at 9600 baud (set its TX/RX pins in the firmware's pin table).
UART.Open(1, 9600);
UART.WriteLine(1, "AT");
string reply = UART.ReadLine(1, 2000);      // null after 2 s without a full line
Console.WriteLine(reply == null ? "no answer" : $"got: {reply}");
UART.Close(1);
`);
T("UART", "Serial echo with OnReceive", "uart_echo.cs",
  "Echo everything received on a UART, event-driven.", String.raw`
// Echo everything that arrives on UART 1 back to the sender (and print it).
UART.Open(1, 115200);
UART.OnReceive(1, (int available) =>
{
    string s = UART.ReadString(1, available);
    Console.Write(s);
    UART.Write(1, s);
});
Console.WriteLine("echoing UART 1 (Stop ends the script)");
while (true) Thread.Sleep(100);
`);
T("UART", "GPS (NMEA) reader", "gps.cs",
  "Parse $GPGGA sentences from a GPS module.", String.raw`
// GPS module on UART 1 at 9600 baud: print fix, satellites and position.
UART.Open(1, 9600);
double ToDeg(string v, string hemi)
{
    if (v.Length < 4) return 0;
    int dot = v.IndexOf('.');
    double deg = double.Parse(v.Substring(0, dot - 2)) + double.Parse(v.Substring(dot - 2)) / 60.0;
    return hemi == "S" || hemi == "W" ? -deg : deg;
}
for (int i = 0; i < 60; i++)
{
    string line = UART.ReadLine(1, 2000);
    if (line == null || !(line.StartsWith("$GPGGA") || line.StartsWith("$GNGGA"))) continue;
    var f = line.Split(',');
    if (f.Length < 8 || f[6] == "0") { Console.WriteLine("no fix yet"); continue; }
    Console.WriteLine($"{ToDeg(f[2], f[3]):F5}, {ToDeg(f[4], f[5]):F5}  sats {f[7]}");
}
`);
T("UART", "Binary packets with checksum", "uart_packet.cs",
  "Build and parse framed packets with BitConverter.", String.raw`
// Packet: 0xAA, length, payload..., checksum (sum of payload bytes & 0xFF)
byte[] Frame(byte[] payload)
{
    var p = new List<byte> { 0xAA, (byte)payload.Length };
    int sum = 0;
    foreach (var b in payload) { p.Add(b); sum += b; }
    p.Add((byte)(sum & 0xFF));
    return p.ToArray();
}
var data = new List<byte>();
data.AddRange(BitConverter.GetBytes(1234));          // int32, little-endian
data.AddRange(BitConverter.GetBytes((short)-5));
byte[] pkt = Frame(data.ToArray());
Console.WriteLine(BitConverter.ToString(pkt));

UART.Open(1, 115200);
UART.Write(1, pkt);
Console.WriteLine($"sent {pkt.Length} bytes; value back = {BitConverter.ToInt32(pkt, 2)}");
`);
/* ---------------------------------------------------------------- I2C */
T("I2C", "I2C bus scan", "i2c_scan.cs",
  "List every device that answers on an I2C bus.", String.raw`
// List every device on I2C bus 0 (set the SDA/SCL pins in the firmware's pin table).
I2C.Open(0, 400000);
var found = I2C.Scan(0);
Console.WriteLine($"{found.Count} device(s):");
foreach (int addr in found) Console.WriteLine($"  0x{addr:X2}");
`);
T("I2C", "Read a register", "i2c_reg.cs",
  "Read and write registers with the static I2C API.", String.raw`
// Register access on a device at 0x68 (e.g. MPU-6050: WHO_AM_I = 0x75, PWR_MGMT_1 = 0x6B).
I2C.Open(0, 400000);
const int Addr = 0x68;
I2C.WriteRegister(0, Addr, 0x6B, 0x00);            // wake up
int who = I2C.ReadRegister(0, Addr, 0x75);
byte[] six = I2C.ReadRegisters(0, Addr, 0x3B, 6);   // 6 bytes from 0x3B
Console.WriteLine($"WHO_AM_I = 0x{who:X2}, data {BitConverter.ToString(six)}");
`);
T("I2C", "Temperature sensor (TMP102)", "tmp102.cs",
  "Read a TMP102 / LM75-style sensor with I2cDevice.", String.raw`
// TMP102 at 0x48: 12-bit temperature, 0.0625 C per bit (LM75: same layout, 9 bits).
I2C.Open(0, 100000);
var sensor = new I2cDevice(0, 0x48);
for (int i = 0; i < 5; i++)
{
    byte[] b = sensor.ReadRegisters(0x00, 2);
    int raw = ((b[0] << 8) | b[1]) >> 4;
    if (raw > 0x7FF) raw -= 4096;                     // negative temperatures
    Console.WriteLine($"{raw * 0.0625:F2} C");
    Thread.Sleep(500);
}
`);
T("I2C", "EEPROM (24Cxx) read and write", "eeprom.cs",
  "Write a string to an I2C EEPROM and read it back.", String.raw`
// 24C02-style EEPROM at 0x50: 1 address byte, 8-byte pages.
I2C.Open(0, 100000);
var rom = new I2cDevice(0, 0x50);
byte[] text = Encoding.UTF8.GetBytes("MicroCS");
var w = new List<byte> { 0x10 };                       // start address 0x10
w.AddRange(text);
rom.Write(w.ToArray());
Thread.Sleep(10);                                      // write cycle time
byte[] back = rom.WriteRead(new byte[] { 0x10 }, text.Length);
Console.WriteLine($"read back: {Encoding.UTF8.GetString(back)}");
`);
T("I2C", "BME280 / BMP280 chip id", "bme280.cs",
  "Identify a Bosch environmental sensor and read raw temperature.", String.raw`
// BME280 (id 0x60) / BMP280 (id 0x58) at 0x76 or 0x77.
I2C.Open(0, 400000);
int addr = I2C.Scan(0).Contains(0x77) ? 0x77 : 0x76;
var bme = new I2cDevice(0, addr);
int id = bme.ReadRegister(0xD0);
Console.WriteLine($"chip id 0x{id:X2} at 0x{addr:X2} ({(id == 0x60 ? "BME280" : id == 0x58 ? "BMP280" : "unknown")})");
bme.WriteRegister(0xF4, 0x27);                         // normal mode, x1 oversampling
Thread.Sleep(50);
byte[] t = bme.ReadRegisters(0xFA, 3);
Console.WriteLine($"raw temperature {(t[0] << 12) | (t[1] << 4) | (t[2] >> 4)} (apply the calibration from 0x88..)");
`);
T("I2C", "OLED display (SSD1306)", "oled.cs",
  "Initialise a 128x64 SSD1306 and draw a pattern.", String.raw`
// SSD1306 128x64 OLED at 0x3C.
I2C.Open(0, 400000);
var oled = new I2cDevice(0, 0x3C);
void Cmd(params int[] c) { foreach (int x in c) oled.Write(new byte[] { 0x00, (byte)x }); }

Cmd(0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
    0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF);
Cmd(0x21, 0, 127, 0x22, 0, 7);                         // whole screen
for (int page = 0; page < 8; page++)
{
    var row = new byte[129];
    row[0] = 0x40;                                     // data
    for (int x = 0; x < 128; x++) row[x + 1] = (byte)(((x + page) & 8) != 0 ? 0xAA : 0x55);
    oled.Write(row);
}
Console.WriteLine("pattern drawn");
`);
/* ---------------------------------------------------------------- SPI */
T("SPI", "SPI transfer", "spi.cs",
  "Full-duplex transfer with the static SPI API.", String.raw`
// SPI bus 0 at 1 MHz, mode 0, chip select on GPIO 10.
SPI.Open(0, 1000000, 0);
byte[] rx = SPI.Transfer(0, new byte[] { 0x9F, 0, 0, 0 }, 10);
Console.WriteLine($"received {BitConverter.ToString(rx)}");
`);
T("SPI", "SPI flash JEDEC id (SpiDevice)", "spi_flash.cs",
  "Read a NOR flash id and status with SpiDevice.", String.raw`
// W25Qxx flash: CS on GPIO 10, 8 MHz, mode 0.
var flash = new SpiDevice(0, 10, 8000000, 0);
byte[] id = flash.WriteRead(new byte[] { 0x9F }, 3);
Console.WriteLine($"JEDEC id {id[0]:X2} {id[1]:X2} {id[2]:X2}" + (id[0] == 0xEF ? " (Winbond)" : ""));
byte[] st = flash.WriteRead(new byte[] { 0x05 }, 1);
Console.WriteLine($"status register 0x{st[0]:X2}");
`);
T("SPI", "Shift register (74HC595)", "shift595.cs",
  "Drive 8 outputs through a 74HC595 over SPI.", String.raw`
// 74HC595: MOSI -> DS, SCK -> SH_CP, GPIO 10 -> ST_CP (latch).
var sr = new SpiDevice(0, 10, 1000000, 0);
for (int i = 0; i < 16; i++)
{
    sr.Write(new byte[] { (byte)(1 << (i % 8)) });
    Thread.Sleep(100);
}
sr.Write(new byte[] { 0 });
`);
/* ---------------------------------------------------------------- Timers and events */
T("Timers and events", "Hardware timer", "timer.cs",
  "Periodic and one-shot hardware timers with microsecond periods.", String.raw`
// Timer 0 samples the ADC every 10 ms; timer 1 fires once after 1 s.
var samples = new List<int>();
Timer.Start(0, 10000, () => samples.Add(ADC.Read(0)));
Timer.Once(1, 1000000, () => Console.WriteLine($"1 s later: {samples.Count} samples"));
Thread.Sleep(1500);
Timer.Stop(0);
Console.WriteLine($"average {samples.Average():F1} over {samples.Count} samples");
`);
T("Timers and events", "User events (Hal.Post / OnEvent)", "events.cs",
  "Post your own events and handle them like interrupts.", String.raw`
// Events with your own numbers: C code posts them with mcs_hal_post(MCS_HAL_EV_USER + n, source, value)
// from any ISR; scripts can post them too.
Hal.OnEvent(5, (int source, int value) => Console.WriteLine($"event 5 from {source}: {value}"));
for (int i = 0; i < 3; i++) Hal.Post(5, 1, i * 10);
Hal.Run(100);                                          // dispatch for 100 ms
Console.WriteLine($"dropped events: {Hal.DroppedEvents}");
`);
T("Timers and events", "Watchdog", "watchdog.cs",
  "Start the watchdog and feed it - stop feeding to see the reset.", String.raw`
// Feed the watchdog 10 times, then stop: the board resets about 2 s later.
Watchdog.Start(2000);
for (int i = 0; i < 10; i++)
{
    Watchdog.Feed();
    Console.WriteLine($"fed {i + 1}");
    Thread.Sleep(500);
}
Console.WriteLine("not feeding any more - reset in ~2 s");
Thread.Sleep(5000);
`);
T("Timers and events", "Real-time clock", "rtc.cs",
  "Set and read RTC.Now (Unix seconds) and format it.", String.raw`
// RTC.Now is Unix time in seconds. Set it once (e.g. from NTP / GPS / the PC).
if (RTC.Now < 1700000000) RTC.Set(1735689600);          // 2025-01-01 00:00:00 UTC
int now = RTC.Now;
int days = now / 86400, secs = now % 86400;
Console.WriteLine($"unix {now}: day {days} since 1970, {secs / 3600:D2}:{secs / 60 % 60:D2}:{secs % 60:D2} UTC");
`);
/* ---------------------------------------------------------------- Board and system */
T("Board and system", "Board information", "info.cs",
  "Board name, CPU clock, unique id and available peripherals.", String.raw`
// What is this board and what can it do?
Console.WriteLine($"board     {Hal.Board}");
Console.WriteLine($"HAL API   v{Hal.ApiVersion}");
Console.WriteLine($"CPU       {Hal.CpuHz / 1000000} MHz");
Console.WriteLine($"unique id {Hal.UniqueId}");
Console.WriteLine($"heap use  {GC.GetTotalMemory(false)} bytes");
var all = new[] { "GPIO", "GPIO.IRQ", "UART", "I2C", "SPI", "ADC", "DAC", "PWM", "Timer", "I2S", "QSPI", "CAN", "Watchdog", "RTC" };
Console.WriteLine("has       " + string.Join(" ", all.Where(n => Hal.Has(n))));
Console.WriteLine("missing   " + string.Join(" ", all.Where(n => !Hal.Has(n))));
if (Hal.Has("Drivers")) Console.WriteLine("drivers   " + string.Join(" ", Drivers.List));   // e.g. ws2812 (LedStrip)
`);
T("Board and system", "Memory and GC", "memory.cs",
  "Watch heap use and run the garbage collector.", String.raw`
// Heap use before and after allocating, and after a collection.
long before = GC.GetTotalMemory(false);
var junk = new List<string>();
for (int i = 0; i < 500; i++) junk.Add("item " + i);
long during = GC.GetTotalMemory(false);
junk = null;
long after = GC.GetTotalMemory(true);                    // true = collect first
Console.WriteLine($"before {before} B, with list {during} B, after GC {after} B");
Console.WriteLine($"collections so far: {GC.CollectionCount(0)}");
`);
T("Board and system", "Feature check (Hal.Has)", "portable.cs",
  "Write one script that adapts to the board's peripherals.", String.raw`
// Use what the board has, skip what it does not.
if (Hal.Has("DAC")) { DAC.WriteMillivolts(0, 1000); Console.WriteLine("DAC set to 1 V"); }
else Console.WriteLine("no DAC on this board");
if (Hal.Has("CAN")) Console.WriteLine("CAN available");
if (Hal.Has("RTC")) Console.WriteLine($"RTC time {RTC.Now}");
`);
T("Board and system", "Reset the board", "reset.cs",
  "Software reset with Hal.Reset.", String.raw`
// Restart the board in 3 s (boot.cs, jobs.cfg and main.cs run again).
for (int i = 3; i > 0; i--) { Console.WriteLine($"reset in {i}..."); Thread.Sleep(1000); }
Hal.Reset();
`);
T("Board and system", "Error handling for hardware", "errors.cs",
  "Driver errors are exceptions: IOException, TimeoutException, NotSupportedException.", String.raw`
// Hardware errors are ordinary C# exceptions.
try
{
    I2C.Open(0);
    I2C.Read(0, 0x3F, 2);                                // nobody at 0x3F? -> IOException (NACK)
    Console.WriteLine("device answered");
}
catch (IOException e) { Console.WriteLine($"I/O error: {e.Message}"); }
catch (TimeoutException) { Console.WriteLine("timeout"); }
catch (NotSupportedException e) { Console.WriteLine($"not on this board: {e.Message}"); }
finally { Console.WriteLine("done"); }
`);
/* ---------------------------------------------------------------- Audio, CAN, QSPI */
T("Audio, CAN, QSPI", "I2S tone", "i2s.cs",
  "Generate a sine tone and send it to an I2S DAC / amplifier.", String.raw`
// 16-bit mono at 16 kHz to an I2S amplifier (MAX98357A...) on bus 0.
I2S.Open(0, 16000, 16, 1);
var buf = new int[100];                                  // 100 samples = 6.25 ms (max 256 bytes per call)
for (int i = 0; i < buf.Length; i++) buf[i] = (int)(Math.Sin(2 * Math.PI * 440 * i / 16000.0) * 8000);
for (int n = 0; n < 160; n++) I2S.WriteSamples(0, buf);  // 1 s of 440 Hz
I2S.Close(0);
`);
T("Audio, CAN, QSPI", "I2S microphone level", "i2s_mic.cs",
  "Read samples from an I2S microphone and print the level.", String.raw`
// INMP441-style microphone on I2S bus 0 (receive).
I2S.Open(0, 16000, 32, 1, I2S.Receive);
for (int n = 0; n < 40; n++)
{
    int[] s = I2S.ReadSamples(0, 64);                    // 64 x 32-bit = 256 bytes per call
    double sum = 0;
    foreach (int x in s) sum += (double)x * x;
    Console.WriteLine($"level {Math.Sqrt(sum / s.Length):F0}");
}
I2S.Close(0);
`);
T("Audio, CAN, QSPI", "CAN bus send and receive", "can.cs",
  "Send frames and print received ones (TWAI on ESP32).", String.raw`
// CAN bus 0 at 500 kbit/s.
CAN.Open(0, 500000);
CAN.OnReceive(0, (int pending) =>
{
    CanFrame f;
    while ((f = CAN.Receive(0)) != null) Console.WriteLine($"rx {f}");
});
for (int i = 0; i < 5; i++)
{
    CAN.Send(0, 0x123, new byte[] { (byte)i, 0xAB });
    Thread.Sleep(200);
}
CAN.Send(0, new CanFrame(0x18FF50E5, new byte[] { 1, 2, 3 }, true));   // extended id
Hal.Run(500);
`);
T("Audio, CAN, QSPI", "QSPI flash id", "qspi.cs",
  "Read a QSPI NOR flash JEDEC id and data.", String.raw`
// QSPI NOR flash on bus 0.
QSPI.Open(0, 10000000);
byte[] id = QSPI.Read(0, 0x9F, -1, 3);                  // -1 = no address phase
Console.WriteLine($"JEDEC {BitConverter.ToString(id)}");
byte[] data = QSPI.Read(0, 0x03, 0, 16);                 // READ at address 0
Console.WriteLine($"first bytes {BitConverter.ToString(data)}");
`);
/* ---------------------------------------------------------------- Files and storage */
T("Files and storage", "Files and free space", "files.cs",
  "Write, read and list files; show the flash usage with DriveInfo.", String.raw`
// Write, read and list files on the device's flash
File.AppendAllText("/log.txt", $"boot at {Environment.TickCount} ms\n");
Console.WriteLine(File.ReadAllText("/log.txt"));

foreach (var f in Directory.GetFiles("/"))
    Console.WriteLine($"{f,-20} {File.GetLength(f),8} bytes");

var d = new DriveInfo("/");
Console.WriteLine($"{d.AvailableFreeSpace / 1024} KB free of {d.TotalSize / 1024} KB ({d.DriveFormat})");
`);
T("Files and storage", "Folders", "folders.cs",
  "Create, list and delete directories; walk a tree recursively.", String.raw`
// Directories: create, list recursively, clean up.
Directory.CreateDirectory("/demo/sub");
File.WriteAllText("/demo/a.txt", "A");
File.WriteAllText("/demo/sub/b.txt", "B");

void Walk(string dir, string indent)
{
    foreach (var d in Directory.GetDirectories(dir)) { Console.WriteLine($"{indent}{Path.GetFileName(d)}/"); Walk(d, indent + "  "); }
    foreach (var f in Directory.GetFiles(dir)) Console.WriteLine($"{indent}{Path.GetFileName(f)} ({File.GetLength(f)} B)");
}
Walk("/demo", "");
Directory.Delete("/demo", true);                         // true = recursive
Console.WriteLine($"exists after delete: {Directory.Exists("/demo")}");
`);
T("Files and storage", "Copy, move, delete", "fileops.cs",
  "File.Copy / Move / Delete / Exists and Path helpers.", String.raw`
File.WriteAllText("/a.txt", "hello");
File.Copy("/a.txt", "/b.txt", true);                     // overwrite
File.Move("/b.txt", "/c.txt");
Console.WriteLine($"a:{File.Exists("/a.txt")} b:{File.Exists("/b.txt")} c:{File.Exists("/c.txt")}");
Console.WriteLine($"{Path.GetFileNameWithoutExtension("/c.txt")} has extension {Path.GetExtension("/c.txt")}");
Console.WriteLine(Path.Combine("/data", "logs", "today.csv"));
File.Delete("/a.txt"); File.Delete("/c.txt");
`);
T("Files and storage", "Lines and CSV", "csv.cs",
  "Write and parse a small CSV file.", String.raw`
// Write a CSV, read it back and compute per-column statistics.
var rows = new List<string> { "name,value" };
var rnd = new Random();
for (int i = 0; i < 10; i++) rows.Add($"s{i},{rnd.Next(100)}");
File.WriteAllLines("/data.csv", rows.ToArray());

var values = File.ReadAllLines("/data.csv").Skip(1).Select(l => int.Parse(l.Split(',')[1])).ToList();
Console.WriteLine($"{values.Count} rows, min {values.Min()}, max {values.Max()}, avg {values.Average():F1}");
`);
T("Files and storage", "Binary files", "binary.cs",
  "Store numbers in a binary file with BitConverter.", String.raw`
// Save three readings as raw little-endian bytes and read them back.
var bytes = new List<byte>();
foreach (int v in new[] { 1000, -42, 123456 }) bytes.AddRange(BitConverter.GetBytes(v));
File.WriteAllBytes("/values.bin", bytes.ToArray());

byte[] back = File.ReadAllBytes("/values.bin");
for (int i = 0; i < back.Length; i += 4) Console.WriteLine(BitConverter.ToInt32(back, i));
Console.WriteLine(BitConverter.ToString(back));
`);
T("Files and storage", "Simple key-value store", "kv.cs",
  "Persist settings across reboots in a file.", String.raw`
// A tiny persistent key/value store (one "key=value" per line).
Dictionary<string, string> Load(string path)
{
    var d = new Dictionary<string, string>();
    if (!File.Exists(path)) return d;
    foreach (var l in File.ReadAllLines(path)) { int i = l.IndexOf('='); if (i > 0) d[l.Substring(0, i)] = l.Substring(i + 1); }
    return d;
}
void Save(string path, Dictionary<string, string> d) => File.WriteAllLines(path, d.Select(kv => kv.Key + "=" + kv.Value).ToArray());

var store = Load("/store.txt");
int runs = store.ContainsKey("runs") ? int.Parse(store["runs"]) : 0;
store["runs"] = (runs + 1).ToString();
store["last"] = Environment.TickCount.ToString();
Save("/store.txt", store);
Console.WriteLine($"this script ran {runs + 1} time(s)");
`);
/* ---------------------------------------------------------------- C# language */
T("C# language", "Classes and interfaces", "classes.cs",
  "Interfaces, inheritance, virtual/override and properties.", String.raw`
interface IShape { double Area(); string Name { get; } }

abstract class Shape : IShape
{
    public abstract double Area();
    public virtual string Name => "shape";
    public override string ToString() => $"{Name} with area {Area():F2}";
}
class Circle : Shape
{
    public double R { get; }
    public Circle(double r) { R = r; }
    public override double Area() => Math.PI * R * R;
    public override string Name => "circle";
}
class Rect : Shape
{
    public double W { get; set; }
    public double H { get; set; }
    public override double Area() => W * H;
    public override string Name => "rectangle";
}

var shapes = new List<IShape> { new Circle(1.5), new Rect { W = 2, H = 3 } };
foreach (var s in shapes) Console.WriteLine(s);
Console.WriteLine($"total area {shapes.Sum(s => s.Area()):F2}");
`);
T("C# language", "A driver class", "driver.cs",
  "Wrap a sensor in a class with IDisposable and properties.", String.raw`
// A reusable driver: wraps an I2C sensor behind a clean API.
class Tmp102 : IDisposable
{
    readonly I2cDevice dev;
    public Tmp102(int bus, int address = 0x48) { I2C.Open(bus); dev = new I2cDevice(bus, address); }
    public double Celsius
    {
        get
        {
            byte[] b = dev.ReadRegisters(0, 2);
            int raw = ((b[0] << 8) | b[1]) >> 4;
            return (raw > 0x7FF ? raw - 4096 : raw) * 0.0625;
        }
    }
    public double Fahrenheit => Celsius * 9 / 5 + 32;
    public void Dispose() => Console.WriteLine("sensor released");
}

using (var t = new Tmp102(0))
    Console.WriteLine($"{t.Celsius:F2} C = {t.Fahrenheit:F1} F");
`);
T("C# language", "Collections", "collections.cs",
  "List, Dictionary, HashSet, Queue and Stack.", String.raw`
var list = new List<int> { 5, 3, 8 };
list.Add(1); list.Sort();
Console.WriteLine("list: " + string.Join(", ", list));

var ages = new Dictionary<string, int> { ["ann"] = 31, ["bob"] = 25 };
ages["cid"] = 40;
foreach (var (name, age) in ages) Console.WriteLine($"{name} is {age}");
Console.WriteLine(ages.TryGetValue("dan", out int a) ? $"dan {a}" : "no dan");

var seen = new HashSet<string> { "x", "y" };
Console.WriteLine($"added z: {seen.Add("z")}, added x again: {seen.Add("x")}");

var q = new Queue<string>(); q.Enqueue("first"); q.Enqueue("second");
var st = new Stack<int>(); st.Push(1); st.Push(2);
Console.WriteLine($"queue -> {q.Dequeue()}, stack -> {st.Pop()}");
`);
T("C# language", "LINQ", "linq.cs",
  "Where, Select, OrderBy, GroupBy, aggregates.", String.raw`
var readings = new[] { 21.5, 22.1, 35.0, 21.9, 22.4, -5.0, 22.0 };
var valid = readings.Where(r => r > 0 && r < 30).ToList();
Console.WriteLine($"valid {valid.Count}/{readings.Length}, avg {valid.Average():F2}, max {valid.Max()}");
Console.WriteLine("sorted: " + string.Join(" ", valid.OrderByDescending(r => r).Select(r => r.ToString("F1"))));

var words = new[] { "apple", "avocado", "banana", "blueberry", "cherry" };
foreach (var g in words.GroupBy(w => w[0]))
    Console.WriteLine($"{g.Key}: {string.Join(", ", g.Value)}");
Console.WriteLine($"any long word: {words.Any(w => w.Length > 8)}, first b: {words.First(w => w.StartsWith("b"))}");
`);
T("C# language", "Strings and StringBuilder", "strings.cs",
  "Common string operations and building text efficiently.", String.raw`
string s = "  MicroCS runs C# on microcontrollers  ";
Console.WriteLine($"[{s.Trim()}] upper: {s.Trim().ToUpper()}");
Console.WriteLine($"contains C#: {s.Contains("C#")}, index of runs: {s.IndexOf("runs")}");
Console.WriteLine(s.Trim().Replace("microcontrollers", "MCUs"));
Console.WriteLine(string.Join("-", s.Trim().Split(' ')));
Console.WriteLine($"[{"7".PadLeft(3, '0')}] [{s.Trim().Substring(0, 7)}] [{s.Trim()[^3..]}]");

var sb = new StringBuilder();
for (int i = 1; i <= 5; i++) sb.Append(i).Append(i < 5 ? "," : "");
sb.AppendLine().Append("done");
Console.WriteLine(sb.ToString());
`);
T("C# language", "Pattern matching and switch", "patterns.cs",
  "Switch expressions, relational patterns and type patterns.", String.raw`
string Classify(double t) => t switch
{
    < 0 => "freezing",
    < 15 => "cold",
    >= 15 and < 25 => "comfortable",
    _ => "hot",
};
foreach (var t in new[] { -3.0, 10, 21, 30 }) Console.WriteLine($"{t,5} C: {Classify(t)}");

object[] things = { 42, "text", 3.5, null };
foreach (var o in things)
{
    string d = o switch { int i when i > 10 => $"big int {i}", int i => $"int {i}", string str => $"string of {str.Length}", null => "null", _ => "something else" };
    Console.WriteLine(d);
}
`);
T("C# language", "Tuples and deconstruction", "tuples.cs",
  "Return several values; swap with tuples.", String.raw`
(int min, int max, double avg) Stats(List<int> v) => (v.Min(), v.Max(), v.Average());

var (lo, hi, mean) = Stats(new List<int> { 4, 8, 15, 16, 23, 42 });
Console.WriteLine($"min {lo}, max {hi}, avg {mean:F2}");

int a = 1, b = 2;
(a, b) = (b, a);
Console.WriteLine($"swapped: a={a} b={b}");

var points = new List<(int x, int y)> { (1, 2), (3, 4) };
foreach (var (x, y) in points) Console.WriteLine($"({x}, {y})");
`);
T("C# language", "Delegates, lambdas and events", "events_cs.cs",
  "Action, Func, closures and C# events.", String.raw`
class Thermostat
{
    public event Action<double> TooHot;
    public double Limit = 30;
    public void Report(double t) { if (t > Limit) TooHot?.Invoke(t); }
}

Func<int, int> square = x => x * x;
Action<string> log = msg => Console.WriteLine($"[log] {msg}");
log($"square(7) = {square(7)}");

int alarms = 0;
var th = new Thermostat();
th.TooHot += t => { alarms++; log($"too hot: {t}"); };
foreach (var t in new[] { 25.0, 31.5, 29.0, 33.0 }) th.Report(t);
log($"{alarms} alarms");
`);
T("C# language", "Generics", "generics.cs",
  "A generic ring buffer class for samples.", String.raw`
class RingBuffer<T>
{
    readonly T[] items; int start, count;
    public RingBuffer(int capacity) { items = new T[capacity]; }
    public int Count => count;
    public void Add(T item)
    {
        items[(start + count) % items.Length] = item;
        if (count < items.Length) count++; else start = (start + 1) % items.Length;
    }
    public T this[int i] => items[(start + i) % items.Length];
    public List<T> ToList() { var l = new List<T>(); for (int i = 0; i < count; i++) l.Add(this[i]); return l; }
}

var last5 = new RingBuffer<int>(5);
for (int i = 1; i <= 8; i++) last5.Add(i * 10);
Console.WriteLine($"{last5.Count} kept: {string.Join(", ", last5.ToList())}");
`);
T("C# language", "Enums and structs", "enums.cs",
  "Enums for states, flags arithmetic and a small struct.", String.raw`
enum Mode { Off, Idle, Running = 10, Error }
[System.Flags] enum Opt { None = 0, Log = 1, Led = 2, Beep = 4 }
struct Point { public int X, Y; public Point(int x, int y) { X = x; Y = y; } public override string ToString() => $"({X},{Y})"; }

Mode m = Mode.Running;
Console.WriteLine($"mode {m} = {(int)m}, next {m + 1}");
Opt o = Opt.Log | Opt.Beep;
Console.WriteLine($"options {(int)o}, has beep {(o & Opt.Beep) != 0}, has led {(o & Opt.Led) != 0}");
var p = new Point(3, 4);
Console.WriteLine($"point {p}, distance {Math.Sqrt(p.X * p.X + p.Y * p.Y)}");
`);
T("C# language", "Exceptions", "exceptions.cs",
  "try / catch / finally, filters and custom exceptions.", String.raw`
class SensorException : Exception { public int Code; public SensorException(string m, int code) : base(m) { Code = code; } }

void Read(int n)
{
    if (n == 2) throw new SensorException("checksum error", 2);
    if (n == 3) throw new InvalidOperationException("not ready");
    Console.WriteLine($"read {n} ok");
}
for (int i = 1; i <= 3; i++)
{
    try { Read(i); }
    catch (SensorException e) when (e.Code == 2) { Console.WriteLine($"sensor: {e.Message}"); }
    catch (Exception e) { Console.WriteLine($"{e.GetType()}: {e.Message}"); }
    finally { Console.WriteLine($"attempt {i} done"); }
}
`);
T("C# language", "Math and random numbers", "math.cs",
  "Math functions, rounding, clamping and Random.", String.raw`
Console.WriteLine($"sqrt(2) = {Math.Sqrt(2):F6}, pi = {Math.PI:F6}");
Console.WriteLine($"sin(30 deg) = {Math.Sin(30 * Math.PI / 180):F3}, atan2 = {Math.Atan2(1, 1) * 180 / Math.PI} deg");
Console.WriteLine($"round {Math.Round(2.567, 2)}, floor {Math.Floor(-1.5)}, ceiling {Math.Ceiling(1.2)}");
Console.WriteLine($"clamp {Math.Clamp(150, 0, 100)}, abs {Math.Abs(-7)}, pow {Math.Pow(2, 10)}");

var rnd = new Random(42);                                // seeded = repeatable
Console.WriteLine($"dice: {rnd.Next(1, 7)} {rnd.Next(1, 7)} {rnd.Next(1, 7)}, double {rnd.NextDouble():F3}");
`);
T("C# language", "Parsing and conversion", "parse.cs",
  "int.Parse, TryParse, Convert and bytes <-> text.", String.raw`
Console.WriteLine(int.Parse("123") + 1);
Console.WriteLine(double.TryParse("3.75", out double d) ? $"double {d}" : "bad");
Console.WriteLine(int.TryParse("12x", out _) ? "ok" : "12x is not a number");
Console.WriteLine($"hex FF = {Convert.ToInt32("FF", 16)}, 255 in binary = {Convert.ToString(255, 2)}");
Console.WriteLine($"char code of A = {(int)'A'}, code 66 = {(char)66}");
byte[] b = Encoding.UTF8.GetBytes("Hi!");
Console.WriteLine($"{BitConverter.ToString(b)} -> {Encoding.UTF8.GetString(b)}");
`);

/* completion snippets: type the shortcut, pick it from the list (Tab / Enter). $0 = caret */
/* ================================================================ more examples (v1.7) */
/* ---------------------------------------------------------------- Sensors */
T("Sensors", "MPU6050 accelerometer / gyro", "mpu6050.cs",
  "Wake the IMU, read acceleration, rotation and temperature.", String.raw`
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
`);
T("Sensors", "DS3231 real-time clock", "ds3231.cs",
  "Read and set the time of a DS3231 / DS1307 RTC module (BCD registers).", String.raw`
// DS3231 / DS1307 at 0x68: time in BCD registers 0x00..0x06
I2C.Open(0);
var rtc = new I2cDevice(0, 0x68);
int FromBcd(int b) => (b >> 4) * 10 + (b & 0x0F);
int ToBcd(int v) => ((v / 10) << 4) | (v % 10);

void SetTime(int year, int month, int day, int h, int m, int s)
{
    rtc.Write(new byte[] { 0x00, (byte)ToBcd(s), (byte)ToBcd(m), (byte)ToBcd(h), 1,
                           (byte)ToBcd(day), (byte)ToBcd(month), (byte)ToBcd(year % 100) });
}

// SetTime(2025, 1, 31, 12, 0, 0);              // run once to set the clock
byte[] r = rtc.ReadRegisters(0x00, 7);
Console.WriteLine($"20{FromBcd(r[6]):D2}-{FromBcd(r[5] & 0x1F):D2}-{FromBcd(r[4]):D2} " +
                  $"{FromBcd(r[2] & 0x3F):D2}:{FromBcd(r[1]):D2}:{FromBcd(r[0] & 0x7F):D2}");
byte[] t = rtc.ReadRegisters(0x11, 2);          // DS3231 temperature sensor
Console.WriteLine($"chip temperature {(sbyte)t[0] + (t[1] >> 6) * 0.25} C");
`);
T("Sensors", "ADS1115 16-bit ADC", "ads1115.cs",
  "Single-shot conversions on the four inputs of an ADS1115.", String.raw`
// ADS1115 at 0x48: 16-bit ADC, +-4.096 V range, single-shot per channel
I2C.Open(0, 400000);
var adc = new I2cDevice(0, 0x48);

double ReadVolts(int channel)
{
    int mux = 4 + channel;                                   // AINx vs GND
    int config = 0x8000 | (mux << 12) | (1 << 9) | (1 << 8) | (4 << 5) | 3;   // start, PGA 4.096 V, single, 128 SPS
    adc.Write(new byte[] { 0x01, (byte)(config >> 8), (byte)config });
    Thread.Sleep(9);                                          // conversion time at 128 SPS
    byte[] r = adc.ReadRegisters(0x00, 2);
    short raw = (short)((r[0] << 8) | r[1]);
    return raw * 4.096 / 32768.0;
}

for (int i = 0; i < 10; i++)
{
    Console.WriteLine($"a0:{ReadVolts(0):F4} a1:{ReadVolts(1):F4} a2:{ReadVolts(2):F4} a3:{ReadVolts(3):F4}");
    Thread.Sleep(500);
}
`);
T("Sensors", "INA219 current / power monitor", "ina219.cs",
  "Bus voltage, shunt current and power from an INA219.", String.raw`
// INA219 at 0x40 with a 0.1 ohm shunt
I2C.Open(0);
var ina = new I2cDevice(0, 0x40);
void Write16(int reg, int v) => ina.Write(new byte[] { (byte)reg, (byte)(v >> 8), (byte)v });
int Read16(int reg) { byte[] b = ina.ReadRegisters(reg, 2); return (short)((b[0] << 8) | b[1]); }

Write16(0x00, 0x399F);                 // 32 V range, +-320 mV shunt, 12-bit, continuous
for (int i = 0; i < 10; i++)
{
    double shuntMv = Read16(0x01) * 0.01;
    double busV = (Read16(0x02) >> 3) * 0.004;
    double mA = shuntMv / 0.1;
    Console.WriteLine($"V:{busV:F3} mA:{mA:F1} mW:{busV * mA:F0}");
    Thread.Sleep(500);
}
`);
T("Sensors", "AHT20 / SHT31 humidity", "humidity.cs",
  "Temperature and relative humidity from an AHT20 (0x38) or SHT31 (0x44).", String.raw`
// Temperature and humidity: AHT20 / AHT21 (0x38) or SHT31 (0x44)
I2C.Open(0);
var found = I2C.Scan(0);

if (found.Contains(0x38))
{
    var aht = new I2cDevice(0, 0x38);
    aht.Write(new byte[] { 0xBE, 0x08, 0x00 });          // calibrate
    Thread.Sleep(10);
    aht.Write(new byte[] { 0xAC, 0x33, 0x00 });          // trigger a measurement
    Thread.Sleep(80);
    byte[] d = aht.Read(6);
    long hum = ((long)d[1] << 12) | ((long)d[2] << 4) | ((long)d[3] >> 4);
    long tmp = (((long)d[3] & 0x0F) << 16) | ((long)d[4] << 8) | d[5];
    Console.WriteLine($"AHT20: {tmp * 200.0 / 1048576 - 50:F1} C, {hum * 100.0 / 1048576:F1} %RH");
}
else if (found.Contains(0x44))
{
    var sht = new I2cDevice(0, 0x44);
    sht.Write(new byte[] { 0x24, 0x00 });                // single shot, high repeatability
    Thread.Sleep(20);
    byte[] d = sht.Read(6);
    int t = (d[0] << 8) | d[1], h = (d[3] << 8) | d[4];
    Console.WriteLine($"SHT31: {-45 + 175.0 * t / 65535:F1} C, {100.0 * h / 65535:F1} %RH");
}
else Console.WriteLine("no AHT20 (0x38) or SHT31 (0x44) found - addresses: " + string.Join(", ", found.Select(a => "0x" + a.ToString("X2"))));
`);
T("Sensors", "DHT22 (single-wire, timing)", "dht22.cs",
  "Read a DHT11/DHT22 with GPIO.PulseIn - shows bit-banged protocols.", String.raw`
// DHT22 / AM2302 on one GPIO (with a 10k pull-up). Bit-banged with PulseIn:
// the sensor answers with 40 bits; a long high pulse (~70 us) is a 1, a short one (~27 us) a 0.
const int DATA = 4;

double[] ReadDht()
{
    GPIO.Mode(DATA, GPIO.Output);
    GPIO.Write(DATA, 0); Thread.Sleep(2);       // start signal
    GPIO.Mode(DATA, GPIO.InputPullUp);
    GPIO.PulseIn(DATA, false, 200);             // sensor response low
    GPIO.PulseIn(DATA, true, 200);              // and high
    var bytes = new byte[5];
    for (int i = 0; i < 40; i++)
    {
        int us = GPIO.PulseIn(DATA, true, 200);
        if (us == 0) throw new TimeoutException("no answer from the DHT");
        if (us > 45) bytes[i / 8] |= (byte)(0x80 >> (i % 8));
    }
    if (((bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xFF) != bytes[4]) throw new IOException("checksum");
    double hum = ((bytes[0] << 8) | bytes[1]) / 10.0;
    double t = (((bytes[2] & 0x7F) << 8) | bytes[3]) / 10.0 * ((bytes[2] & 0x80) != 0 ? -1 : 1);
    return new double[] { t, hum };
}

try { var r = ReadDht(); Console.WriteLine($"{r[0]:F1} C  {r[1]:F1} %RH"); }
catch (Exception e) { Console.WriteLine("DHT read failed: " + e.Message); }
`);
T("Sensors", "Light sensor (BH1750)", "bh1750.cs",
  "Illuminance in lux from a BH1750 - a two-byte I2C sensor.", String.raw`
// BH1750 ambient light sensor at 0x23
I2C.Open(0);
var bh = new I2cDevice(0, 0x23);
bh.Write(new byte[] { 0x01 });           // power on
bh.Write(new byte[] { 0x10 });           // continuous high-resolution mode (1 lx)
Thread.Sleep(180);
for (int i = 0; i < 10; i++)
{
    byte[] r = bh.Read(2);
    double lux = ((r[0] << 8) | r[1]) / 1.2;
    Console.WriteLine($"lux:{lux:F0}");
    Thread.Sleep(500);
}
`);
T("Sensors", "Analog joystick", "joystick.cs",
  "Two ADC axes plus a push button, with dead zone and scaling.", String.raw`
// Analog joystick: X/Y on two ADC channels, button to GND
const int AX = 0, AY = 1, BTN = 5;
var button = new Pin(BTN, GPIO.InputPullUp);
int center = 1 << (ADC.Resolution - 1);

int Axis(int ch)
{
    int v = ADC.ReadAverage(ch, 8) - center;
    if (Math.Abs(v) < center / 10) return 0;               // dead zone
    return (int)Math.Clamp(v * 100L / center, -100, 100);   // -100..100
}

for (int i = 0; i < 40; i++)
{
    Console.WriteLine($"x:{Axis(AX)} y:{Axis(AY)} btn:{(button.Read() ? 0 : 1)}");
    Thread.Sleep(100);
}
`);

/* ---------------------------------------------------------------- Displays */
T("Displays", "MAX7219 8x8 LED matrix", "max7219.cs",
  "Drive an 8x8 LED matrix (or 8-digit display) over SPI.", String.raw`
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
`);
T("Displays", "WS2812 / NeoPixel LED strip", "ledstrip.cs",
  "Addressable RGB LEDs (WS2812B, SK6812, on-board RGB LEDs) on any pin with LedStrip: wipe, rainbow, chase.", String.raw`
// WS2812 / WS2812B / SK6812 ("NeoPixel") LEDs on ANY GPIO with the native LedStrip class.
// The bit timing is done by the firmware: RP2040/RP2350 PIO, ESP32 RMT, a tight loop on STM32,
// the led-strip driver on Zephyr. LedStrip comes from the firmware's "ws2812" driver, which
// can be left out of a build: Hal.Has("LedStrip") tells whether this firmware has it.
// On-board RGB LEDs: Waveshare RP2040-Zero / RP2040-Matrix GP16, ESP32-S3-DevKitC GPIO48
// (some v1.0 boards: 38), ESP32-C3/C6 DevKits GPIO8 - or pass "NEOPIXEL" where the port knows it.
// Wiring a strip: DIN -> PIN (330 ohm in series helps), GND -> GND, 5 V -> a supply that can
// feed the LEDs (up to 60 mA per LED at full white).
const int PIN = 16;                   // RP2040-Zero on-board LED
const int COUNT = 1;                  // number of LEDs (1 for an on-board LED)

var strip = new LedStrip(PIN, COUNT); // add LedStrip.RGB for RGB-ordered LEDs, LedStrip.GRBW for SK6812 RGBW
strip.Brightness = 100;                // 0..255, applied in Show() (keeps current and eyes safe)

// 1. colour wipe: red, green, blue
foreach (int color in new[] { LedStrip.Rgb(255, 0, 0), LedStrip.Rgb(0, 255, 0), LedStrip.Rgb(0, 0, 255) })
{
    for (int i = 0; i < COUNT; i++) { strip[i] = color; strip.Show(); Thread.Sleep(COUNT > 1 ? 50 : 400); }
}

// 2. rainbow running along the strip (hue 0..359)
for (int step = 0; step < 360; step += 5)
{
    for (int i = 0; i < COUNT; i++) strip[i] = LedStrip.Hsv((i * 360 / COUNT + step) % 360);
    strip.Show();
    Thread.Sleep(20);
}

// 3. theater chase in warm white (on a single LED: blinks)
for (int round = 0; round < 15; round++)
{
    for (int i = 0; i < COUNT; i++) strip[i] = i % 3 == round % 3 ? LedStrip.Rgb(255, 160, 60) : 0;
    strip.Show();
    Thread.Sleep(100);
}

strip.Clear();                        // all off
strip.Show();
Console.WriteLine($"done: {strip.Count} LEDs on pin {strip.Pin}");
`);
T("Displays", "WS2812 LED strip via SPI (no LedStrip driver)", "ws2812_spi.cs",
  "WS2812B / SK6812 driven through SPI MOSI, for ports without LedStrip: wipe, rainbow, chase.", String.raw`
// WS2812 / WS2812B / SK6812 ("NeoPixel") LEDs through the SPI bus. Each LED bit becomes
// 3 SPI bits at 2.4 MHz (1 -> 110, 0 -> 100), which gives the 0.4 / 0.8 us pulses the LEDs
// expect, so timing is done by the SPI hardware, not by the script.
// Wiring: strip DIN -> MOSI of SPI bus 0 (Pico: GP19), GND -> GND, 5 V -> a supply that can
// feed the LEDs (up to 60 mA per LED at full white). A 330 ohm resistor in the data line helps.
// One SPI.Write sends at most 256 bytes = 28 LEDs; build the firmware with
// -DMCS_HAL_MAX_XFER=1024 for up to 113 LEDs.
const int BUS = 0;
const int COUNT = 8;                  // number of LEDs
const int BRIGHTNESS = 40;            // 0..255 (keeps current and eyes safe)

SPI.Open(BUS, 2_400_000, 0);
var frame = new byte[1 + COUNT * 9];  // 9 SPI bytes per LED; the leading 0 keeps DIN low first
var pixels = new int[COUNT];          // colours as 0xRRGGBB

int Rgb(int r, int g, int b) => (r << 16) | (g << 8) | b;

void Encode(int at, int value)        // one colour byte -> 24 SPI bits (3 bytes)
{
    int bits = 0;
    for (int i = 7; i >= 0; i--) bits = (bits << 3) | (((value >> i) & 1) == 1 ? 6 : 4);
    frame[at] = (byte)((bits >> 16) & 0xFF);
    frame[at + 1] = (byte)((bits >> 8) & 0xFF);
    frame[at + 2] = (byte)(bits & 0xFF);
}

void Show()
{
    for (int i = 0; i < COUNT; i++)
    {
        int c = pixels[i];
        int r = ((c >> 16) & 0xFF) * BRIGHTNESS / 255;
        int g = ((c >> 8) & 0xFF) * BRIGHTNESS / 255;
        int b = (c & 0xFF) * BRIGHTNESS / 255;
        int at = 1 + i * 9;
        Encode(at, g); Encode(at + 3, r); Encode(at + 6, b);   // WS2812 wants G, R, B
    }
    SPI.Write(BUS, frame);
    Thread.Sleep(1);                  // line low > 280 us = latch the new colours
}

void Fill(int color) { for (int i = 0; i < COUNT; i++) pixels[i] = color; }

int Wheel(int pos)                    // 0..255 -> colour around the rainbow
{
    pos &= 255;
    if (pos < 85) return Rgb(255 - pos * 3, pos * 3, 0);
    if (pos < 170) { pos -= 85; return Rgb(0, 255 - pos * 3, pos * 3); }
    pos -= 170;
    return Rgb(pos * 3, 0, 255 - pos * 3);
}

// 1. colour wipe: red, green, blue
foreach (int color in new[] { Rgb(255, 0, 0), Rgb(0, 255, 0), Rgb(0, 0, 255) })
{
    for (int i = 0; i < COUNT; i++) { pixels[i] = color; Show(); Thread.Sleep(50); }
}

// 2. rainbow running along the strip
for (int step = 0; step < 256; step += 4)
{
    for (int i = 0; i < COUNT; i++) pixels[i] = Wheel(i * 256 / COUNT + step);
    Show();
    Thread.Sleep(20);
}

// 3. theater chase in warm white
for (int round = 0; round < 15; round++)
{
    for (int i = 0; i < COUNT; i++) pixels[i] = i % 3 == round % 3 ? Rgb(255, 160, 60) : 0;
    Show();
    Thread.Sleep(100);
}

Fill(0);                              // all off
Show();
Console.WriteLine($"done: {COUNT} LEDs, {frame.Length} SPI bytes per frame");
`);
T("Displays", "Character LCD 16x2 (I2C backpack)", "lcd1602.cs",
  "HD44780 LCD through a PCF8574 I2C backpack (address 0x27).", String.raw`
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
`);
T("Displays", "7-segment display (TM1637)", "tm1637.cs",
  "Show numbers on a 4-digit TM1637 module (two-wire, bit-banged).", String.raw`
// TM1637 4-digit display: CLK and DIO on two GPIOs (bit-banged protocol)
const int CLK = 18, DIO = 19;
byte[] digits = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
GPIO.Mode(CLK, GPIO.Output); GPIO.Mode(DIO, GPIO.Output);

void Bit(int clk, int dio) { GPIO.Write(DIO, dio); GPIO.Write(CLK, clk); Hal.DelayMicroseconds(5); }
void Start() { Bit(1, 1); Bit(1, 0); Bit(0, 0); }
void Stop() { Bit(0, 0); Bit(1, 0); Bit(1, 1); }
void Byte(int b)
{
    for (int i = 0; i < 8; i++) { Bit(0, (b >> i) & 1); Bit(1, (b >> i) & 1); }
    Bit(0, 1); Bit(1, 1); Bit(0, 1);                 // ack clock
}
void Show(int value, bool colon)
{
    Start(); Byte(0x40); Stop();                     // auto-increment
    Start(); Byte(0xC0);
    for (int i = 3; i >= 0; i--)
    {
        int d = digits[(value / (int)Math.Pow(10, i)) % 10];
        Byte(i == 2 && colon ? d | 0x80 : d);
    }
    Stop();
    Start(); Byte(0x88 | 4); Stop();                 // display on, brightness 4
}

for (int s = 0; s < 10; s++) { Show(1200 + s, s % 2 == 0); Thread.Sleep(500); }
`);
T("Displays", "Text console on an SSD1306 OLED", "oled_text.cs",
  "A tiny 5x7 font renderer for a 128x64 SSD1306 - prints lines of text.", String.raw`
// SSD1306 128x64 OLED at 0x3C: a minimal text renderer (digits, A-Z, a few signs)
I2C.Open(0, 400000);
var oled = new I2cDevice(0, 0x3C);
void Cmds(params int[] c) { foreach (int x in c) oled.Write(new byte[] { 0x00, (byte)x }); }
Cmds(0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
     0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF);

var font = new Dictionary<char, int[]> {
    ['0'] = new[] { 0x3E, 0x51, 0x49, 0x45, 0x3E }, ['1'] = new[] { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    ['2'] = new[] { 0x42, 0x61, 0x51, 0x49, 0x46 }, ['3'] = new[] { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    ['C'] = new[] { 0x3E, 0x41, 0x41, 0x41, 0x22 }, ['M'] = new[] { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
    ['I'] = new[] { 0x00, 0x41, 0x7F, 0x41, 0x00 }, ['R'] = new[] { 0x7F, 0x09, 0x19, 0x29, 0x46 },
    ['O'] = new[] { 0x3E, 0x41, 0x41, 0x41, 0x3E }, ['S'] = new[] { 0x46, 0x49, 0x49, 0x49, 0x31 },
    [':'] = new[] { 0x00, 0x36, 0x36, 0x00, 0x00 }, [' '] = new[] { 0, 0, 0, 0, 0 },
};

void Print(int page, string text)
{
    Cmds(0x21, 0, 127, 0x22, page, page);              // column and page window
    var row = new List<byte> { 0x40 };
    foreach (char ch in text.ToUpper())
    {
        int[] g = font.ContainsKey(ch) ? font[ch] : new[] { 0x7F, 0x41, 0x41, 0x41, 0x7F };
        foreach (int col in g) row.Add((byte)col);
        row.Add(0);
    }
    oled.Write(row.ToArray());
}

Print(0, "MICROCS");
Print(2, "OS: 123");
Console.WriteLine("text written to the OLED");
`);

/* ---------------------------------------------------------------- Motors and actuators */
T("Motors and actuators", "DC motor with an H-bridge", "motor.cs",
  "Speed and direction for an L298N / TB6612 / DRV8833 driver.", String.raw`
// DC motor on an H-bridge: IN1/IN2 set the direction, PWM sets the speed
const int IN1 = 25, IN2 = 26, PWM_CH = 0;     // PWM channel 0 drives the ENA pin
GPIO.Mode(IN1, GPIO.Output); GPIO.Mode(IN2, GPIO.Output);

void Drive(double speed)                       // -1.0 (full reverse) .. 1.0 (full forward)
{
    speed = Math.Clamp(speed, -1.0, 1.0);
    GPIO.Write(IN1, speed > 0); GPIO.Write(IN2, speed < 0);
    PWM.Set(PWM_CH, 20000, Math.Abs(speed));   // 20 kHz: silent
}
void Brake() { GPIO.Write(IN1, 1); GPIO.Write(IN2, 1); PWM.Set(PWM_CH, 20000, 1.0); }

for (double s = 0; s <= 1.0; s += 0.25) { Drive(s); Console.WriteLine($"forward {s:P0}"); Thread.Sleep(400); }
Brake(); Thread.Sleep(300);
for (double s = 0; s >= -1.0; s -= 0.5) { Drive(s); Console.WriteLine($"reverse {-s:P0}"); Thread.Sleep(400); }
Drive(0); PWM.Stop(PWM_CH);
`);
T("Motors and actuators", "Stepper motor (A4988 / DRV8825)", "stepper.cs",
  "STEP/DIR stepper driver with acceleration.", String.raw`
// Stepper driver with STEP / DIR / EN pins (A4988, DRV8825, TMC2208 in step mode)
const int STEP = 32, DIR = 33, EN = 27;
const int STEPS_PER_REV = 200 * 16;            // 1.8 deg motor, 1/16 microstepping
GPIO.Mode(STEP, GPIO.Output); GPIO.Mode(DIR, GPIO.Output); GPIO.Mode(EN, GPIO.Output);
GPIO.Write(EN, 0);                              // enable (active low)

void Move(int steps, int maxSpeed /* steps/s */)
{
    GPIO.Write(DIR, steps > 0);
    int n = Math.Abs(steps), ramp = Math.Min(n / 2, 400);
    for (int i = 0; i < n; i++)
    {
        int edge = Math.Min(i, n - 1 - i);                     // distance to start / end
        double speed = edge < ramp ? maxSpeed * (0.2 + 0.8 * edge / ramp) : maxSpeed;
        GPIO.Write(STEP, 1); Hal.DelayMicroseconds(3); GPIO.Write(STEP, 0);
        Hal.DelayMicroseconds((int)(1_000_000 / speed));
    }
}

Move(STEPS_PER_REV / 4, 4000);   Console.WriteLine("quarter turn forward");
Move(-STEPS_PER_REV / 4, 4000);  Console.WriteLine("and back");
GPIO.Write(EN, 1);
`);
T("Motors and actuators", "28BYJ-48 stepper (ULN2003)", "uln2003.cs",
  "Half-step sequence for the cheap 28BYJ-48 geared stepper.", String.raw`
// 28BYJ-48 with a ULN2003 board: four coil pins, half-step sequence (4096 steps / turn)
int[] pins = { 13, 12, 14, 27 };
foreach (int p in pins) GPIO.Mode(p, GPIO.Output);
int[][] seq = { new[] {1,0,0,0}, new[] {1,1,0,0}, new[] {0,1,0,0}, new[] {0,1,1,0},
                new[] {0,0,1,0}, new[] {0,0,1,1}, new[] {0,0,0,1}, new[] {1,0,0,1} };
int phase = 0;

void Step(int count, int delayMs)
{
    int dir = count > 0 ? 1 : -1;
    for (int s = 0; s < Math.Abs(count); s++)
    {
        phase = (phase + dir + 8) % 8;
        for (int c = 0; c < 4; c++) GPIO.Write(pins[c], seq[phase][c]);
        Thread.Sleep(delayMs);
    }
    foreach (int p in pins) GPIO.Write(p, 0);   // coils off: no heat
}

Step(512, 2); Console.WriteLine("45 degrees clockwise");
Step(-512, 2); Console.WriteLine("and back");
`);
T("Motors and actuators", "Relay with safety timeout", "relay.cs",
  "Switch a relay and make sure it turns off again, even on errors.", String.raw`
// Relay module: active-low input on most boards. The finally block guarantees "off".
const int RELAY = 23;
var relay = new Pin(RELAY, GPIO.Output);
void Set(bool on) { relay.Write(!on); Console.WriteLine(on ? "relay ON" : "relay off"); }

Set(false);
try
{
    Set(true);
    var sw = Stopwatch.StartNew();
    while (sw.ElapsedMilliseconds < 2000)      // maximum on-time
    {
        // ... watch a sensor here, break when done
        Thread.Sleep(100);
    }
}
finally { Set(false); }
`);
T("Motors and actuators", "Servo with smooth motion", "servo_smooth.cs",
  "Ease a servo between positions instead of jumping.", String.raw`
// Smooth servo motion: ease-in-out between angles
const int CH = 0;
double pos = 90;
PWM.Servo(CH, pos);

void MoveTo(double target, int ms)
{
    double start = pos; int steps = ms / 20;
    for (int i = 1; i <= steps; i++)
    {
        double t = (double)i / steps;
        double e = t < 0.5 ? 2 * t * t : 1 - Math.Pow(-2 * t + 2, 2) / 2;   // ease in-out
        pos = start + (target - start) * e;
        PWM.Servo(CH, pos);
        Thread.Sleep(20);                       // one servo frame
    }
}

foreach (double a in new double[] { 0, 180, 45, 135, 90 }) { MoveTo(a, 600); Console.WriteLine($"at {a} deg"); }
PWM.Stop(CH);
`);

/* ---------------------------------------------------------------- Input devices */
T("Input devices", "Rotary encoder", "encoder.cs",
  "Quadrature decoding with pin interrupts, plus the push button.", String.raw`
// KY-040 rotary encoder: CLK (A), DT (B), SW. Counts detents with an interrupt on A.
const int A = 32, B = 33, SW = 25;
var pinA = new Pin(A, GPIO.InputPullUp);
var pinB = new Pin(B, GPIO.InputPullUp);
var sw = new Pin(SW, GPIO.InputPullUp);
int position = 0, last = 0;

pinA.OnChange(GPIO.Falling, (bool level) => { position += pinB.Read() ? 1 : -1; });
sw.OnChange(GPIO.Falling, (bool level) => { position = 0; Console.WriteLine("reset"); });

for (int i = 0; i < 100; i++)                  // 10 s
{
    if (position != last) { Console.WriteLine($"position:{position}"); last = position; }
    Thread.Sleep(100);                          // callbacks run during Sleep
}
`);
T("Input devices", "4x4 matrix keypad", "keypad.cs",
  "Scan a membrane keypad: rows as outputs, columns with pull-ups.", String.raw`
// 4x4 keypad: drive one row low at a time and read the columns
int[] rows = { 13, 12, 14, 27 }, cols = { 26, 25, 33, 32 };
string[] keys = { "123A", "456B", "789C", "*0#D" };
foreach (int r in rows) { GPIO.Mode(r, GPIO.Output); GPIO.Write(r, 1); }
foreach (int c in cols) GPIO.Mode(c, GPIO.InputPullUp);

char? Scan()
{
    for (int r = 0; r < 4; r++)
    {
        GPIO.Write(rows[r], 0);
        for (int c = 0; c < 4; c++)
            if (!GPIO.Read(cols[c])) { GPIO.Write(rows[r], 1); return keys[r][c]; }
        GPIO.Write(rows[r], 1);
    }
    return null;
}

var code = new StringBuilder();
char? prev = null;
for (int i = 0; i < 300; i++)                    // ~6 s
{
    char? k = Scan();
    if (k != null && k != prev)
    {
        if (k == '#') { Console.WriteLine(code.ToString() == "1234" ? "unlocked" : "wrong code"); code.Clear(); }
        else code.Append(k);
    }
    prev = k;
    Thread.Sleep(20);
}
`);
T("Input devices", "Button: click, double-click, long press", "button_events.cs",
  "Turn a raw button into click / double / long-press events.", String.raw`
// Button gestures from one input: click, double-click and long press
var btn = new Pin(0, GPIO.InputPullUp);         // BOOT button on most ESP32 boards
long downAt = 0, lastClick = -1000;
bool wasDown = false, pendingClick = false;

for (int i = 0; i < 500; i++)                   // 10 s
{
    bool down = !btn.Read();
    long now = Environment.TickCount;
    if (down && !wasDown) downAt = now;
    if (!down && wasDown)
    {
        long held = now - downAt;
        if (held > 800) Console.WriteLine("long press");
        else if (now - lastClick < 350) { Console.WriteLine("double click"); pendingClick = false; lastClick = -1000; }
        else { pendingClick = true; lastClick = now; }
    }
    if (pendingClick && now - lastClick >= 350) { Console.WriteLine("click"); pendingClick = false; }
    wasDown = down;
    Thread.Sleep(20);
}
`);
T("Input devices", "Touch / capacitive pad (charge time)", "touch.cs",
  "Measure how long a pin takes to charge - a DIY touch sensor.", String.raw`
// Charge-time touch sensor: a wire / foil on a pin, 1M resistor to a "send" pin.
// Touching adds capacitance, so the pin takes longer to read high.
const int SEND = 26, SENSE = 27;
GPIO.Mode(SEND, GPIO.Output);

int Measure()
{
    GPIO.Mode(SENSE, GPIO.Output); GPIO.Write(SENSE, 0); GPIO.Write(SEND, 0);
    Hal.DelayMicroseconds(10);
    GPIO.Mode(SENSE, GPIO.Input);
    long t0 = Hal.Micros;
    GPIO.Write(SEND, 1);
    while (!GPIO.Read(SENSE) && Hal.Micros - t0 < 2000) { }
    return (int)(Hal.Micros - t0);
}

int baseline = 0;
for (int i = 0; i < 8; i++) baseline += Measure();
baseline /= 8;
for (int i = 0; i < 30; i++)
{
    int us = Measure();
    Console.WriteLine($"us:{us} touched:{(us > baseline * 2 ? 1 : 0)}");
    Thread.Sleep(100);
}
`);

/* ---------------------------------------------------------------- Control and filters */
T("Control and filters", "PID controller", "pid.cs",
  "A reusable PID class driving a simulated heater to a set point.", String.raw`
// PID controller (with anti-windup) - here it drives a simulated heater model
class Pid
{
    public double Kp, Ki, Kd, Min = 0, Max = 1;
    double integral, last; bool first = true;
    public Pid(double kp, double ki, double kd) { Kp = kp; Ki = ki; Kd = kd; }
    public double Update(double setPoint, double measured, double dt)
    {
        double err = setPoint - measured;
        double deriv = first ? 0 : (measured - last) / dt;     // derivative on measurement: no kick
        first = false; last = measured;
        double outRaw = Kp * err + Ki * (integral + err * dt) - Kd * deriv;
        if (outRaw > Min && outRaw < Max) integral += err * dt; // anti-windup
        return Math.Clamp(outRaw, Min, Max);
    }
}

var pid = new Pid(0.08, 0.02, 0.05);
double temp = 20, setPoint = 60, dt = 0.5;
for (int i = 0; i < 80; i++)
{
    double power = pid.Update(setPoint, temp, dt);           // 0..1 -> PWM.Set(0, 1000, power)
    temp += (power * 8 - (temp - 20) * 0.05) * dt;           // heater model
    if (i % 4 == 0) Console.WriteLine($"setpoint:{setPoint} temp:{temp:F2} power:{power * 100:F0}");
}
`);
T("Control and filters", "Moving average and median filters", "filters.cs",
  "Smooth noisy readings: moving average, median of 5, exponential.", String.raw`
// Three classic filters for noisy sensor values
class MovingAverage
{
    readonly double[] buf; int n, i; double sum;
    public MovingAverage(int size) { buf = new double[size]; }
    public double Add(double v) { sum += v - buf[i]; buf[i] = v; i = (i + 1) % buf.Length; if (n < buf.Length) n++; return sum / n; }
}
class Median
{
    readonly List<double> win = new List<double>(); readonly int size;
    public Median(int size) { this.size = size; }
    public double Add(double v) { win.Add(v); if (win.Count > size) win.RemoveAt(0); var s = win.Order().ToList(); return s[s.Count / 2]; }
}
class Ema
{
    readonly double alpha; double y; bool init;
    public Ema(double alpha) { this.alpha = alpha; }
    public double Add(double v) { y = init ? y + alpha * (v - y) : v; init = true; return y; }
}

var rnd = new Random(1);
var avg = new MovingAverage(8); var med = new Median(5); var ema = new Ema(0.2);
for (int t = 0; t < 60; t++)
{
    double raw = 50 + 10 * Math.Sin(t / 8.0) + (rnd.NextDouble() - 0.5) * 8 + (t % 17 == 0 ? 40 : 0);  // noise + spikes
    Console.WriteLine($"raw:{raw:F1} avg:{avg.Add(raw):F1} median:{med.Add(raw):F1} ema:{ema.Add(raw):F1}");
}
`);
T("Control and filters", "Thermostat with hysteresis", "thermostat.cs",
  "On/off control with a dead band so the relay doesn't chatter.", String.raw`
// Bang-bang control with hysteresis (a simulated room)
const double SET = 21.0, BAND = 0.5;
bool heating = false;
double room = 18.0;
var relay = new Pin(23, GPIO.Output);

for (int minute = 0; minute < 120; minute++)
{
    if (room < SET - BAND) heating = true;
    else if (room > SET + BAND) heating = false;   // between the limits: keep the state
    relay.Write(heating);
    room += heating ? 0.12 : -0.05;               // room model
    if (minute % 5 == 0) Console.WriteLine($"room:{room:F2} heating:{(heating ? 1 : 0)}");
}
`);
T("Control and filters", "Kalman filter (1-D)", "kalman.cs",
  "A one-dimensional Kalman filter for a slowly changing value.", String.raw`
// 1-D Kalman filter: estimate a value from noisy measurements
class Kalman
{
    double x, p = 1;                  // estimate and its variance
    readonly double q, r;             // process noise, measurement noise
    public Kalman(double q, double r, double initial) { this.q = q; this.r = r; x = initial; }
    public double Update(double z)
    {
        p += q;                       // predict
        double k = p / (p + r);       // gain
        x += k * (z - x);             // correct
        p *= 1 - k;
        return x;
    }
}

var kf = new Kalman(0.01, 4, 25);
var rnd = new Random(7);
for (int i = 0; i < 50; i++)
{
    double truth = 25 + i * 0.05;
    double z = truth + (rnd.NextDouble() - 0.5) * 6;
    Console.WriteLine($"truth:{truth:F2} measured:{z:F2} kalman:{kf.Update(z):F2}");
}
`);

/* ---------------------------------------------------------------- Protocols and data */
T("Protocols and data", "CRC-8 / CRC-16 / CRC-32", "crc.cs",
  "Checksums used by sensors (CRC-8), Modbus (CRC-16) and files (CRC-32).", String.raw`
// CRC algorithms used in embedded protocols
int Crc8(byte[] data, int poly = 0x31, int init = 0xFF)          // Sensirion / Dallas style
{
    int crc = init;
    foreach (byte b in data) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 0x80) != 0 ? ((crc << 1) ^ poly) & 0xFF : (crc << 1) & 0xFF; }
    return crc;
}
int Crc16Modbus(byte[] data)
{
    int crc = 0xFFFF;
    foreach (byte b in data) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1; }
    return crc;
}
int Crc32(byte[] data)                      // MicroCS integers are 32-bit signed: >>> is the logical shift
{
    int crc = -1;
    foreach (byte b in data) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >>> 1) ^ unchecked((int)0xEDB88320) : crc >>> 1; }
    return ~crc;
}

var text = Encoding.ASCII.GetBytes("123456789");                 // the standard check string
Console.WriteLine($"CRC-8   0x{Crc8(new byte[] { 0xBE, 0xEF }):X2}  (Sensirion example: 0x92)");
Console.WriteLine($"CRC-16  0x{Crc16Modbus(text):X4}  (Modbus check: 0x4B37)");
Console.WriteLine($"CRC-32  0x{Crc32(text):X8}  (check: 0xCBF43926)");
`);
T("Protocols and data", "Modbus RTU master (read registers)", "modbus.cs",
  "Build a Modbus RTU request, send it on a UART (RS-485) and parse the reply.", String.raw`
// Modbus RTU: read holding registers (function 3) from slave 1 over RS-485 (UART 1)
const int PORT = 1;
UART.Open(PORT, 9600);

int Crc16(List<byte> d)
{
    int crc = 0xFFFF;
    foreach (byte b in d) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1; }
    return crc;
}

int[] ReadHolding(int slave, int start, int count)
{
    var req = new List<byte> { (byte)slave, 3, (byte)(start >> 8), (byte)start, (byte)(count >> 8), (byte)count };
    int crc = Crc16(req);
    req.Add((byte)crc); req.Add((byte)(crc >> 8));
    UART.Write(PORT, req.ToArray());
    byte[] resp = UART.Read(PORT, 5 + 2 * count, 300);
    if (resp.Length < 5 + 2 * count) throw new TimeoutException($"Modbus: {resp.Length} bytes received");
    var body = new List<byte>(resp.Take(resp.Length - 2));
    if ((Crc16(body) & 0xFFFF) != (resp[resp.Length - 2] | (resp[resp.Length - 1] << 8))) throw new IOException("Modbus: bad CRC");
    var regs = new int[count];
    for (int i = 0; i < count; i++) regs[i] = (resp[3 + 2 * i] << 8) | resp[4 + 2 * i];
    return regs;
}

try { Console.WriteLine("registers: " + string.Join(", ", ReadHolding(1, 0, 4))); }
catch (Exception e) { Console.WriteLine(e.Message); }
`);
T("Protocols and data", "NMEA checksum and fields", "nmea.cs",
  "Validate an NMEA sentence and pick out the fields.", String.raw`
// NMEA 0183: "$...*hh" - XOR of the characters between $ and *
bool Valid(string s)
{
    int star = s.IndexOf('*');
    if (!s.StartsWith("$") || star < 0) return false;
    int x = 0;
    for (int i = 1; i < star; i++) x ^= s[i];
    return x == Convert.ToInt32(s.Substring(star + 1, 2), 16);
}
double Degrees(string v, string hemi)                 // ddmm.mmmm -> decimal degrees
{
    double raw = double.Parse(v);
    double deg = Math.Floor(raw / 100) + (raw % 100) / 60;
    return hemi == "S" || hemi == "W" ? -deg : deg;
}

string line = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
Console.WriteLine("checksum ok: " + Valid(line));
string[] f = line.Split('*')[0].Split(',');
Console.WriteLine($"time {f[1]}  fix {(f[2] == "A" ? "valid" : "none")}  lat {Degrees(f[3], f[4]):F5}  lon {Degrees(f[5], f[6]):F5}  speed {double.Parse(f[7]) * 1.852:F1} km/h");
`);
T("Protocols and data", "Base64 and hex encoding", "base64.cs",
  "Encode / decode Base64 and hex strings by hand.", String.raw`
// Base64 and hex helpers (no Convert.ToBase64String in MicroCS - here is one)
const string B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string ToBase64(byte[] d)
{
    var sb = new StringBuilder();
    for (int i = 0; i < d.Length; i += 3)
    {
        int n = d[i] << 16 | (i + 1 < d.Length ? d[i + 1] << 8 : 0) | (i + 2 < d.Length ? d[i + 2] : 0);
        sb.Append(B64[(n >> 18) & 63]).Append(B64[(n >> 12) & 63]);
        sb.Append(i + 1 < d.Length ? B64[(n >> 6) & 63] : '=');
        sb.Append(i + 2 < d.Length ? B64[n & 63] : '=');
    }
    return sb.ToString();
}
byte[] FromBase64(string s)
{
    var outp = new List<byte>(); int buf = 0, bits = 0;
    foreach (char c in s)
    {
        if (c == '=') break;
        buf = (buf << 6) | B64.IndexOf(c); bits += 6;
        if (bits >= 8) { bits -= 8; outp.Add((byte)((buf >> bits) & 0xFF)); }
    }
    return outp.ToArray();
}
string ToHex(byte[] d) => string.Concat(d.Select(b => b.ToString("x2")));

byte[] data = Encoding.UTF8.GetBytes("MicroCS!");
string b64 = ToBase64(data);
Console.WriteLine($"{b64}  ->  {Encoding.UTF8.GetString(FromBase64(b64))}");
Console.WriteLine("hex: " + ToHex(data) + "   BitConverter: " + BitConverter.ToString(data));
`);
T("Protocols and data", "Command interpreter on the console", "commands.cs",
  "Parse typed commands (led on, pwm 50, help) - a device CLI.", String.raw`
// A small command shell: type commands in the console (C# tab) after Run.
var led = new Pin(2, GPIO.Output);
var commands = new Dictionary<string, Action<string[]>>
{
    ["help"] = a => Console.WriteLine("commands: led on|off, blink <n>, pwm <0-100>, uptime, quit"),
    ["led"] = a => { led.Write(a.Length > 1 && a[1] == "on"); Console.WriteLine("led " + (led.Read() ? "on" : "off")); },
    ["blink"] = a => { int n = a.Length > 1 ? int.Parse(a[1]) : 3; for (int i = 0; i < n * 2; i++) { led.Toggle(); Thread.Sleep(150); } },
    ["pwm"] = a => { PWM.Set(0, 1000, int.Parse(a[1]) / 100.0); Console.WriteLine("duty " + a[1] + "%"); },
    ["uptime"] = a => Console.WriteLine($"{Environment.TickCount / 1000} s"),
};

Console.WriteLine("type 'help'");
for (;;)
{
    string line = Console.ReadLine();
    if (line == null || line.Trim() == "quit") break;
    string[] args = line.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
    if (args.Length == 0) continue;
    try
    {
        if (commands.TryGetValue(args[0].ToLower(), out var cmd)) cmd(args);
        else Console.WriteLine("unknown command - try 'help'");
    }
    catch (Exception e) { Console.WriteLine("error: " + e.Message); }
}
`);
T("Protocols and data", "JSON-style output (manual)", "json_out.cs",
  "Build JSON text for a host program or MQTT bridge.", String.raw`
// Build JSON by hand (MicroCS has no JSON library) - escape strings properly
string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n");
string Json(Dictionary<string, object> d)
{
    var parts = d.Select(kv => $"\"{Esc(kv.Key)}\":" + kv.Value switch
    {
        null => "null",
        string s => $"\"{Esc(s)}\"",
        bool b => b ? "true" : "false",
        double x => x.ToString("F3"),
        _ => kv.Value.ToString(),
    });
    return "{" + string.Join(",", parts) + "}";
}

for (int i = 0; i < 3; i++)
{
    var msg = new Dictionary<string, object>
    {
        ["board"] = Hal.Board, ["uptime"] = Environment.TickCount, ["adc"] = ADC.Read(0),
        ["volts"] = ADC.ReadVoltage(0), ["ok"] = true, ["note"] = "line \"" + i + "\"",
    };
    Console.WriteLine(Json(msg));
    Thread.Sleep(500);
}
`);
T("Protocols and data", "Ring buffer (circular queue)", "ringbuffer.cs",
  "A fixed-size buffer for samples - no allocation per sample.", String.raw`
// Fixed-size ring buffer: the newest N samples, constant memory
class Ring
{
    readonly int[] data; int head, count;
    public Ring(int size) { data = new int[size]; }
    public int Count => count;
    public void Push(int v) { data[head] = v; head = (head + 1) % data.Length; if (count < data.Length) count++; }
    public int this[int i] => data[(head - count + i + data.Length * 2) % data.Length];   // 0 = oldest
    public double Average() { long s = 0; for (int i = 0; i < count; i++) s += this[i]; return count > 0 ? (double)s / count : 0; }
}

var ring = new Ring(16);
for (int i = 0; i < 40; i++)
{
    ring.Push(ADC.Read(0));
    Thread.Sleep(25);
}
Console.WriteLine($"{ring.Count} samples, oldest {ring[0]}, newest {ring[ring.Count - 1]}, average {ring.Average():F1}");
`);

/* ---------------------------------------------------------------- Serial plotter */
T("Serial plotter", "Plot a sine and cosine", "plot_sine.cs",
  "Named values on each line - open Console → Plotter to see them live.", String.raw`
// Open the console's "Plotter" view, then Run: each line is one sample, "name:value" pairs are series.
for (int i = 0; i < 400; i++)
{
    double t = i / 20.0;
    Console.WriteLine($"sin:{Math.Sin(t):F3} cos:{Math.Cos(t):F3} mix:{0.5 * Math.Sin(3 * t) + 0.3 * Math.Cos(t):F3}");
    Thread.Sleep(20);
}
`);
T("Serial plotter", "Plot analog inputs", "plot_adc.cs",
  "Stream two ADC channels to the plotter.", String.raw`
// Two analog inputs, 20 samples per second - watch them in Console -> Plotter
for (int i = 0; i < 300; i++)
{
    Console.WriteLine($"a0:{ADC.ReadMillivolts(0)} a1:{ADC.ReadMillivolts(1)}");
    Thread.Sleep(50);
}
`);
T("Serial plotter", "Plot a step response", "plot_step.cs",
  "A second-order system reacting to a set-point step - classic control demo.", String.raw`
// Step response of a damped spring (second-order system) - for the Plotter view
double x = 0, v = 0, target = 0;
const double k = 4.0, c = 0.8, dt = 0.05;
for (int i = 0; i < 300; i++)
{
    if (i == 20) target = 1.0;
    if (i == 160) target = -0.5;
    double a = k * (target - x) - c * v;
    v += a * dt; x += v * dt;
    Console.WriteLine($"target:{target:F2} position:{x:F3}");
    Thread.Sleep(15);
}
`);

/* ---------------------------------------------------------------- Fun and games */
T("Fun and games", "Morse code blinker", "morse.cs",
  "Blink any text in Morse code on the LED (and print it).", String.raw`
// Morse code on an LED: dot = 1 unit, dash = 3, gaps 1 / 3 / 7 units
var led = new Pin(2, GPIO.Output);
const int UNIT = 120;
var code = new Dictionary<char, string> {
    ['A'] = ".-", ['B'] = "-...", ['C'] = "-.-.", ['D'] = "-..", ['E'] = ".", ['F'] = "..-.", ['G'] = "--.", ['H'] = "....",
    ['I'] = "..", ['J'] = ".---", ['K'] = "-.-", ['L'] = ".-..", ['M'] = "--", ['N'] = "-.", ['O'] = "---", ['P'] = ".--.",
    ['Q'] = "--.-", ['R'] = ".-.", ['S'] = "...", ['T'] = "-", ['U'] = "..-", ['V'] = "...-", ['W'] = ".--", ['X'] = "-..-",
    ['Y'] = "-.--", ['Z'] = "--..", ['0'] = "-----", ['1'] = ".----", ['2'] = "..---", ['3'] = "...--", ['4'] = "....-",
    ['5'] = ".....", ['6'] = "-....", ['7'] = "--...", ['8'] = "---..", ['9'] = "----.",
};

void Send(string text)
{
    foreach (char ch in text.ToUpper())
    {
        if (ch == ' ') { Thread.Sleep(UNIT * 7); Console.Write(" / "); continue; }
        if (!code.TryGetValue(ch, out var sym)) continue;
        foreach (char s in sym) { led.High(); Thread.Sleep(s == '.' ? UNIT : UNIT * 3); led.Low(); Thread.Sleep(UNIT); }
        Console.Write(sym + " ");
        Thread.Sleep(UNIT * 2);
    }
    Console.WriteLine();
}

Send("SOS MicroCS");
`);
T("Fun and games", "Reaction time game", "reaction.cs",
  "LED turns on after a random delay - press the button as fast as you can.", String.raw`
// Reaction game: wait for the LED, then press the button
var led = new Pin(2, GPIO.Output);
var btn = new Pin(0, GPIO.InputPullUp);
var rnd = new Random();
var times = new List<long>();

for (int round = 1; round <= 3; round++)
{
    Console.WriteLine($"round {round}: get ready...");
    Thread.Sleep(1000 + rnd.Next(2000));
    if (!btn.Read()) { Console.WriteLine("too early!"); continue; }
    led.High();
    var sw = Stopwatch.StartNew();
    while (btn.Read() && sw.ElapsedMilliseconds < 3000) { }
    led.Low();
    if (sw.ElapsedMilliseconds >= 3000) { Console.WriteLine("too slow (3 s)"); continue; }
    times.Add(sw.ElapsedMilliseconds);
    Console.WriteLine($"{sw.ElapsedMilliseconds} ms");
}
if (times.Count > 0) Console.WriteLine($"best {times.Min()} ms, average {times.Average():F0} ms");
`);
T("Fun and games", "Dice roller with LEDs", "dice.cs",
  "Roll a die on a button press and show it on 7 LEDs (or the console).", String.raw`
// Electronic die: 7 LEDs in the classic pattern (a..g), button to roll
int[] leds = { 13, 12, 14, 27, 26, 25, 33 };
//  a . b      positions:  a=top-left  b=top-right  c=mid-left  d=center
//  c d e                  e=mid-right f=bottom-left g=bottom-right
//  f . g
string[] faces = { "d", "ag", "adg", "abfg", "abdfg", "abcefg" };
foreach (int p in leds) GPIO.Mode(p, GPIO.Output);
var rnd = new Random();

void Show(int value)
{
    string on = faces[value - 1];
    for (int i = 0; i < 7; i++) GPIO.Write(leds[i], on.Contains((char)('a' + i)));
    string Dot(char c) => on.Contains(c) ? "o" : ".";
    Console.WriteLine($"{Dot('a')} . {Dot('b')}\n{Dot('c')} {Dot('d')} {Dot('e')}\n{Dot('f')} . {Dot('g')}\n");
}

for (int roll = 0; roll < 3; roll++)
{
    for (int spin = 0; spin < 6; spin++) { foreach (int p in leds) GPIO.Write(p, rnd.Next(2)); Thread.Sleep(60 + spin * 30); }
    int v = rnd.Next(1, 7);
    Console.WriteLine($"rolled {v}");
    Show(v);
    Thread.Sleep(800);
}
`);
T("Fun and games", "Conway's Game of Life", "life.cs",
  "Cellular automaton on a 16x16 grid printed to the console.", String.raw`
// Game of Life on a 16x16 torus - prints a few generations
const int N = 16;
bool[][] NewGrid() { var g = new bool[N][]; for (int i = 0; i < N; i++) g[i] = new bool[N]; return g; }
var grid = NewGrid();
var rnd = new Random(42);
for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) grid[y][x] = rnd.Next(4) == 0;

int Neighbours(int y, int x)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dy != 0 || dx != 0) && grid[(y + dy + N) % N][(x + dx + N) % N]) n++;
    return n;
}

for (int gen = 0; gen < 5; gen++)
{
    var sb = new StringBuilder($"generation {gen}\n");
    for (int y = 0; y < N; y++) { for (int x = 0; x < N; x++) sb.Append(grid[y][x] ? "#" : "."); sb.Append('\n'); }
    Console.WriteLine(sb.ToString());
    var next = NewGrid();
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) { int n = Neighbours(y, x); next[y][x] = n == 3 || (n == 2 && grid[y][x]); }
    grid = next;
}
`);

/* ---------------------------------------------------------------- Benchmarks */
T("Benchmarks", "CPU benchmark", "bench_cpu.cs",
  "Integer, floating point, string and collection speed of the board.", String.raw`
// Quick speed test - compare boards (and builds) with the same script.
// Sizes fit the heap of small boards (RP2040: 160 KB); double math is slow on chips without a double FPU (RP2040, Cortex-M0/M4).
long Time(string name, Action body)
{
    GC.Collect();
    var sw = Stopwatch.StartNew();
    body();
    Console.WriteLine($"{name,-22} {sw.ElapsedMilliseconds,6} ms");
    return sw.ElapsedMilliseconds;
}

long total = 0;
total += Time("integer loop 100k", () => { int s = 0; for (int i = 0; i < 100_000; i++) s += i & 7; });
total += Time("double math 20k", () => { double s = 0; for (int i = 1; i < 20_000; i++) s += Math.Sqrt(i) * Math.Sin(i); });
total += Time("method calls 50k", () => { int F(int x) => x + 1; int s = 0; for (int i = 0; i < 50_000; i++) s = F(s); });
total += Time("string builder 5k", () => { var sb = new StringBuilder(); for (int i = 0; i < 5000; i++) sb.Append(i); });
total += Time("List<int> 4k", () => { var l = new List<int>(); for (int i = 0; i < 4_000; i++) l.Add(4_000 - i); l.Sort(); });
total += Time("Dictionary 2k", () => { var d = new Dictionary<int, int>(); for (int i = 0; i < 2000; i++) d[i] = i; for (int i = 0; i < 2000; i++) _ = d[i]; });
Console.WriteLine($"{"total",-22} {total,6} ms  ({Hal.Board}, {Hal.CpuHz / 1_000_000} MHz)");
`);
T("Benchmarks", "GPIO toggle speed", "bench_gpio.cs",
  "How fast can a script toggle a pin? Static calls vs. a Pin object.", String.raw`
// GPIO speed from C#: static call vs. Pin object (measure the pin with a scope too)
const int P = 2, N = 20_000;
GPIO.Mode(P, GPIO.Output);
var pin = new Pin(P, GPIO.Output);

var sw = Stopwatch.StartNew();
for (int i = 0; i < N; i++) GPIO.Toggle(P);
double a = sw.ElapsedMilliseconds;
sw.Restart();
for (int i = 0; i < N; i++) pin.Toggle();
double b = sw.ElapsedMilliseconds;
Console.WriteLine($"GPIO.Toggle: {N / Math.Max(a, 1):F0} kHz toggles   Pin.Toggle: {N / Math.Max(b, 1):F0} kHz toggles");
`);

const SNIPPETS = [
  { label: "cw", detail: "Console.WriteLine", text: "Console.WriteLine($0);" },
  { label: "cwi", detail: "Console.WriteLine($\"...\")", text: "Console.WriteLine($\"$0\");" },
  { label: "for", detail: "for loop", text: "for (int i = 0; i < $0; i++)\n{\n    \n}" },
  { label: "forr", detail: "reverse for loop", text: "for (int i = $0 - 1; i >= 0; i--)\n{\n    \n}" },
  { label: "foreach", detail: "foreach loop", text: "foreach (var item in $0)\n{\n    \n}" },
  { label: "while", detail: "while loop", text: "while ($0)\n{\n    \n}" },
  { label: "loop", detail: "main loop with Thread.Sleep", text: "while (true)\n{\n    $0\n    Thread.Sleep(100);\n}" },
  { label: "do", detail: "do / while", text: "do\n{\n    $0\n} while ();" },
  { label: "if", detail: "if statement", text: "if ($0)\n{\n    \n}" },
  { label: "ife", detail: "if / else", text: "if ($0)\n{\n    \n}\nelse\n{\n    \n}" },
  { label: "switch", detail: "switch statement", text: "switch ($0)\n{\n    case 0:\n        break;\n    default:\n        break;\n}" },
  { label: "try", detail: "try / catch", text: "try\n{\n    $0\n}\ncatch (Exception e)\n{\n    Console.WriteLine(e.Message);\n}" },
  { label: "tryf", detail: "try / catch / finally", text: "try\n{\n    $0\n}\ncatch (Exception e)\n{\n    Console.WriteLine(e.Message);\n}\nfinally\n{\n    \n}" },
  { label: "tryio", detail: "catch hardware errors", text: "try\n{\n    $0\n}\ncatch (IOException e) { Console.WriteLine($\"I/O error: {e.Message}\"); }\ncatch (TimeoutException) { Console.WriteLine(\"timeout\"); }" },
  { label: "class", detail: "class", text: "class $0\n{\n    \n}" },
  { label: "ctor", detail: "constructor", text: "public $0()\n{\n    \n}" },
  { label: "prop", detail: "auto property", text: "public int $0 { get; set; }" },
  { label: "propg", detail: "read-only property", text: "public int $0 => 0;" },
  { label: "interface", detail: "interface", text: "interface I$0\n{\n    \n}" },
  { label: "enum", detail: "enum", text: "enum $0 { A, B, C }" },
  { label: "func", detail: "local function", text: "int $0(int x)\n{\n    return x;\n}" },
  { label: "lambda", detail: "Action lambda", text: "Action $0 = () =>\n{\n    \n};" },
  { label: "pin", detail: "new Pin output", text: "var led = new Pin($0, GPIO.Output);" },
  { label: "pinin", detail: "new Pin input with pull-up", text: "var button = new Pin($0, GPIO.InputPullUp);" },
  { label: "onchange", detail: "pin interrupt", text: "button.OnChange(GPIO.Falling, (bool level) =>\n{\n    $0\n});" },
  { label: "gpioirq", detail: "GPIO.OnChange(pin, edge, fn)", text: "GPIO.OnChange($0, GPIO.Both, (int pin, bool level) =>\n{\n    \n});" },
  { label: "every", detail: "Scheduler.Every", text: "Scheduler.Every(1000, () =>\n{\n    $0\n});" },
  { label: "after", detail: "Scheduler.After", text: "Scheduler.After(1000, () =>\n{\n    $0\n});" },
  { label: "timer", detail: "Timer.Start (hardware)", text: "Timer.Start(0, 1000, () =>\n{\n    $0\n});" },
  { label: "i2cdev", detail: "new I2cDevice", text: "I2C.Open(0, 400000);\nvar dev = new I2cDevice(0, 0x$0);" },
  { label: "i2cscan", detail: "scan the I2C bus", text: "I2C.Open(0);\nforeach (int a in I2C.Scan(0)) Console.WriteLine($\"0x{a:X2}\");$0" },
  { label: "spidev", detail: "new SpiDevice", text: "var dev = new SpiDevice(0, $0, 1000000, 0);" },
  { label: "uart", detail: "open a UART", text: "UART.Open(1, 115200);$0" },
  { label: "uartrx", detail: "UART.OnReceive", text: "UART.OnReceive(1, (int available) =>\n{\n    string s = UART.ReadString(1, available);\n    $0\n});" },
  { label: "adc", detail: "read millivolts", text: "int mv = ADC.ReadMillivolts($0);" },
  { label: "pwm", detail: "PWM.Set", text: "PWM.Set($0, 1000, 0.5);" },
  { label: "servo", detail: "PWM.Servo", text: "PWM.Servo($0, 90);" },
  { label: "sw", detail: "Stopwatch", text: "var sw = Stopwatch.StartNew();\n$0\nConsole.WriteLine($\"{sw.ElapsedMilliseconds} ms\");" },
  { label: "wdt", detail: "watchdog", text: "Watchdog.Start(3000);\n// call Watchdog.Feed() regularly$0" },
  { label: "readfile", detail: "read a text file", text: "string text = File.Exists(\"$0\") ? File.ReadAllText(\"\") : \"\";" },
  { label: "append", detail: "append a line to a file", text: "File.AppendAllText(\"/log.txt\", $\"{Environment.TickCount}: $0\\n\");" },
  { label: "ls", detail: "list files", text: "foreach (var f in Directory.GetFiles(\"/$0\")) Console.WriteLine(f);" },
  { label: "has", detail: "if the board has a peripheral", text: "if (Hal.Has(\"$0\"))\n{\n    \n}" },
  { label: "plot", detail: "print values for the serial plotter", text: "Console.WriteLine($\"a:{$0} b:{0}\");" },
  { label: "pulse", detail: "GPIO.PulseIn (pulse length in µs)", text: "int us = GPIO.PulseIn($0, true, 30000);" },
  { label: "regs", detail: "read I2C registers", text: "byte[] r = I2C.ReadRegisters(0, 0x$0, 0x00, 2);\nint value = (r[0] << 8) | r[1];" },
  { label: "bcd", detail: "BCD helpers (RTC chips)", text: "int FromBcd(int b) => (b >> 4) * 10 + (b & 0x0F);\nint ToBcd(int v) => ((v / 10) << 4) | (v % 10);$0" },
  { label: "clamp", detail: "Math.Clamp", text: "Math.Clamp($0, 0, 100)" },
  { label: "map", detail: "map a value from one range to another", text: "double Map(double x, double inMin, double inMax, double outMin, double outMax) => (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;$0" },
  { label: "usingb", detail: "using block (calls Dispose)", text: "using (var $0 = )\n{\n    \n}" },
  { label: "cmd", detail: "read commands from the console", text: "for (;;)\n{\n    string line = Console.ReadLine();\n    if (line == null) break;\n    $0\n}" },
];
