// Regression tests from the robustness pass (fuzzing + sanitizers): argument ranges
// that used to overflow, StringComparison overloads, Math.Round on huge values.
static void Try(string name, Action a) {
    try { a(); Console.WriteLine(name + " ok"); }
    catch (Exception e) { Console.WriteLine(name + " " + e.GetType().Name); }
}

// StringComparison is not mistaken for a start index
Console.WriteLine("Hello".IndexOf("l", StringComparison.Ordinal));
Console.WriteLine("Hello".IndexOf("L", StringComparison.OrdinalIgnoreCase));
Console.WriteLine("Hello".IndexOf("LO", 2, 3, StringComparison.OrdinalIgnoreCase));
Console.WriteLine("Hello".Contains("ELL", StringComparison.OrdinalIgnoreCase));
Console.WriteLine("Hello".Contains("ELL"));
Console.WriteLine("Hello".StartsWith("he", StringComparison.OrdinalIgnoreCase));
Console.WriteLine("Hello".StartsWith("he", StringComparison.Ordinal));
Console.WriteLine("Hello".EndsWith("LO", true, null));
Console.WriteLine("Hello".Replace("L", "_", StringComparison.OrdinalIgnoreCase));
Console.WriteLine("Hello".Equals("hello", StringComparison.Ordinal));
Console.WriteLine("Hello".Equals("hello", StringComparison.InvariantCultureIgnoreCase));
Console.WriteLine(string.Equals("Hello", "HELLO", StringComparison.OrdinalIgnoreCase));
Console.WriteLine(string.Compare("a", "B", StringComparison.Ordinal) > 0);
Console.WriteLine(string.Compare("a", "B", StringComparison.OrdinalIgnoreCase) < 0);

// IndexOf count / LastIndexOf startIndex and count
Console.WriteLine("Hello".IndexOf("o", 0, 4));
Console.WriteLine("Hello".IndexOf("o", 0, 5));
Console.WriteLine("abcabc".LastIndexOf("b"));
Console.WriteLine("abcabc".LastIndexOf("b", 3));
Console.WriteLine("abcabc".LastIndexOf('c', 2));
Console.WriteLine("abcabc".LastIndexOf("bc", 1));
Console.WriteLine("abcabc".LastIndexOf("a", 5, 2));
Console.WriteLine("abcabc".LastIndexOf("a", 5, 3));
Try("IndexOf count", () => "abc".IndexOf("b", 1, 5));
Try("LastIndexOf start", () => "abc".LastIndexOf("b", 9));

// start + count near int.MaxValue
Try("sb.Remove", () => new StringBuilder("abc").Remove(1, int.MaxValue));
Try("sb.ToString", () => new StringBuilder("abc").ToString(2, int.MaxValue));
Try("Substring", () => "abc".Substring(2, int.MaxValue));
Try("Remove", () => "abc".Remove(1, int.MaxValue));
Try("GetRange", () => new List<int> { 1, 2, 3 }.GetRange(2, int.MaxValue));
Try("RemoveRange", () => new List<int> { 1, 2, 3 }.RemoveRange(1, int.MaxValue));
Try("Array.Copy", () => Array.Copy(new int[3], 1, new int[3], 0, int.MaxValue));
Try("Array.Fill", () => Array.Fill(new int[3], 1, 2, int.MaxValue));
Try("ToBase64", () => Convert.ToBase64String(new byte[3], 2, int.MaxValue));
Try("UTF8.GetString", () => Encoding.UTF8.GetString(new byte[3], 1, int.MaxValue));
Try("BitConverter", () => BitConverter.ToInt32(new byte[4], int.MaxValue));

// Math.Round with digits keeps huge values (x * 10^d used to overflow to infinity)
Console.WriteLine(Math.Round(1e300, 15) == 1e300);
Console.WriteLine(Math.Round(2.345, 2));
Console.WriteLine(Math.Round(1e17, 3) == 1e17);

// Enumerable.Range: start + count - 1 must fit in int
Try("Range", () => Enumerable.Range(int.MaxValue, 2).ToList());
Console.WriteLine(Enumerable.Range(int.MaxValue, 1).First());

// 16+ nested JSON objects used to exhaust the native GC roots (a fatal, uncatchable error)
var deep = "";
for (int i = 0; i < 15; i++) deep += "{\"a\":";
deep += "1";
for (int i = 0; i < 15; i++) deep += "}";
Console.WriteLine(Json.Stringify(Json.Parse(deep)).Length);
Try("Json too deep", () => Json.Parse(new string('[', 1000)));

// recursion through LINQ callbacks: each level holds native roots; now a catchable StackOverflowException
int Depth(int n) => n == 0 ? 0 : new List<int> { n }.Select(x => Depth(x - 1)).OrderBy(x => x).First() + 1;
Console.WriteLine(Depth(20));
Try("deep LINQ recursion", () => Depth(100000));
Console.WriteLine(Depth(20));

// constant MinValue / -1 is not folded (C UB); the VM throws at run time (C# rejects it, CS0220)
Try("const MinValue / -1", () => Console.WriteLine(-2147483648 / -1));
Try("const MinValue % -1", () => Console.WriteLine(-2147483648 % -1));
