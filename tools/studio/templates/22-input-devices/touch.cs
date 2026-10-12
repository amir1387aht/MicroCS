// Charge-time touch sensor: a wire / foil on a pin, 1M resistor to a "send" pin.
// Touching adds capacitance, so the pin takes longer to read high.
const int SEND = 26, SENSE = 27;
GPIO.Mode(SEND, GPIO.Output);

int Measure()
{
    GPIO.Mode(SENSE, GPIO.Output); GPIO.Write(SENSE, 0); GPIO.Write(SEND, 0);
    Hal.DelayMicroseconds(10);
    GPIO.Mode(SENSE, GPIO.Input);
    long t0 = Hal.Micros;
    GPIO.Write(SEND, 1);
    while (!GPIO.Read(SENSE) && Hal.Micros - t0 < 2000) { }
    return (int)(Hal.Micros - t0);
}

int baseline = 0;
for (int i = 0; i < 8; i++) baseline += Measure();
baseline /= 8;
for (int i = 0; i < 30; i++)
{
    int us = Measure();
    Console.WriteLine($"us:{us} touched:{(us > baseline * 2 ? 1 : 0)}");
    Thread.Sleep(100);
}
