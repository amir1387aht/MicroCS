// Fixed-size ring buffer: the newest N samples, constant memory
class Ring
{
    readonly int[] data; int head, count;
    public Ring(int size) { data = new int[size]; }
    public int Count => count;
    public void Push(int v) { data[head] = v; head = (head + 1) % data.Length; if (count < data.Length) count++; }
    public int this[int i] => data[(head - count + i + data.Length * 2) % data.Length];   // 0 = oldest
    public double Average() { long s = 0; for (int i = 0; i < count; i++) s += this[i]; return count > 0 ? (double)s / count : 0; }
}

var ring = new Ring(16);
for (int i = 0; i < 40; i++)
{
    ring.Push(ADC.Read(0));
    Thread.Sleep(25);
}
Console.WriteLine($"{ring.Count} samples, oldest {ring[0]}, newest {ring[ring.Count - 1]}, average {ring.Average():F1}");
