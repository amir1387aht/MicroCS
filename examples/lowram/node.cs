// Sensor node for a small MCU: runs as a precompiled image executed in place
// from flash (see lowram_firmware.c). The firmware calls Node.Tick() from its
// main loop; all state lives in a few small objects so the heap stays flat.
using System;
using System.Collections.Generic;

Console.WriteLine("node ready");   // top-level code runs once when the image is loaded

enum Mode { Idle, Sampling, Alarm }

class Ring {
    readonly int[] buf;
    int head, count;
    public Ring(int n) { buf = new int[n]; }
    public void Push(int v) { buf[head] = v; head = (head + 1) % buf.Length; if (count < buf.Length) count++; }
    public int Count => count;
    public int Average() { if (count == 0) return 0; int s = 0; for (int i = 0; i < count; i++) s += buf[i]; return s / count; }
    public int Max() { int m = int.MinValue; for (int i = 0; i < count; i++) if (buf[i] > m) m = buf[i]; return m; }
}

class SensorFault : Exception {
    public int Raw;
    public SensorFault(int raw) : base("sensor fault") { Raw = raw; }
}

static class Node {
    static Ring temps = new Ring(8);
    static Mode mode = Mode.Idle;
    static int ticks, faults, alarms;
    const int AlarmAt = 285;          // 28.5 C in tenths

    // fake ADC: deterministic so the example output can be checked
    static int ReadRaw(int t) => t == 13 ? -1 : 200 + (t * 37) % 140;

    static int ToTenths(int raw) {
        if (raw < 0) throw new SensorFault(raw);
        return raw;                       // 1 LSB = 0.1 C on this pretend sensor
    }

    public static string Tick() {
        ticks++;
        try {
            temps.Push(ToTenths(ReadRaw(ticks)));
        } catch (SensorFault e) {
            faults++;
            return $"t={ticks} fault raw={e.Raw}";
        }
        int avg = temps.Average();
        Mode next = avg >= AlarmAt ? Mode.Alarm : temps.Count >= 4 ? Mode.Sampling : Mode.Idle;
        if (next == Mode.Alarm && mode != Mode.Alarm) alarms++;
        mode = next;
        // (enums are plain integers in MicroCS: map them to text explicitly)
        string led = mode switch { Mode.Alarm => "red", Mode.Sampling => "green", _ => "off" };
        int max = temps.Max();
        return $"t={ticks} avg={avg / 10}.{avg % 10}C max={max / 10}.{max % 10}C led={led}";
    }

    public static string Summary() => $"ticks={ticks} faults={faults} alarms={alarms}";
}

