local sum = 0
for i = 0, 9999999 do sum = sum + (i % 7) end
print(sum)
