// PID controller (with anti-windup) - here it drives a simulated heater model
class Pid
{
    public double Kp, Ki, Kd, Min = 0, Max = 1;
    double integral, last; bool first = true;
    public Pid(double kp, double ki, double kd) { Kp = kp; Ki = ki; Kd = kd; }
    public double Update(double setPoint, double measured, double dt)
    {
        double err = setPoint - measured;
        double deriv = first ? 0 : (measured - last) / dt;     // derivative on measurement: no kick
        first = false; last = measured;
        double outRaw = Kp * err + Ki * (integral + err * dt) - Kd * deriv;
        if (outRaw > Min && outRaw < Max) integral += err * dt; // anti-windup
        return Math.Clamp(outRaw, Min, Max);
    }
}

var pid = new Pid(0.08, 0.02, 0.05);
double temp = 20, setPoint = 60, dt = 0.5;
for (int i = 0; i < 80; i++)
{
    double power = pid.Update(setPoint, temp, dt);           // 0..1 -> PWM.Set(0, 1000, power)
    temp += (power * 8 - (temp - 20) * 0.05) * dt;           // heater model
    if (i % 4 == 0) Console.WriteLine($"setpoint:{setPoint} temp:{temp:F2} power:{power * 100:F0}");
}
