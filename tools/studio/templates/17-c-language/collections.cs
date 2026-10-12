var list = new List<int> { 5, 3, 8 };
list.Add(1); list.Sort();
Console.WriteLine("list: " + string.Join(", ", list));

var ages = new Dictionary<string, int> { ["ann"] = 31, ["bob"] = 25 };
ages["cid"] = 40;
foreach (var (name, age) in ages) Console.WriteLine($"{name} is {age}");
Console.WriteLine(ages.TryGetValue("dan", out int a) ? $"dan {a}" : "no dan");

var seen = new HashSet<string> { "x", "y" };
Console.WriteLine($"added z: {seen.Add("z")}, added x again: {seen.Add("x")}");

var q = new Queue<string>(); q.Enqueue("first"); q.Enqueue("second");
var st = new Stack<int>(); st.Push(1); st.Push(2);
Console.WriteLine($"queue -> {q.Dequeue()}, stack -> {st.Pop()}");
