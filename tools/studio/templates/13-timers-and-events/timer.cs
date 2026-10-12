// Timer 0 samples the ADC every 10 ms; timer 1 fires once after 1 s.
var samples = new List<int>();
Timer.Start(0, 10000, () => samples.Add(ADC.Read(0)));
Timer.Once(1, 1000000, () => Console.WriteLine($"1 s later: {samples.Count} samples"));
Thread.Sleep(1500);
Timer.Stop(0);
Console.WriteLine($"average {samples.Average():F1} over {samples.Count} samples");
