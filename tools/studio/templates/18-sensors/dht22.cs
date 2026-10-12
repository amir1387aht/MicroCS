// DHT22 / AM2302 on one GPIO (with a 10k pull-up). Bit-banged with PulseIn:
// the sensor answers with 40 bits; a long high pulse (~70 us) is a 1, a short one (~27 us) a 0.
const int DATA = 4;

double[] ReadDht()
{
    GPIO.Mode(DATA, GPIO.Output);
    GPIO.Write(DATA, 0); Thread.Sleep(2);       // start signal
    GPIO.Mode(DATA, GPIO.InputPullUp);
    GPIO.PulseIn(DATA, false, 200);             // sensor response low
    GPIO.PulseIn(DATA, true, 200);              // and high
    var bytes = new byte[5];
    for (int i = 0; i < 40; i++)
    {
        int us = GPIO.PulseIn(DATA, true, 200);
        if (us == 0) throw new TimeoutException("no answer from the DHT");
        if (us > 45) bytes[i / 8] |= (byte)(0x80 >> (i % 8));
    }
    if (((bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xFF) != bytes[4]) throw new IOException("checksum");
    double hum = ((bytes[0] << 8) | bytes[1]) / 10.0;
    double t = (((bytes[2] & 0x7F) << 8) | bytes[3]) / 10.0 * ((bytes[2] & 0x80) != 0 ? -1 : 1);
    return new double[] { t, hum };
}

try { var r = ReadDht(); Console.WriteLine($"{r[0]:F1} C  {r[1]:F1} %RH"); }
catch (Exception e) { Console.WriteLine("DHT read failed: " + e.Message); }
