// Pan/tilt bracket: pan servo on PWM channel 0, tilt servo on channel 1.
// The tilt servo only travels 0-90 degrees here (new Servo(channel, minUs, maxUs, maxAngle)).
var pan = new Servo(0);
var tilt = new Servo(1, 1000, 2000, 90);
pan.Angle = 90; tilt.Angle = 45;
Thread.Sleep(500);

void Aim(int p, int t, int ms)
{
    // start the tilt first: MoveTo blocks, so for a diagonal move split it in two halves
    tilt.MoveTo(t, ms / 2);
    pan.MoveTo(p, ms / 2);
    Console.WriteLine($"aimed pan={pan.Angle} tilt={tilt.Angle}");
}

Aim(20, 10, 800);
Aim(160, 80, 1200);
Aim(90, 45, 800);                         // back to centre
pan.Detach(); tilt.Detach();
