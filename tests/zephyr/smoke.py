#!/usr/bin/env python3
"""Smoke test for the Zephyr port on native_sim: drives the example firmware's
console (stdin/stdout, CONFIG_NATIVE_UART_0_ON_STDINOUT) like MicroCS Studio
and tools/mcs_remote.py do - REPL, machine protocol, LittleFS on the flash
simulator (kept across a restart), jobs and job cancelling.

    python3 tests/zephyr/smoke.py build/zephyr/zephyr.exe
"""
import os
import subprocess
import sys
import tempfile
import threading
import time

EXE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/zephyr/zephyr.exe")
FS = sys.argv[2] if len(sys.argv) > 2 else "littlefs"   # expected filesystem: littlefs | yaffs2
WORK = tempfile.mkdtemp(prefix="mcs_zephyr_")
fails = 0


class Board:
    def __init__(self):
        self.p = subprocess.Popen([EXE, "-flash=" + os.path.join(WORK, "flash.bin")], cwd=WORK,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.buf = b""
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            b = self.p.stdout.read1(4096)
            if not b:
                return
            with self.lock:
                self.buf += b

    def send(self, data):
        self.p.stdin.write(data if isinstance(data, bytes) else data.encode())
        self.p.stdin.flush()

    def wait_for(self, token, timeout=5.0):
        token = token.encode() if isinstance(token, str) else token
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                i = self.buf.find(token)
                if i >= 0:
                    out, self.buf = self.buf[:i + len(token)], self.buf[i + len(token):]
                    return out.decode(errors="replace")
            time.sleep(0.01)
        with self.lock:
            raise TimeoutError(f"no {token!r} in {self.buf[-400:]!r}")

    def cmd(self, line):
        """machine-protocol command -> (status, output)"""
        self.send(line + "\n")
        out = self.wait_for(b"\x04")
        status = self.wait_for(b"\n").strip()
        return status, out[:-1]

    def close(self):
        self.p.kill()
        self.p.wait()


def check(name, cond, detail=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else f"  -> {detail!r}"))
    if not cond:
        fails += 1


b = Board()
try:
    banner = b.wait_for("> ", 10)
    check("REPL banner", "C# REPL" in banner, banner)
    b.send("var x = 6 * 7;\n")
    b.wait_for("> ")
    b.send("x\n")
    check("REPL expression", "42" in b.wait_for("> "))
    b.send("Hal.Board\n")
    check("Hal.Board", "native_sim" in b.wait_for("> "))
    b.send("\x01")                                    # Ctrl-A: machine mode
    b.wait_for(b"\x04OK\n")
    st, out = b.cmd("info")
    check("info", st == "OK" and "MicroCS" in out, out)
    st, out = b.cmd("df")
    check("df reports " + FS, st == "OK" and FS in out, out)
    code = b'Console.WriteLine("sum " + (1 + 2 + 3));\n'
    b.send(f"put /main2.cs {len(code)}\n")
    b.wait_for(b"\x04READY\n")
    b.send(code)
    st = b.wait_for(b"\n", 5)
    check("put", "OK" in st, st)
    st, out = b.cmd("ls")
    check("ls lists the file", st == "OK" and "main2.cs" in out, out)
    st, out = b.cmd("cat /main2.cs")
    check("cat", out.strip() == code.decode().strip(), out)
    st, out = b.cmd("run /main2.cs")
    check("run", st == "OK" and "sum 6" in out, out)
    st, out = b.cmd("mkdir /lib")
    st2, out = b.cmd("mv /main2.cs /lib/m.cs")
    st3, out = b.cmd("ls /lib")
    check("mkdir + mv", st == "OK" and st2 == "OK" and "m.cs" in out, out)
    st, out = b.cmd('exec Scheduler.Every(100, () => Console.WriteLine("tick"));')
    time.sleep(0.5)
    st, out = b.cmd("jobs")
    check("delegate job runs after its script ended", "active" in out and "<delegate>" in out, out)
    st, out = b.cmd("cancel scripts")
    check("cancel scripts", st == "OK" and "cancelled 1" in out, out)
    time.sleep(0.3)
    with b.lock:
        b.buf = b""
    time.sleep(0.4)
    with b.lock:
        quiet = b"tick" not in b.buf
    check("no ticks after cancel", quiet)
    st, out = b.cmd('exec CAN.Open(0, 500000); CAN.OnReceive(0, (int n) => { CanFrame f; while ((f = CAN.Receive(0)) != null) Console.WriteLine("can " + f.Id + " len " + f.Length); }); CAN.Send(0, 0x123, new byte[] { 1, 2, 3 });')
    check("CAN.Open/Send on the loopback controller", st == "OK", (st, out))
    time.sleep(0.5)
    st, out = b.cmd("info")
    check("CAN.OnReceive callback ran", "can 291 len 3" in out, out)
    put = b'Console.WriteLine("boot ok");\n'
    b.send(f"put /main.cs {len(put)}\n")
    b.wait_for(b"\x04READY\n")
    b.send(put)
    b.wait_for(b"\n")
finally:
    b.close()

b = Board()                                           # restart: files must survive
try:
    out = b.wait_for("> ", 10)
    check("main.cs runs at boot from flash", "boot ok" in out, out)
    b.send("\x01")
    b.wait_for(b"\x04OK\n")
    st, out = b.cmd("ls /lib")
    check("files persist across restart", "m.cs" in out, out)
    st, out = b.cmd("rm /lib/m.cs")
    st2, _ = b.cmd("rm /lib")
    st3, _ = b.cmd("rm /main.cs")
    check("rm file + dir", st == st2 == st3 == "OK", (st, st2, st3))
finally:
    b.close()

print("ALL PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
