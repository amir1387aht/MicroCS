#!/usr/bin/env python3
"""MicroCS server-side network tests: a C# HttpServer (routes, parameters, forms, JSON, Basic auth,
redirects, 404 / 405 / 500, WebSocket endpoint + Broadcast) and DnsServer driven by Python clients,
the C# WebSocket client against a Python WebSocket server, and Wifi.Wps / Wifi.SmartConfig on the
simulated radio.

usage: python3 tests/test_net_server.py ./mcs
"""
import base64, difflib, hashlib, os, socket, struct, subprocess, sys, tempfile, threading, time
import urllib.request, urllib.error, json

def free_port(kind=socket.SOCK_STREAM):
    s = socket.socket(socket.AF_INET, kind); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

SERVER = r'''
class Payload {public int Base=21; public int Twice=>Base*2;}
var web = new HttpServer(PORT);
var dns = new DnsServer("10.0.0.1");
dns.Start(null, DNSPORT);
web.Get("/", req => "<h1>hi</h1>");
web.Get("/led/{id}", req => { req.SetHeader("X-Led", req.Param("id")); return "led " + req.Param("id") + " on=" + req.Param("on", "?"); });
web.Post("/form", req => "name=" + req.Param("name") + " pw=" + req.Param("pw"));
web.Post("/json", req => { var d = req.Json; return new Dictionary<string, object> { ["got"] = d["a"], ["m"] = req.Method }; });
web.Get("/computed", req => new Payload());
web.Get("/json-null", req => {req.SendJson(null);});
web.Get("/secret", req => { if (!req.Authenticate("admin", "pw")) return null; return "welcome"; });
web.Get("/go", req => req.Redirect("/"));
web.Get("/boom", req => { throw new InvalidOperationException("bad"); });
web.Get("/stop", req => { req.Send("bye"); web.Stop(); });
web.NotFound(req => { req.Send(404, "text/plain", "nope " + req.Path); });
web.WebSocket("/ws", (ws, msg) => { ws.Send("echo " + msg); if (msg == "all") web.Broadcast("bcast"); },
              ws => Console.WriteLine("open " + ws.Id + " " + ws.Path), ws => Console.WriteLine("closed " + ws.CloseStatus));
web.Start();
Console.WriteLine("listening " + (web.Port == PORT));
try { web.Run(); } catch (Exception e) { Console.WriteLine("Run threw: " + e.Message); web.Run(); }
Console.WriteLine("stopped, requests " + web.Requests + " dns " + dns.Queries);
'''
SERVER_EXPECT = """listening True
open 1 /ws
closed 1000
Run threw: bad
stopped, requests 15 dns 1
"""
CLIENT_EXPECT = """(200, '<h1>hi</h1>')
200 led 7 on=1 7
(200, 'name=Bob X pw=a&b')
(200, '{"got":5,"m":"POST"}')
computed 21 42
(200, 'null')
(401, '') (200, 'welcome')
(200, '<h1>hi</h1>')
(404, 'nope /missing')
(405, 'method not allowed')
dns 1234 True
b'HTTP/1.1 101 Switching Protocols'
(1, b'echo hello')
(1, b'echo all') (1, b'bcast')
(10, b'ping!')
(8, b'\\x03\\xe8')
(500, 'handler failed')
(200, 'bye')
"""

