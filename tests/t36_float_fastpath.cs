// Fast and generic numeric paths must agree for mixed types and special values.
void Check(double x, double y) {
    Console.WriteLine((x + y) + "," + (x - y) + "," + (x * y) + "," + (x / y));
    Console.WriteLine((x == y) + "," + (x != y) + "," + (x < y) + "," + (x <= y) + "," + (x > y) + "," + (x >= y));
}
Check(1.5, 2.0);
Check(0.0, -0.0);
Check(double.NaN, 1.0);
Check(double.PositiveInfinity, 1.0);
int a = 2; double b = 2.5;
Console.WriteLine((a + b) + "," + (b - a) + "," + (a * b) + "," + (b / a));
Console.WriteLine((a < b) + "," + (b >= a));
char ch = 'A'; Console.WriteLine((ch + b) + "," + (ch < b));
double sum = 0.0; for (int i = 0; i < 5; i++) { sum += b; if (sum > 4) sum -= 1.0; }
Console.WriteLine(sum);
