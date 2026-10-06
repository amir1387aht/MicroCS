// 03 · PWM: LED fade, hobby servo and a buzzer tone.
// PWM.Set(channel, hz, duty 0.0..1.0)   PWM.SetPermille(ch, hz, 0..1000)
// PWM.Servo(ch, degrees)          PWM.Tone(ch, hz)   PWM.Stop(ch)
// Run: ./mcs --sim examples/hardware/03_pwm_fade_servo_tone.cs
const int LedCh = 0, ServoCh = 1, BuzzerCh = 2;

// fade up and down at 1 kHz
for (double duty = 0; duty <= 1.0; duty += 0.25) { PWM.Set(LedCh, 1000, duty); Thread.Sleep(20); }
for (int pm = 1000; pm >= 0; pm -= 250) { PWM.SetPermille(LedCh, 1000, pm); Thread.Sleep(20); }
Console.WriteLine("fade done");

// sweep a servo (50 Hz, 1..2 ms pulses are handled for you)
foreach (int angle in new[] { 0, 45, 90, 135, 180 }) { PWM.Servo(ServoCh, angle); Thread.Sleep(20); }
Console.WriteLine("servo swept 0..180 degrees");

// play a short melody
int[] notes = { 262, 294, 330, 349, 392 };      // C D E F G
foreach (int hz in notes) { PWM.Tone(BuzzerCh, hz); Thread.Sleep(30); }
PWM.Stop(BuzzerCh);
Console.WriteLine("melody played");
