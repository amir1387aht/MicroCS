// Guess the number - type your guesses in the console ("q" quits)
var rnd = new Random();
int secret = rnd.Next(1, 101), tries = 0;
Console.WriteLine("I'm thinking of a number from 1 to 100.");
while (true)
{
    Console.Write("your guess: ");
    string line = Console.ReadLine();
    if (line == null || line.Trim() == "q") { Console.WriteLine($"\nit was {secret}"); break; }
    if (!int.TryParse(line.Trim(), out int g)) { Console.WriteLine("type a number"); continue; }
    tries++;
    if (g < secret) Console.WriteLine("higher");
    else if (g > secret) Console.WriteLine("lower");
    else { Console.WriteLine($"correct! {tries} guesses"); break; }
}
