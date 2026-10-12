// Three jobs at different rates; a fourth prints how often each one ran.
var runs = new int[3];
Scheduler.Every(100, () => runs[0]++);
Scheduler.Every(250, () => runs[1]++);
Scheduler.Every(1000, () => runs[2]++);
Scheduler.Every(3000, () => Console.WriteLine($"100ms:{runs[0]} 250ms:{runs[1]} 1s:{runs[2]} jobs:{Scheduler.Count}"));
