import time
t=time.time(); s=0
for i in range(10000000): s += i % 7
print(s); print("loop 10M: %d ms" % ((time.time()-t)*1000))
