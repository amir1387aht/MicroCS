import time
def fib(n): return n if n < 2 else fib(n-1) + fib(n-2)
t=time.time(); print(fib(30)); print("fib(30): %d ms" % ((time.time()-t)*1000))
