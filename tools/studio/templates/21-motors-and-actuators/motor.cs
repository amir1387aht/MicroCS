// DC motor on an H-bridge: IN1/IN2 set the direction, PWM sets the speed
const int IN1 = 25, IN2 = 26, PWM_CH = 0;     // PWM channel 0 drives the ENA pin
GPIO.Mode(IN1, GPIO.Output); GPIO.Mode(IN2, GPIO.Output);

void Drive(double speed)                       // -1.0 (full reverse) .. 1.0 (full forward)
{
    speed = Math.Clamp(speed, -1.0, 1.0);
    GPIO.Write(IN1, speed > 0); GPIO.Write(IN2, speed < 0);
    PWM.Set(PWM_CH, 20000, Math.Abs(speed));   // 20 kHz: silent
}
void Brake() { GPIO.Write(IN1, 1); GPIO.Write(IN2, 1); PWM.Set(PWM_CH, 20000, 1.0); }

for (double s = 0; s <= 1.0; s += 0.25) { Drive(s); Console.WriteLine($"forward {s:P0}"); Thread.Sleep(400); }
Brake(); Thread.Sleep(300);
for (double s = 0; s >= -1.0; s -= 0.5) { Drive(s); Console.WriteLine($"reverse {-s:P0}"); Thread.Sleep(400); }
Drive(0); PWM.Stop(PWM_CH);
