def total(n):
    s = 0
    for i in range(n):
        s += i % 7
        if (i & 3) == 0:
            s -= 1
    return s
print(total(50000))
