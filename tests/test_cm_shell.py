#!/usr/bin/env python3
"""Standalone-runtime protocol test against the Cortex-M33 firmware running in
the Unicorn emulator (tools/cm_emu.py). Same protocol as the host shell tests."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import mcs_remote as R

ROOT = os.path.join(HERE, "..")
ELF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "cm", "m33-shell.elf")
fails = 0


def check(cond, what):
    global fails
    print(("PASS " if cond else "FAIL ") + what)
    fails += 0 if cond else 1


emu = "python3 %s %s --cpu m33 --quiet" % (os.path.join(ROOT, "tools", "cm_emu.py"), ELF)
dev = R.Device(R.ProcLink(emu), timeout=120)
st, out = dev.sync()
check(st == "OK" and b"churn acc=89700" in out, "firmware booted, image demo ran, shell ready")
st, _ = dev.put(b'var l = new List<int>{3,1,2}; l.Sort(); Console.WriteLine(string.Join("-", l));\n'
                b'File.WriteAllText("/out.txt", "from-script");\n', "/app.cs")
check(st == "OK", "upload script over UART")
st, out = dev.command("run /app.cs")
check(st == "OK" and out == b"1-2-3\n", "compile + run uploaded script on the device")
st, out = dev.command("cat /out.txt")
check(st == "OK" and out.strip() == b"from-script", "script wrote a file in the RAM filesystem")
st, out = dev.command("ls /")
check(st == "OK" and b"app.cs" in out and b"out.txt" in out, "ls lists uploaded + generated files")
st, out = dev.command("run /missing.cs")
check(st.startswith("ERR"), "missing script reports ERR")
st, out = dev.command("mem")
check(st == "OK" and len(out) > 0, "mem command")
dev.command("quit")
print("----")
print("all cortex-m shell tests passed" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
