class P:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    def len2(self):
        return self.x * self.x + self.y * self.y
def main():
    acc = 0
    for r in range(4):
        lst = []
        for i in range(500):
            lst.append(P(i % 100, r))
        for p in lst:
            acc = (acc + p.len2()) % 1000003
    print(acc)
main()
