using System;
using System.Collections.Generic;
using System.Text;

class Node { public int V; public Node Next; public List<string> Tags = new List<string>(); }

int total = 0;
for (int round = 0; round < 200; round++)
{
    Node head = null;
    for (int i = 0; i < 50; i++)
    {
        var n = new Node { V = i, Next = head };
        n.Tags.Add("t" + i);
        n.Tags.Add($"r{round}");
        head = n;
    }
    var d = new Dictionary<string, Node>();
    for (var p = head; p != null; p = p.Next) d[p.Tags[0]] = p;
    var sb = new StringBuilder();
    foreach (var kv in d) sb.Append(kv.Key.Length);
    Func<int, int> f = x => x + round;
    total += d.Count + sb.Length + f(0) - round;
}
Console.WriteLine(total);
Console.WriteLine(GC.CollectionCount(0) > 0);
var keep = new List<int[]>();
for (int i = 0; i < 100; i++) keep.Add(new int[16]);
string s = "";
for (int i = 0; i < 300; i++) s += (char)('a' + i % 26);
Console.WriteLine(s.Length + " " + keep.Count + " " + s.Substring(0, 5));
