using System;
using System.Collections;
using System.Text;
namespace Bench {
    class P { public int X, Y; public P(int x, int y) { X = x; Y = y; } public int Len2() { return X * X + Y * Y; } }
    public class Program {
        static int Fib(int n) { return n < 2 ? n : Fib(n - 1) + Fib(n - 2); }
        static int Sum(int n) { int sum = 0; for (int i = 0; i < n; i++) { sum += i % 7; if ((i & 3) == 0) sum -= 1; } return sum; }
        static int seed = 12345;
        static int Next() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed; }
        static double Avg(double[] a) { double s = 0; for (int i = 0; i < a.Length; i++) s += a[i]; return s / a.Length; }
        static long T0;
        static void Start() { T0 = DateTime.UtcNow.Ticks; }
        static void Stop(string name) { long us = (DateTime.UtcNow.Ticks - T0) / 10; Console.WriteLine("[time] " + name + " " + us.ToString() + " us"); }
        static int Objects(int rounds, int n) {
            int acc = 0;
            for (int r = 0; r < rounds; r++) {
                var list = new ArrayList();
                for (int i = 0; i < n; i++) list.Add(new P(i % 100, r));
                foreach (P p in list) acc = (acc + p.Len2()) % 1000003;
            }
            return acc;
        }
        static void Sensor() {
            double[] buf = new double[64]; seed = 12345; double ema = 0; int alarms = 0;
            for (int t = 0; t < 300; t++) {
                double v = 20.0 + (Next() % 1000) / 100.0;
                buf[t % buf.Length] = v; ema = ema * 0.9 + v * 0.1; if (v > 29.5) alarms++;
            }
            Console.WriteLine("avg=" + Avg(buf).ToString("F2") + " ema=" + ema.ToString("F2") + " alarms=" + alarms.ToString());
        }
        static void Strings() {
            var sb = new StringBuilder();
            for (int i = 0; i < 200; i++) { sb.Append(i); sb.Append(','); }
            string s = sb.ToString(); int total = 0;
            foreach (var part in s.Split(',')) if (part.Length > 0) total += int.Parse(part);
            Console.WriteLine(s.Length.ToString() + " " + total.ToString());
        }
        public static void Main() {
            for (int rep = 0; rep < 3; rep++) {
            Start(); Console.WriteLine(Fib(18).ToString()); Stop("mcu-fib");
            Start(); Console.WriteLine(Sum(50000).ToString()); Stop("mcu-loop");
            Start(); Console.WriteLine(Objects(4, 500).ToString()); Stop("mcu-objects");
            Start(); Sensor(); Stop("mcu-sensor");
            Start(); Strings(); Stop("mcu-strings");
            Start(); Console.WriteLine(Fib(30).ToString()); Stop("fib30");
            Start(); int sum = 0; for (int i = 0; i < 10000000; i++) { sum += i % 7; } Console.WriteLine(sum.ToString()); Stop("loop10M");
            Start(); Console.WriteLine(Objects(100, 10000).ToString()); Stop("objects1M");
            }
        }
    }
}
