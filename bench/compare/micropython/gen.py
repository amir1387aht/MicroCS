import sys, json, os
os.chdir(sys.argv[1] if len(sys.argv) > 1 else ".")
names = ["fib", "loop", "objects", "sensor", "strings"]
out = ["typedef struct { const char* name; const char* src; const unsigned char* mpy; unsigned mpy_len; } pyb_t;"]
for n in names:
    b = open(f"{n}.mpy", "rb").read()
    out.append(f"static const unsigned char {n}_mpy[] = {{{','.join(str(x) for x in b)}}};")
out.append("static const pyb_t benches[] = {")
for n in names:
    out.append(f'  {{"{n}", {json.dumps(open(f"{n}.py").read())}, {n}_mpy, sizeof {n}_mpy}},')
out.append("};")
open(sys.argv[2] if len(sys.argv) > 2 else "bench_py.h", "w").write("\n".join(out) + "\n")
