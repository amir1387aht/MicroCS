double Filter(int n) {
    double ema = 0, x = 0, acc = 0; int alarms = 0;
    for (int i = 0; i < n; i++) {
        x = x * 0.999 + 0.5 * (i % 100) / 10.0;
        ema = ema * 0.9 + x * 0.1;
        if (ema > 30.0) alarms++;
        if (x < 1.0) x += 2.5;
        acc += ema - x;
    }
    return acc + alarms;
}
Console.WriteLine(Filter(5000000));
