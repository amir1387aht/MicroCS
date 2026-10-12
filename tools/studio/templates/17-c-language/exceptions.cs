class SensorException : Exception { public int Code; public SensorException(string m, int code) : base(m) { Code = code; } }

void Read(int n)
{
    if (n == 2) throw new SensorException("checksum error", 2);
    if (n == 3) throw new InvalidOperationException("not ready");
    Console.WriteLine($"read {n} ok");
}
for (int i = 1; i <= 3; i++)
{
    try { Read(i); }
    catch (SensorException e) when (e.Code == 2) { Console.WriteLine($"sensor: {e.Message}"); }
    catch (Exception e) { Console.WriteLine($"{e.GetType()}: {e.Message}"); }
    finally { Console.WriteLine($"attempt {i} done"); }
}
