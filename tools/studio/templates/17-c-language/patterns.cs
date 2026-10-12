string Classify(double t) => t switch
{
    < 0 => "freezing",
    < 15 => "cold",
    >= 15 and < 25 => "comfortable",
    _ => "hot",
};
foreach (var t in new[] { -3.0, 10, 21, 30 }) Console.WriteLine($"{t,5} C: {Classify(t)}");

object[] things = { 42, "text", 3.5, null };
foreach (var o in things)
{
    string d = o switch { int i when i > 10 => $"big int {i}", int i => $"int {i}", string str => $"string of {str.Length}", null => "null", _ => "something else" };
    Console.WriteLine(d);
}
