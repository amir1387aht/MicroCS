// 28BYJ-48 with a ULN2003 board: four coil pins, half-step sequence (4096 steps / turn)
int[] pins = { 13, 12, 14, 27 };
foreach (int p in pins) GPIO.Mode(p, GPIO.Output);
int[][] seq = { new[] {1,0,0,0}, new[] {1,1,0,0}, new[] {0,1,0,0}, new[] {0,1,1,0},
                new[] {0,0,1,0}, new[] {0,0,1,1}, new[] {0,0,0,1}, new[] {1,0,0,1} };
int phase = 0;

void Step(int count, int delayMs)
{
    int dir = count > 0 ? 1 : -1;
    for (int s = 0; s < Math.Abs(count); s++)
    {
        phase = (phase + dir + 8) % 8;
        for (int c = 0; c < 4; c++) GPIO.Write(pins[c], seq[phase][c]);
        Thread.Sleep(delayMs);
    }
    foreach (int p in pins) GPIO.Write(p, 0);   // coils off: no heat
}

Step(512, 2); Console.WriteLine("45 degrees clockwise");
Step(-512, 2); Console.WriteLine("and back");
