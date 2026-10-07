# Host timing for the MicroPython unix port: micropython host_time.py fib.py
# (compile once, best of 50 runs of the module code, microseconds).
import sys, time
name = sys.argv[1]
src = open(name).read()
code = compile(src, name, "exec")
best = 1e9
for i in range(50):
    g = {"__name__": "__main__"}
    t = time.ticks_us()
    exec(code, g)
    d = time.ticks_diff(time.ticks_us(), t)
    best = min(best, d)
print("[time]", name, best, "us")
