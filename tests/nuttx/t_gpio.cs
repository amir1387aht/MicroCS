// NuttX sim:nsh registers preconfigured GPIO0 (input) and GPIO1 (output).
Console.WriteLine("native " + Hal.Board + " gpio=" + Hal.Has("GPIO"));
GPIO.Mode(0, GPIO.Input);
GPIO.Mode(1, GPIO.Output);
GPIO.Write(1, true);
Console.WriteLine("high " + GPIO.Read(1));
GPIO.Write(1, false);
Console.WriteLine("low " + GPIO.Read(1));
try { GPIO.Mode(0, GPIO.Output); } catch (NotSupportedException) { Console.WriteLine("mode rejected"); }
try { GPIO.Read(999); } catch (IOException) { Console.WriteLine("missing pin"); }
