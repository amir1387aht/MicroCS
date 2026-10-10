// whole script on its own thread: produce values, then a sentinel
var ch = new Channel("data", 4);
for (int i = 1; i <= 10; i++) ch.Send(i * 10);
ch.Send("done");
Console.WriteLine("producer finished on a thread: " + (Thread.Id > 0));
