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
