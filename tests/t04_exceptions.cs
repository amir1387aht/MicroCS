using System;

class SensorException : Exception
{
    public int Code { get; }
    public SensorException(string msg, int code) : base(msg) { Code = code; }
}

class Resource : IDisposable
{
    string name;
    public Resource(string n) { name = n; Console.WriteLine("open " + n); }
    public void Dispose() { Console.WriteLine("close " + name); }
}

static class Program
{
    static int depth = 0;
    static int Read(int ch)
    {
        if (ch < 0) throw new ArgumentOutOfRangeException(nameof(ch));
        if (ch > 3) throw new SensorException("sensor " + ch + " not present", 40 + ch);
        return ch * 100;
    }
    static void Recurse() { depth++; Recurse(); }
    static string Finally()
    {
        try { return "try"; }
        finally { Console.WriteLine("finally runs before return"); }
    }

    static void Main()
    {
        for (int ch = -1; ch <= 5; ch += 3)
        {
            try { Console.WriteLine("value " + Read(ch)); }
            catch (SensorException e) when (e.Code == 45) { Console.WriteLine("filtered " + e.Message + " code=" + e.Code); }
            catch (SensorException e) { Console.WriteLine("sensor " + e.Code); }
            catch (ArgumentException e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
            finally { Console.WriteLine("done " + ch); }
        }
        try { int z = 0; Console.WriteLine(10 / z); }
        catch (DivideByZeroException e) { Console.WriteLine(e.Message); }
        try { string s = null; Console.WriteLine(s.Length); }
        catch (NullReferenceException) { Console.WriteLine("NRE"); }
        try { int[] a = new int[2]; a[5] = 1; }
        catch (IndexOutOfRangeException e) { Console.WriteLine(e.Message); }
        try { object o = "str"; int i = (int)o; }
        catch (InvalidCastException) { Console.WriteLine("bad cast"); }
        try { int.Parse("12x"); }
        catch (FormatException e) { Console.WriteLine("format: " + e.Message); }
        try { Recurse(); }
        catch (StackOverflowException) { Console.WriteLine("stack overflow caught, deep: " + (depth > 30)); }
        try
        {
            try { throw new InvalidOperationException("inner"); }
            catch (Exception e) { throw new Exception("outer", e); }
        }
        catch (Exception e) { Console.WriteLine(e.Message + " <- " + e.InnerException.Message); }
        try { try { throw new TimeoutException("t"); } finally { Console.WriteLine("inner finally"); } }
        catch (TimeoutException e) { Console.WriteLine("rethrown " + e.Message); }
        try { try { throw new Exception("x"); } catch { Console.WriteLine("catch-all"); throw; } }
        catch (Exception e) { Console.WriteLine("rethrow kept: " + e.Message); }
        Console.WriteLine(Finally());
        using (var r = new Resource("uart")) { Console.WriteLine("using body"); }
        using var r2 = new Resource("i2c");
        Console.WriteLine(new SensorException("m", 1) is Exception);
        Console.WriteLine(new SensorException("boom", 7).ToString());
        for (int i = 0; i < 3; i++)
        {
            try { if (i == 1) continue; Console.WriteLine("loop " + i); }
            finally { Console.WriteLine("fin " + i); }
        }
        throw new SensorException("unhandled at end", 99);
    }
}
