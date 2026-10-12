// Stepper driver with STEP / DIR / EN pins (A4988, DRV8825, TMC2208 in step mode)
const int STEP = 32, DIR = 33, EN = 27;
const int STEPS_PER_REV = 200 * 16;            // 1.8 deg motor, 1/16 microstepping
GPIO.Mode(STEP, GPIO.Output); GPIO.Mode(DIR, GPIO.Output); GPIO.Mode(EN, GPIO.Output);
GPIO.Write(EN, 0);                              // enable (active low)

void Move(int steps, int maxSpeed /* steps/s */)
{
    GPIO.Write(DIR, steps > 0);
    int n = Math.Abs(steps), ramp = Math.Min(n / 2, 400);
    for (int i = 0; i < n; i++)
    {
        int edge = Math.Min(i, n - 1 - i);                     // distance to start / end
        double speed = edge < ramp ? maxSpeed * (0.2 + 0.8 * edge / ramp) : maxSpeed;
        GPIO.Write(STEP, 1); Hal.DelayMicroseconds(3); GPIO.Write(STEP, 0);
        Hal.DelayMicroseconds((int)(1_000_000 / speed));
    }
}

Move(STEPS_PER_REV / 4, 4000);   Console.WriteLine("quarter turn forward");
Move(-STEPS_PER_REV / 4, 4000);  Console.WriteLine("and back");
GPIO.Write(EN, 1);
