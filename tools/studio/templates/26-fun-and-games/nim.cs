// Nim game: you take 1-3 stones, ("q" quits)
int AiMove(int stones) { int rem = (stones - 1) % 4; return rem != 0 ? rem : 1; }
int stones = new Random().Next(15, 26);
Console.WriteLine("Welcome to nim! You may take 1-3 stones. Who takes last stone - loses! (q quits)");
while (stones > 0)
{
    Console.Write($"Stones left: {stones}. How much do you take: ");
    string input = Console.ReadLine();
    if (input == null || input == "q") return;
    int playerTake;
    if (!int.TryParse(input, out playerTake) || playerTake < 1 || playerTake > 3 || playerTake > stones) {
        Console.WriteLine("Impossible move, you may take only 1-3 stones and less than is left.");
        continue; }
    stones -= playerTake;
    if (stones == 0) { Console.WriteLine("You took last stone. You have lost!"); break; }
    int aiTake = AiMove(stones);
    stones -= aiTake;
    Console.WriteLine($"I've decided to take {aiTake} stones.");
    if (stones == 0) { Console.WriteLine("I took last stone. You won!"); break; }
}
