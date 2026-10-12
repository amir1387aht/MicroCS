// A traffic light as a state machine: each state schedules the next one.
var red = new Pin(2, GPIO.Output);
var yellow = new Pin(3, GPIO.Output);
var green = new Pin(4, GPIO.Output);

void Show(bool r, bool y, bool g) { red.Write(r); yellow.Write(y); green.Write(g); }

void Red()    { Show(true, false, false); Console.WriteLine("red");    Scheduler.After(3000, Green); }
void Green()  { Show(false, false, true); Console.WriteLine("green");  Scheduler.After(3000, Yellow); }
void Yellow() { Show(false, true, false); Console.WriteLine("yellow"); Scheduler.After(1000, Red); }

Red();
