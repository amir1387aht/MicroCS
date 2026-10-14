// t28_nim.cs - Nim AI strategy test
int AiMove(int stones) { int rem = (stones - 1) % 4; return rem != 0 ? rem : 1; }
void Check(int stones, int expectedTake) {
    int take = AiMove(stones);
    if (take != expectedTake) {
        Console.WriteLine($"FAIL: stones={stones} expected={expectedTake} got={take}");
        throw new Exception("Nim AI test failed");
    }
}
Check(2, 1);
Check(3, 2);
Check(4, 3);
Check(5, 1); // fallback case
Check(6, 1);
Check(7, 2);
Check(8, 3);
Check(10, 1);
Console.WriteLine("PASS: t28_nim");
