// Morse code on an LED: dot = 1 unit, dash = 3, gaps 1 / 3 / 7 units
var led = new Pin(2, GPIO.Output);
const int UNIT = 120;
var code = new Dictionary<char, string> {
    ['A'] = ".-", ['B'] = "-...", ['C'] = "-.-.", ['D'] = "-..", ['E'] = ".", ['F'] = "..-.", ['G'] = "--.", ['H'] = "....",
    ['I'] = "..", ['J'] = ".---", ['K'] = "-.-", ['L'] = ".-..", ['M'] = "--", ['N'] = "-.", ['O'] = "---", ['P'] = ".--.",
    ['Q'] = "--.-", ['R'] = ".-.", ['S'] = "...", ['T'] = "-", ['U'] = "..-", ['V'] = "...-", ['W'] = ".--", ['X'] = "-..-",
    ['Y'] = "-.--", ['Z'] = "--..", ['0'] = "-----", ['1'] = ".----", ['2'] = "..---", ['3'] = "...--", ['4'] = "....-",
    ['5'] = ".....", ['6'] = "-....", ['7'] = "--...", ['8'] = "---..", ['9'] = "----.",
};

void Send(string text)
{
    foreach (char ch in text.ToUpper())
    {
        if (ch == ' ') { Thread.Sleep(UNIT * 7); Console.Write(" / "); continue; }
        if (!code.TryGetValue(ch, out var sym)) continue;
        foreach (char s in sym) { led.High(); Thread.Sleep(s == '.' ? UNIT : UNIT * 3); led.Low(); Thread.Sleep(UNIT); }
        Console.Write(sym + " ");
        Thread.Sleep(UNIT * 2);
    }
    Console.WriteLine();
}

Send("SOS MicroCS");
