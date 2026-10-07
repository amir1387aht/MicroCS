buf = [0.0] * 64
seed = 12345
def next_rand():
    global seed
    seed = (seed * 1103515245 + 12345) & 0x7fffffff
    return seed
def avg(a):
    s = 0.0
    for i in range(len(a)):
        s += a[i]
    return s / len(a)
ema = 0.0
alarms = 0
for t in range(300):
    v = 20.0 + (next_rand() % 1000) / 100.0
    buf[t % len(buf)] = v
    ema = ema * 0.9 + v * 0.1
    if v > 29.5:
        alarms += 1
print("avg={:.2f} ema={:.2f} alarms={}".format(avg(buf), ema, alarms))
