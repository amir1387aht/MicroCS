parts = []
for i in range(200):
    parts.append(str(i))
    parts.append(",")
s = "".join(parts)
total = 0
for part in s.split(","):
    if len(part) > 0:
        total += int(part)
print("{} {}".format(len(s), total))
