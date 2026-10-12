// Bang-bang control with hysteresis (a simulated room)
const double SET = 21.0, BAND = 0.5;
bool heating = false;
double room = 18.0;
var relay = new Pin(23, GPIO.Output);

for (int minute = 0; minute < 120; minute++)
{
    if (room < SET - BAND) heating = true;
    else if (room > SET + BAND) heating = false;   // between the limits: keep the state
    relay.Write(heating);
    room += heating ? 0.12 : -0.05;               // room model
    if (minute % 5 == 0) Console.WriteLine($"room:{room:F2} heating:{(heating ? 1 : 0)}");
}
