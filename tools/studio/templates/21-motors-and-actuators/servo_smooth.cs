// Smooth servo motion: MoveTo eases in and out in 20 ms frames and waits until it arrives.
var servo = new Servo(0);                 // PWM channel 0, 500-2500 us = 0-180 degrees
servo.Angle = 90;
Thread.Sleep(500);                        // let it reach the start position

foreach (int a in new[] { 0, 180, 45, 135, 90 })
{
    servo.MoveTo(a, 600);                 // 600 ms
    Console.WriteLine($"at {servo.Angle} deg ({servo.Pulse} us)");
}
servo.Detach();
