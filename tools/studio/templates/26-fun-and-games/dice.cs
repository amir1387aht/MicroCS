// Electronic die: 7 LEDs in the classic pattern (a..g), button to roll
int[] leds = { 13, 12, 14, 27, 26, 25, 33 };
//  a . b      positions:  a=top-left  b=top-right  c=mid-left  d=center
//  c d e                  e=mid-right f=bottom-left g=bottom-right
//  f . g
string[] faces = { "d", "ag", "adg", "abfg", "abdfg", "abcefg" };
foreach (int p in leds) GPIO.Mode(p, GPIO.Output);
var rnd = new Random();

void Show(int value)
{
    string on = faces[value - 1];
    for (int i = 0; i < 7; i++) GPIO.Write(leds[i], on.Contains((char)('a' + i)));
    string Dot(char c) => on.Contains(c) ? "o" : ".";
    Console.WriteLine($"{Dot('a')} . {Dot('b')}\n{Dot('c')} {Dot('d')} {Dot('e')}\n{Dot('f')} . {Dot('g')}\n");
}

for (int roll = 0; roll < 3; roll++)
{
    for (int spin = 0; spin < 6; spin++) { foreach (int p in leds) GPIO.Write(p, rnd.Next(2)); Thread.Sleep(60 + spin * 30); }
    int v = rnd.Next(1, 7);
    Console.WriteLine($"rolled {v}");
    Show(v);
    Thread.Sleep(800);
}
