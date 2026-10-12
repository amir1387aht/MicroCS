#!/usr/bin/env python3
"""HttpServer / HttpRequest / WebSocket / DnsServer end-to-end test: python3 tests/test_web.py ./mcs"""
import subprocess,time,socket,urllib.request,urllib.error,base64,os,struct,sys,tempfile
SRC=r"""var server = new HttpServer(18081);
server.Get("/", req => "<h1>hi</h1>");
server.Get("/api/{id}", req => new Dictionary<string, object> { ["id"] = req.Param("id"), ["q"] = req.Param("q", "none") });
server.Post("/echo", req => { req.Send(201, req.Body, "text/plain"); return null; });
server.Post("/json", req => { var j = req.Json(); return j["a"]; });
server.Post("/form", req => req.Form["name"]);
server.Get("/boom", req => { throw new Exception("bad"); });
server.Get("/code", req => 418);
server.Get("/files/*", req => req.Param("*"));
server.WebSocket("/ws", ws => { ws.OnMessage = (msg) => { ws.Send("echo:" + msg); }; ws.Send("hello"); });
var dns = new DnsServer();
dns.Start("10.0.0.1", 18053);
server.Start();
Console.WriteLine("ready " + server.Port);
server.Run();
"""
if "net" not in subprocess.run([sys.argv[1],"--features"],capture_output=True,text=True).stdout.lower(): print("web: skipped (no net)"); sys.exit(0)
d=tempfile.mkdtemp(); open(os.path.join(d,"s.cs"),"w").write(SRC)
p=subprocess.Popen([sys.argv[1],os.path.join(d,"s.cs")],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
time.sleep(1)
try:
  pass
except: pass
import atexit; atexit.register(lambda: p.kill())
def get(path,data=None,ct=None):
    r=urllib.request.Request("http://127.0.0.1:18081"+path,data=data)
    if ct: r.add_header("Content-Type",ct)
    try:
        x=urllib.request.urlopen(r,timeout=3); return x.status,x.read().decode(),x.headers.get("Content-Type")
    except urllib.error.HTTPError as e: return e.code,e.read().decode(),None
ok=0;bad=0
def chk(name,a,b):
    global ok,bad
    if a==b: ok+=1
    else: bad+=1; print("FAIL",name,a,b)
chk("root",get("/")[:2],(200,"<h1>hi</h1>"))
chk("roottype",get("/")[2],"text/html; charset=utf-8")
chk("param",get("/api/7?q=x%20y")[1],'{"id":"7","q":"x y"}')
chk("echo",get("/echo",b"abc")[:2],(201,"abc"))
chk("json",get("/json",b'{"a":5}',"application/json")[1],"5")
chk("form",get("/form",b"name=Bob+X","application/x-www-form-urlencoded")[1],"Bob X")
chk("boom",get("/boom")[0],500)
chk("code",get("/code")[0],418)
chk("star",get("/files/a/b.txt")[1],"a/b.txt")
chk("404",get("/nope")[0],404)
chk("405",get("/echo")[0],405)
s=socket.create_connection(("127.0.0.1",18081))
key=base64.b64encode(os.urandom(16)).decode()
s.send(f"GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
h=b""
while b"\r\n\r\n" not in h: h+=s.recv(1)
chk("101",h[:12],b"HTTP/1.1 101")
def rf():
    b=s.recv(2); n=b[1]&127; return s.recv(n).decode()
chk("hello",rf(),"hello")
m=b"ping";k=os.urandom(4)
s.send(bytes([0x81,0x80|len(m)])+k+bytes(c^k[i%4] for i,c in enumerate(m)))
chk("wsecho",rf(),"echo:ping")
s.close()
u=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);u.settimeout(2)
q=b"\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x07example\x03com\x00\x00\x01\x00\x01"
u.sendto(q,("127.0.0.1",18053));r,_=u.recvfrom(512)
chk("dns",r[-4:],bytes([10,0,0,1]))
p.kill();out=p.stdout.read()
chk("boommsg","HttpServer: GET /boom: Exception: bad" in out,True)
print(f"{ok} passed, {bad} failed");
if bad: print(out); sys.exit(1)
