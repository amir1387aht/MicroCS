import time
class P:
    def __init__(self,x,y): self.x=x; self.y=y
    def len2(self): return self.x*self.x+self.y*self.y
t=time.time(); acc=0
for r in range(100):
    l=[P(i%100,r) for i in range(10000)]
    for p in l: acc=(acc+p.len2())%1000003
print(acc); print("objects 1M: %d ms" % ((time.time()-t)*1000))
