class RingBuffer<T>
{
    readonly T[] items; int start, count;
    public RingBuffer(int capacity) { items = new T[capacity]; }
    public int Count => count;
    public void Add(T item)
    {
        items[(start + count) % items.Length] = item;
        if (count < items.Length) count++; else start = (start + 1) % items.Length;
    }
    public T this[int i] => items[(start + i) % items.Length];
    public List<T> ToList() { var l = new List<T>(); for (int i = 0; i < count; i++) l.Add(this[i]); return l; }
}

var last5 = new RingBuffer<int>(5);
for (int i = 1; i <= 8; i++) last5.Add(i * 10);
Console.WriteLine($"{last5.Count} kept: {string.Join(", ", last5.ToList())}");
