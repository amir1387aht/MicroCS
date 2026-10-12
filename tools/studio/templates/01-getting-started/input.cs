// Type the answer in the console box below and press Enter
Console.Write("What is your name? ");
string name = Console.ReadLine();
Console.WriteLine($"Hello, {name}!");

Console.Write("Pick a number: ");
if (int.TryParse(Console.ReadLine(), out int n))
    Console.WriteLine($"{n} squared is {n * n}");
else
    Console.WriteLine("that was not a number");
