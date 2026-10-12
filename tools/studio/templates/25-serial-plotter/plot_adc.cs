// Two analog inputs, 20 samples per second - watch them in Console -> Plotter
for (int i = 0; i < 300; i++)
{
    Console.WriteLine($"a0:{ADC.ReadMillivolts(0)} a1:{ADC.ReadMillivolts(1)}");
    Thread.Sleep(50);
}
