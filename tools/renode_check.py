#!/usr/bin/env python3
"""Boot a MicroCS STM32 firmware in the Renode emulator and talk to its C# REPL.

    python3 tools/renode_check.py <renode> <firmware.elf> <platform.repl> <uart> [port]

Checks the banner, an expression, Hal.Board, a file written to (emulated) internal
flash through TinyFS, and the LED pin. Used by CI for the boards Renode models
(STM32F4 / STM32H743); exits non-zero on a mismatch.
"""
import os, socket, subprocess, sys, tempfile, time

renode, elf, platform, uart = sys.argv[1:5]
port = int(sys.argv[5]) if len(sys.argv) > 5 else 33450
renode_dir = os.path.dirname(os.path.abspath(renode))
resc = tempfile.NamedTemporaryFile("w", suffix=".resc", delete=False)
resc.write(f'''mach create "board"
machine LoadPlatformDescription @{platform}
sysbus LoadELF @{os.path.abspath(elf)}
emulation CreateServerSocketTerminal {port} "term" false
connector Connect {uart} term
logLevel 3
start
''')
resc.close()
log = open(resc.name + ".log", "w")
proc = subprocess.Popen([os.path.abspath(renode), "--disable-gui", "--console", "-e", f"include @{resc.name}"],
                        cwd=renode_dir, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
out = b""
sock = None
try:
    for _ in range(240):
        try:
            sock = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            time.sleep(0.5)
    if sock is None:
        sys.exit("renode did not open the UART socket (see %s.log)" % resc.name)
    sock.settimeout(0.5)

    def wait_for(text, timeout, start=0):
        """read until `text` appears after offset `start` of the output"""
        global out
        end = time.time() + timeout
        while text.encode() not in out[start:] and time.time() < end:
            try:
                d = sock.recv(4096)
                if d:
                    out += d
            except socket.timeout:
                pass
        return text.encode() in out[start:]

    def ask(line, expect, timeout=90):
        mark = len(out)
        sock.sendall(line.encode() + b"\r\n")
        # the echo of the line comes first; the answer follows it
        if not wait_for(line, timeout, mark) or not wait_for(expect, timeout, out.index(line.encode(), mark) + len(line)):
            print(out.decode("utf-8", "replace"))
            sys.exit(f"FAIL: '{line}' did not print '{expect}'")

    sock.sendall(b"\r\n")                  # the banner may have gone out before we connected
    if not wait_for("> ", 120):
        print(out.decode("utf-8", "replace"))
        sys.exit("FAIL: no REPL prompt")
    ask("Console.WriteLine(6 * 7);", "42")
    ask('Console.WriteLine("board=" + Hal.Board);', "board=")
    ask('File.WriteAllText("/ci.txt", "flash ok");', "> ")
    ask('Console.WriteLine(File.ReadAllText("/ci.txt"));', "flash ok")
    ask(".df", "tinyfs")
    ask('var led = new Pin("LED"); led.Write(true); Console.WriteLine("led=" + led.Read());', "led=True")
    print(out.decode("utf-8", "replace"))
    print("OK", os.path.basename(elf))
finally:
    if sock:
        sock.close()
    proc.kill()
