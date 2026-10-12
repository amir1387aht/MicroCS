// Heap use before and after allocating, and after a collection.
long before = GC.GetTotalMemory(false);
var junk = new List<string>();
for (int i = 0; i < 500; i++) junk.Add("item " + i);
long during = GC.GetTotalMemory(false);
junk = null;
long after = GC.GetTotalMemory(true);                    // true = collect first
Console.WriteLine($"before {before} B, with list {during} B, after GC {after} B");
Console.WriteLine($"collections so far: {GC.CollectionCount(0)}");
