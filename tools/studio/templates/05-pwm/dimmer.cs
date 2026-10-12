// Potentiometer on ADC channel 0 sets the brightness of the LED on PWM channel 0.
int max = (1 << ADC.Resolution) - 1;
Console.WriteLine("turn the knob (Stop ends the script)");
while (true)
{
    int raw = ADC.ReadAverage(0, 8);
    PWM.SetPermille(0, 1000, raw * 1000 / max);
    Thread.Sleep(20);
}
