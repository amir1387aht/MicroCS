// Hobby servo on PWM channel 1: 50 Hz, 500-2500 us = 0-180 degrees.
// Servo comes from the firmware's "servo" driver; use new Servo(1, 544, 2400) for your servo's pulse range.
var servo = new Servo(1);
for (int deg = 0; deg <= 180; deg += 10) { servo.Angle = deg; Thread.Sleep(80); }
for (int deg = 180; deg >= 0; deg -= 10) { servo.Angle = deg; Thread.Sleep(80); }
servo.Pulse = 1500;             // centre, as a raw pulse width in microseconds
Thread.Sleep(500);
servo.Detach();                 // stop the signal: the servo goes limp
