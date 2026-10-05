#!/usr/bin/env python3
"""Protocol tests for the standalone runtime (mcs --shell) via tools/mcs_remote.py."""
import os, sys, time, tempfile, shutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import mcs_remote as R

MCS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./mcs")
fails = 0


def check(cond, what):
    global fails
    print(("PASS " if cond else "FAIL ") + what)
    fails += 0 if cond else 1


def open_dev(args):
    d = R.Device(R.ProcLink(MCS + " --shell " + args), timeout=10)
    return d, d.sync()


root = tempfile.mkdtemp(prefix="mcs_shell_")
try:
    with open(os.path.join(root, "boot.cs"), "w") as f:
        f.write('Console.WriteLine("boot");\n')
    with open(os.path.join(root, "main.cs"), "w") as f:
        f.write('Console.WriteLine("main " + File.Exists("/jobs.cfg"));\n')
    with open(os.path.join(root, "jobs.cfg"), "w") as f:
        f.write("every 30 /tick.cs restart=always\n")
    with open(os.path.join(root, "tick.cs"), "w") as f:
        f.write('File.AppendAllText("/ticks.txt", "t");\n')

    dev, (st, out) = open_dev("--fs " + root)
    check(st == "OK" and out.startswith(b"boot\nmain True\n"), "boot.cs then main.cs, jobs.cfg loaded")

    blob = bytes(range(256)) * 3 + b"\x04OK\n\x03"
    st, _ = dev.put(blob, "/blob.bin")
    check(st == "OK", "binary upload")
    st, data = dev.get("/blob.bin")
    check(st == "OK" and data == blob, "binary download matches (EOT, newline, Ctrl-C bytes inside)")
    check(not os.path.exists(os.path.join(root, "blob.bin.part")), "no temporary file left")

    st, out = dev.command("exec Console.WriteLine(Enumerable.Range(1, 4).Sum());")
    check(st == "OK" and out == b"10\n", "exec one-liner")

    dev.put(b"long i = 0; while (true) i++;\n", "/loop.cs")
    dev.link.write(b"run /loop.cs\n")
    time.sleep(0.3)
    dev.link.write(b"\x03")
    st, out = dev._status()
    check(st.startswith("ERR") and "aborted" in st, "Ctrl-C stops a running script (%s)" % st)
    st, out = dev.command("exec Console.WriteLine(\"alive\");")
    check(st == "OK" and out == b"alive\n", "runtime responsive after stop")

    time.sleep(0.2)
    st, out = dev.command("jobs")
    check(st == "OK" and b"active every 30 ms" in out, "jobs listing")
    st, out = dev.command("cat /ticks.txt")
    check(st == "OK" and out.count(b"t") >= 3, "periodic job ran while idle (%r)" % out[:20])

    st, _ = dev.command("bogus")
    check(st.startswith("ERR unknown command"), "unknown command reported")
    st, _ = dev.command("run /missing.cs")
    check(st.startswith("ERR") and "no such file" in st, "missing script reported")
    st, out = dev.command("exec throw new InvalidOperationException(\"boom\");")
    check(st.startswith("ERR Unhandled exception. InvalidOperationException: boom"), "script exception reported")
    st, _ = dev.command("quit")
    check(st == "OK", "quit")
    dev.link.close()

    # quota: a failed upload keeps the previous version of the file
    dev, (st, _) = open_dev("--ramfs 2048 --no-boot")
    dev.put(b"Console.WriteLine(\"v1\");", "/app.cs")
    st, _ = dev.put(b"x" * 4000, "/app.cs")
    check(st.startswith("ERR") and "space" in st, "upload over quota rejected")
    st, out = dev.command("run /app.cs")
    check(st == "OK" and out == b"v1\n", "previous version intact after failed upload")
    st, out = dev.command("ls")
    check(st == "OK" and out.strip() == b"f       24 app.cs", "no partial files (%r)" % out)
    dev.command("quit")
    dev.link.close()
finally:
    shutil.rmtree(root)
print("----\n%s" % ("all shell tests passed" if not fails else "%d shell tests FAILED" % fails))
sys.exit(1 if fails else 0)
