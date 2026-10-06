#!/usr/bin/env python3
"""mcs_remote - manage scripts on a MicroCS device running the shell.

  mcs_remote.py --port /dev/ttyUSB0 [--baud 115200] <command> [args]
  mcs_remote.py --exec "./mcs --shell --fs dev_root" <command> [args]

Commands: ls [dir], cat <f>, put <local> [remote], get <remote> [local], rm <f>,
mkdir <d>, mv <a> <b>, run <f>, exec <code>, jobs, every <t> <f>, after <t> <f>,
cancel <id>, mem, info. Several commands can be chained with "+".
  repl      interactive C# prompt in this terminal (Ctrl-] quits)
Exit status is 0 when every command succeeded.

Before the first command the tool sends Ctrl-A, which switches a device that
sits in the interactive REPL back to the machine protocol (replies EOT OK).

Only the Python standard library is used (termios for serial ports).
"""
import os, sys, subprocess, time, argparse, select, shlex

EOT = b"\x04"


class Link:
    def read(self, n, timeout):
        raise NotImplementedError

    def write(self, data):
        raise NotImplementedError

    def close(self):
        pass


class ProcLink(Link):
    def __init__(self, cmd):
        self.p = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)

    def read(self, n, timeout):
        r, _, _ = select.select([self.p.stdout], [], [], timeout)
        return os.read(self.p.stdout.fileno(), n) if r else b""

    def write(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


class SerialLink(Link):
    def __init__(self, port, baud):
        import termios, tty
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        speed = getattr(termios, "B%d" % baud)
        attrs[4] = attrs[5] = speed
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def read(self, n, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        return os.read(self.fd, n) if r else b""

    def write(self, data):
        os.write(self.fd, data)

    def close(self):
        os.close(self.fd)


class Device:
    def __init__(self, link, timeout=10.0):
        self.link, self.timeout, self.buf = link, timeout, b""

    def _fill(self, deadline):
        chunk = self.link.read(4096, max(0.0, deadline - time.time()))
        if not chunk and time.time() >= deadline:
            raise TimeoutError("device did not answer")
        self.buf += chunk

    def _status(self, echo=None):
        """Read output until an EOT status line; return (status, text)."""
        deadline = time.time() + self.timeout
        out = b""
        while True:
            i = self.buf.find(EOT)
            j = self.buf.find(b"\n", i) if i >= 0 else -1
            if j >= 0:
                out += self.buf[:i]
                status = self.buf[i + 1:j].decode(errors="replace")
                self.buf = self.buf[j + 1:]
                if echo:
                    echo(out)
                return status, out
            self._fill(deadline)

    def sync(self):
        return self._status()

    def machine_mode(self):
        """Ctrl-A: leave the REPL (or a half-typed line) and get a clean status."""
        self.link.write(b"\x01")
        deadline = time.time() + min(self.timeout, 3.0)
        while time.time() < deadline:
            i = self.buf.rfind(EOT + b"OK\n")
            if i >= 0:
                self.buf = self.buf[i + 4:]
                return True
            chunk = self.link.read(4096, max(0.0, deadline - time.time()))
            self.buf += chunk
        return False

    def command(self, line, echo=None):
        self.link.write(line.encode() + b"\n")
        return self._status(echo)

    def put(self, data, remote):
        self.link.write(("put %s %d\n" % (remote, len(data))).encode())
        st, _ = self._status()
        if st != "READY":
            return st, b""
        self.link.write(data)
        return self._status()

    def get(self, remote):
        self.link.write(("get %s\n" % remote).encode())
        st, _ = self._status()
        if not st.startswith("DATA "):
            return st, b""
        n = int(st[5:])
        deadline = time.time() + self.timeout
        while len(self.buf) < n:
            self._fill(deadline)
        data, self.buf = self.buf[:n], self.buf[n:]
        st, _ = self._status()
        return st, data


def terminal(dev):
    """Interactive REPL session: raw keyboard -> device, device -> screen."""
    import termios, tty
    dev.link.write(b"repl\n")
    sys.stdout.write("[MicroCS REPL - Ctrl-] to quit]\n")
    sys.stdout.flush()
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd) if os.isatty(fd) else None
    try:
        if old:
            tty.setraw(fd)
        if dev.buf:
            sys.stdout.buffer.write(dev.buf); dev.buf = b""
        while True:
            r, _, _ = select.select([fd], [], [], 0.02)
            if r:
                k = os.read(fd, 64)
                if not k or b"\x1d" in k:
                    break
                dev.link.write(k.replace(b"\n", b"\r"))
            out = dev.link.read(4096, 0.01)
            if out:
                sys.stdout.buffer.write(out.replace(b"\n", b"\r\n") if old else out)
                sys.stdout.flush()
    finally:
        if old:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
    print()
    dev.buf = b""
    return "OK" if dev.machine_mode() else "OK"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--exec", dest="exec_cmd", help="run a local shell process instead of a serial port")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    if not a.args:
        ap.error("no command")
    link = ProcLink(a.exec_cmd) if a.exec_cmd else SerialLink(a.port, a.baud) if a.port else None
    if not link:
        ap.error("need --port or --exec")
    dev = Device(link, a.timeout)
    if a.exec_cmd:
        dev.sync()      # a fresh process prints its boot banner + status first
    elif not dev.machine_mode():
        print("warning: no answer to Ctrl-A; is the MicroCS shell running?", file=sys.stderr)
    echo = lambda b: (sys.stdout.buffer.write(b), sys.stdout.flush())
    cmds, cur = [], []
    for t in a.args:
        if t == "+":
            cmds.append(cur); cur = []
        else:
            cur.append(t)
    cmds.append(cur)
    rc = 0
    for c in cmds:
        if not c:
            continue
        op = c[0]
        if op == "put":
            local = c[1]
            remote = c[2] if len(c) > 2 else "/" + os.path.basename(local)
            with open(local, "rb") as f:
                st, _ = dev.put(f.read(), remote)
        elif op == "get":
            st, data = dev.get(c[1])
            if st == "OK":
                if len(c) > 2:
                    with open(c[2], "wb") as f:
                        f.write(data)
                else:
                    sys.stdout.buffer.write(data)
        elif op == "repl":
            st = terminal(dev)
        elif op == "exec":
            st, _ = dev.command("exec " + " ".join(c[1:]), echo)
        else:
            st, _ = dev.command(" ".join(c), echo)
        if st != "OK":
            print("error: %s" % st, file=sys.stderr)
            rc = 1
            break
    link.close()
    sys.exit(rc)


if __name__ == "__main__":
    main()
