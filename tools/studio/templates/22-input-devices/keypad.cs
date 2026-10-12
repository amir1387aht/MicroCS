// 4x4 keypad: drive one row low at a time and read the columns
int[] rows = { 13, 12, 14, 27 }, cols = { 26, 25, 33, 32 };
string[] keys = { "123A", "456B", "789C", "*0#D" };
foreach (int r in rows) { GPIO.Mode(r, GPIO.Output); GPIO.Write(r, 1); }
foreach (int c in cols) GPIO.Mode(c, GPIO.InputPullUp);

char? Scan()
{
    for (int r = 0; r < 4; r++)
    {
        GPIO.Write(rows[r], 0);
        for (int c = 0; c < 4; c++)
            if (!GPIO.Read(cols[c])) { GPIO.Write(rows[r], 1); return keys[r][c]; }
        GPIO.Write(rows[r], 1);
    }
    return null;
}

var code = new StringBuilder();
char? prev = null;
for (int i = 0; i < 300; i++)                    // ~6 s
{
    char? k = Scan();
    if (k != null && k != prev)
    {
        if (k == '#') { Console.WriteLine(code.ToString() == "1234" ? "unlocked" : "wrong code"); code.Clear(); }
        else code.Append(k);
    }
    prev = k;
    Thread.Sleep(20);
}
