#!/usr/bin/env python3
"""Mutation fuzzer for the MicroCS compiler + VM.

Takes the test scripts as a corpus, applies random token/line/byte mutations
and runs `mcs` with step/time/heap limits. Any crash (signal), sanitizer report
or hang is saved to build/fuzz/ for reproduction.

usage: python3 tools/fuzz.py [./mcs] [iterations] [seed] [--image]
Build mcs with `make asan` first for best results.

--image  mutate compiled bytecode images (.mcsb) instead of source, to
         exercise the image loader's validation. Note: the loader checks
         structure, constant/global/local/upvalue indices and branch targets,
         but not stack balance, so mutated images are loaded and
         disassembled (`mcs -d`) rather than executed. Executing untrusted
         images is not supported (docs/SECURITY.md).
"""
import os, random, subprocess, sys, glob, re

IMAGE = "--image" in sys.argv
argv = [a for a in sys.argv if a != "--image"]
MCS = argv[1] if len(argv) > 1 else "./mcs"
N = int(argv[2]) if len(argv) > 2 else 2000
SEED = int(argv[3]) if len(argv) > 3 else 1
rnd = random.Random(SEED)
here = os.path.dirname(os.path.abspath(__file__))
corpus = [open(f, encoding="utf-8").read() for f in sorted(glob.glob(os.path.join(here, "..", "tests", "*.cs")))]
out_dir = os.path.join(here, "..", "build", "fuzz")
os.makedirs(out_dir, exist_ok=True)
TOKENS = ["(", ")", "{", "}", "[", "]", ";", ",", ".", "=", "==", "=>", "?", ":", "??", "?.",
          "new", "class", "struct", "return", "if", "else", "for", "foreach", "while", "switch",
          "case", "default", "break", "continue", "try", "catch", "finally", "throw", "null",
          "this", "base", "out", "ref", "in", "var", "int", "string", "double", "bool", "is",
          "as", "when", "static", "void", "0", "-1", "2147483647", "1e308", "\"x\"", "'c'",
          "$\"{", "}\"", "x", "List<int>", "Dictionary<string,int>", "=>", "++", "--", "<<", ">>",
          "%", "/", "*", "+", "-", "!", "~", "&&", "||", "lambda", "async", "yield", "\\"]

def mutate(s):
    for _ in range(rnd.randint(1, 4)):
        m = rnd.randrange(6)
        if not s:
            s = rnd.choice(TOKENS)
        p = rnd.randrange(len(s) + 1)
        if m == 0:
            s = s[:p] + " " + rnd.choice(TOKENS) + " " + s[p:]
        elif m == 1:
            q = min(len(s), p + rnd.randint(1, 20))
            s = s[:p] + s[q:]
        elif m == 2:
            lines = s.split("\n"); i = rnd.randrange(len(lines))
            lines.insert(rnd.randrange(len(lines) + 1), lines[i]); s = "\n".join(lines)
        elif m == 3:
            lines = s.split("\n"); i = rnd.randrange(len(lines)); del lines[i]; s = "\n".join(lines)
        elif m == 4:
            s = s[:p] + chr(rnd.randrange(1, 128)) + s[p + 1:]
        else:
            other = rnd.choice(corpus); a = rnd.randrange(len(other) + 1)
            s = s[:p] + other[a:a + rnd.randint(1, 200)] + s[p:]
    return s

def build_images():
    imgs = []
    for f in sorted(glob.glob(os.path.join(here, "..", "tests", "*.cs"))):
        o = os.path.join(out_dir, "seed_" + os.path.basename(f) + "b")
        if subprocess.run([MCS, "-c", f, "-o", o], capture_output=True).returncode == 0:
            imgs.append(open(o, "rb").read())
    return imgs

def mutate_image(b):
    b = bytearray(b)
    for _ in range(rnd.randint(1, 6)):
        m = rnd.randrange(4); p = rnd.randrange(8, len(b))
        if m == 0: b[p] = rnd.randrange(256)
        elif m == 1: b[p] ^= 1 << rnd.randrange(8)
        elif m == 2: del b[p:p + rnd.randint(1, 8)]
        else: b[p:p] = bytes(rnd.randrange(256) for _ in range(rnd.randint(1, 4)))
        if len(b) < 16: break
    return bytes(b)

BAD = re.compile(r"AddressSanitizer|runtime error:|LeakSanitizer|Assertion|internal error")
found = 0
images = build_images() if IMAGE else None
for it in range(N):
    if IMAGE:
        path = os.path.join(out_dir, "cur.mcsb")
        with open(path, "wb") as f:
            f.write(mutate_image(rnd.choice(images)))
    else:
        path = os.path.join(out_dir, "cur.cs")
        with open(path, "w", encoding="utf-8", errors="replace") as f:
            f.write(mutate(rnd.choice(corpus)))
    try:
        cmd = [MCS, "-d", path] if IMAGE else [MCS, "--step-limit", "200000", "--time-limit", "2000", "--heap", "262144",
                            "--ramfs", "16384", "--sim", "--run-for", "300", path]
        r = subprocess.run(cmd, capture_output=True, timeout=20) if IMAGE else subprocess.run([MCS, "--step-limit", "200000", "--time-limit", "2000", "--heap", "262144",
                            "--ramfs", "16384", "--sim", "--run-for", "300", path], capture_output=True, timeout=20)
        out = (r.stdout + r.stderr).decode("utf-8", "replace")
        bad = r.returncode < 0 or BAD.search(out)
    except subprocess.TimeoutExpired:
        bad, out = True, "TIMEOUT"
    if bad:
        found += 1
        name = os.path.join(out_dir, "crash_%d_%d.%s" % (SEED, it, "mcsb" if IMAGE else "cs"))
        os.replace(path, name)
        print("BUG", name, out.strip().splitlines()[-1][:160] if out.strip() else r.returncode)
print("fuzz: %d iterations, %d findings" % (N, found))
sys.exit(1 if found else 0)
