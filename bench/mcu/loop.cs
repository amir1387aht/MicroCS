// integer loop with locals (inside a method, like most firmware code)
int Sum(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) { sum += i % 7; if ((i & 3) == 0) sum -= 1; }
    return sum;
}
Console.WriteLine(Sum(50000));
