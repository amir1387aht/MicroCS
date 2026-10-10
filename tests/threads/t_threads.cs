// args: --fs tests/threads
Console.WriteLine("os " + (Thread.Os.Length > 0) + " cores>=1 " + (Thread.Cores >= 1) + " id " + Thread.Id);
var f = Thread.Run("/math.cs", "Fib", 20);
var g = Thread.Run("/math.cs", "Greet", "ab", 3);
var q = Thread.RunOn(0, "/math.cs", "Squares", 5);
Console.WriteLine("fib " + f.Result);
Console.WriteLine("greet " + g.Result);
int[] sq = (int[])q.Result;
Console.WriteLine("squares " + sq.Length + " " + sq[4] + " core " + q.Core);
Console.WriteLine("joined " + f.Join(1000) + " alive " + f.IsAlive + " state " + f.State);

// script thread + channel
var ch = new Channel("data", 4);
var p = Thread.Start("/producer.cs");
int sum = 0;
while (true) { var v = ch.Receive(2000); if (v is string) break; sum += (int)v; }
p.Join();
Console.WriteLine("sum " + sum + " state " + p.State + " count " + ch.Count);

// errors come back to the caller
var b = Thread.Run("/math.cs", "Boom");
try { var r = b.Result; Console.WriteLine("no error?"); }
catch (InvalidOperationException e) { Console.WriteLine("caught: " + e.Message.Contains("boom in a thread")); }
Console.WriteLine("state " + b.State + " error set " + (b.Error != null));

// stop a busy thread
var s = Thread.Run("/math.cs", "Spin");
Console.WriteLine("spinning " + (s.Join(50) == false));
s.Stop();
Console.WriteLine("stopped " + s.Join(2000) + " " + s.State);

// periodic OS task
var ticks = new Channel("ticks", 16);
var per = Thread.Every(20, "/math.cs", "Tick");
int got = 0;
while (got < 3) { var t = ticks.Receive(2000); if (t == null) break; if ((bool)t) got++; }
per.Stop(); per.Join();
Console.WriteLine("periodic runs>=3 " + (per.Runs >= 3) + " " + per.State);

// bad arguments
try { Thread.RunOn(999, "/math.cs", "Fib", 1); } catch (ArgumentOutOfRangeException) { Console.WriteLine("bad core rejected"); }
try { ch.Send(new Dictionary<string, int>()); } catch (ArgumentException) { Console.WriteLine("dictionary rejected"); }
Console.WriteLine("try receive " + (ch.TryReceive() == null) + " threads left " + Thread.Count);
// many in parallel
var ws = new List<Worker>();
for (int i = 0; i < 6; i++) ws.Add(Thread.Run("/math.cs", "Fib", 15 + i));
int tot = 0;
foreach (var w in ws) tot += (int)w.Result;
Console.WriteLine("parallel " + tot);