def http_client(port, dnsport, out):
    base = "http://127.0.0.1:%d" % port
    def get(p, data=None, h={}):
        r = urllib.request.Request(base + p, data=data, headers=h)
        try:
            with urllib.request.urlopen(r, timeout=5) as f: return f.status, f.read().decode(), dict(f.headers)
        except urllib.error.HTTPError as e: return e.code, e.read().decode(), dict(e.headers)
    for _ in range(100):                        # wait for the server
        try: socket.create_connection(("127.0.0.1", port), timeout=1).close(); break
        except OSError: time.sleep(0.1)
    p = lambda *a: out.append(" ".join(str(x) for x in a))
    p(get("/")[:2])
    s, b, h = get("/led/7?on=1"); p(s, b, h.get("X-Led"))
    p(get("/form", b"name=Bob+X&pw=a%26b", {"Content-Type": "application/x-www-form-urlencoded"})[:2])
    p(get("/json", b'{"a":5}', {"Content-Type": "application/json"})[:2])
    computed = json.loads(get("/computed")[1]); p("computed", computed["Base"], computed["Twice"])
    p(get("/json-null")[:2])
    p(get("/secret")[:2][0:1] + ("",), get("/secret", h={"Authorization": "Basic " + base64.b64encode(b"admin:pw").decode()})[:2])
    p(get("/go")[:2])
    p(get("/missing")[:2])
    p(get("/led/7", b"x")[:2])
    q = struct.pack(">HHHHHH", 0x1234, 0x0100, 1, 0, 0, 0) + b"\x07example\x03com\x00" + struct.pack(">HH", 1, 1)
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.settimeout(3); u.sendto(q, ("127.0.0.1", dnsport))
    a = u.recv(512); p("dns", a[:2].hex(), a[-4:] == bytes([10, 0, 0, 1]))
    c = socket.create_connection(("127.0.0.1", port), timeout=5); k = base64.b64encode(os.urandom(16)).decode()
    c.send(("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % k).encode())
    hd = b""
    while b"\r\n\r\n" not in hd: hd += c.recv(1)
    p(hd.split(b"\r\n")[0])
    def send(t, op=1):
        d = t.encode() if isinstance(t, str) else t; m = os.urandom(4)
        c.send(bytes([0x80 | op, 0x80 | len(d)]) + m + bytes(x ^ m[i % 4] for i, x in enumerate(d)))
    def recv():
        h = c.recv(2); n = h[1] & 127
        if n == 126: n = struct.unpack(">H", c.recv(2))[0]
        d = b""
        while len(d) < n: d += c.recv(n - len(d))
        return h[0] & 15, d
    send("hello"); p(recv())
    send("all"); p(recv(), recv())
    send(b"ping!", 9); p(recv())
    send(struct.pack(">H", 1000), 8); p(recv())
    c.close()
    p(get("/boom")[:2])
    time.sleep(0.3)
    p(get("/stop")[:2])

WSCLIENT = r'''
var ws = new WebSocket("ws://127.0.0.1:PORT/chat");
Console.WriteLine(ws.IsOpen + " " + ws.Path.EndsWith("/chat"));
Console.WriteLine(ws.Receive(3000));
ws.Send("abc"); Console.WriteLine(ws.Receive(3000));
ws.Send(new byte[] { 1, 2, 3 }); var b = (byte[])ws.Receive(3000); Console.WriteLine(b.Length + " " + b[2]);
int n = 0; ws.OnMessage(m => { Console.WriteLine("on " + m); n++; });
ws.Send("x"); ws.Poll(1000); Console.WriteLine("polled " + n + " null=" + (ws.Receive(200) == null));
ws.Close(1000, "done"); Console.WriteLine(ws.IsOpen + " " + ws.CloseStatus);
'''
WSCLIENT_EXPECT = """True True
hello
ec:abc
3 3
on ec:x
polled 1 null=True
False 1000
"""

def ws_server(lsock):
    conn, _ = lsock.accept()
    with conn:
        hd = b""
        while b"\r\n\r\n" not in hd: hd += conn.recv(1)
        key = [l.split(b": ")[1] for l in hd.split(b"\r\n") if l.lower().startswith(b"sec-websocket-key")][0]
        acc = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
        conn.send(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  b"Sec-WebSocket-Accept: " + acc + b"\r\n\r\n")
        conn.send(b"\x81\x05hello")
        conn.send(b"\x89\x02hi")                # a ping: the client answers with a pong
        while True:
            h = conn.recv(2)
            if len(h) < 2: return
            op = h[0] & 15; n = h[1] & 127
            if n == 126: n = struct.unpack(">H", conn.recv(2))[0]
            m = conn.recv(4); d = b""
            while len(d) < n: d += conn.recv(n - len(d))
            d = bytes(x ^ m[i % 4] for i, x in enumerate(d))
            if op == 8: conn.send(b"\x88\x02" + d[:2]); return
            if op == 10: continue
            if op == 1: conn.send(b"\x01\x03ec:" + bytes([0x80, len(d)]) + d)   # fragmented text
            else: conn.send(bytes([0x82, len(d)]) + d)

PROVISION = r'''
Console.WriteLine(Net.Has("Wifi.Wps") + " " + Net.Has("Wifi.SmartConfig") + " " + Net.Has("HttpServer") + " " + Net.Has("WebSocket") + " " + Net.Has("DnsServer"));
Console.WriteLine(Wifi.Wps(5000) + " " + Wifi.Ssid + " saved=" + Wifi.SavedSsid);
Wifi.Disconnect(); Wifi.Forget();
Console.WriteLine(Wifi.SmartConfig(5000, false) + " saved='" + Wifi.SavedSsid + "'");
'''
PROVISION_EXPECT = """True True True True True
True MicroCS-Lab saved=MicroCS-Lab
True saved=''
"""

def run(mcs, d, name, src, extra=(), timeout=60):
    path = os.path.join(d, name)
    with open(path, "w") as f: f.write(src)
    return subprocess.run([mcs, *extra, "--fs", d, path], capture_output=True, text=True, timeout=timeout)

def check(label, want, got, fails):
    if want == got: return 1
    fails.append(label); print("FAIL " + label)
    sys.stdout.writelines(difflib.unified_diff(want.splitlines(True), got.splitlines(True), "expected", "got"))
    return 0

def main():
    mcs = sys.argv[1] if len(sys.argv) > 1 else "./mcs"
    feats = subprocess.run([mcs, "--features"], capture_output=True, text=True).stdout
    if " net" not in feats:
        print("SKIP server network tests (built without MCS_ENABLE_NET)"); return 0
    fails, checks = [], 0
    with tempfile.TemporaryDirectory() as d:
        port, dnsport = free_port(), free_port(socket.SOCK_DGRAM)
        out = []
        t = threading.Thread(target=lambda: http_client(port, dnsport, out), daemon=True)
        t.start()
        r = run(mcs, d, "server.cs", SERVER.replace("DNSPORT", str(dnsport)).replace("PORT", str(port)))
        t.join(10)
        checks += check("HttpServer (C# side)", SERVER_EXPECT, r.stdout + r.stderr, fails)
        checks += check("HttpServer (client side)", CLIENT_EXPECT, "".join(l + "\n" for l in out), fails)

        ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); ls.bind(("127.0.0.1", 0)); ls.listen(1)
        wport = ls.getsockname()[1]
        threading.Thread(target=ws_server, args=(ls,), daemon=True).start()
        r = run(mcs, d, "wsclient.cs", WSCLIENT.replace("PORT", str(wport)))
        ls.close()
        checks += check("WebSocket client", WSCLIENT_EXPECT, r.stdout + r.stderr, fails)

        r = run(mcs, d, "provision.cs", PROVISION, extra=("--sim",))
        checks += check("Wifi.Wps / SmartConfig (--sim)", PROVISION_EXPECT, r.stdout + r.stderr, fails)
    if fails: return 1
    print("PASS server network tests (%d groups)" % checks)
    return 0

if __name__ == "__main__":
    sys.exit(main())
