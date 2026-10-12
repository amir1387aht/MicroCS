// Fade an LED on PWM channel 0 (the port maps channels to pins, see its README).
for (int round = 0; round < 3; round++)
{
    for (int i = 0; i <= 100; i += 2) { PWM.Set(0, 1000, i / 100.0); Thread.Sleep(10); }
    for (int i = 100; i >= 0; i -= 2) { PWM.Set(0, 1000, i / 100.0); Thread.Sleep(10); }
}
PWM.Stop(0);
