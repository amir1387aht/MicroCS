#!/usr/bin/env python3
"""MicroCS HTTP client tests: runs C# Http / TcpClient calls against a local HTTP server.

usage: python3 tests/test_net.py ./mcs
"""
import http.server, os, subprocess, sys, tempfile, threading

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def reply(self, code, body, ctype="text/plain", extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra: self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD": self.wfile.write(body)
    def do_GET(self):
        p = self.path
        if p == "/redir": return self.reply(302, b"", extra=[("Location", "/hello?x=1")])
        if p == "/redir-rel": return self.reply(301, b"", extra=[("Location", "hello")])
        if p == "/loop": return self.reply(302, b"", extra=[("Location", "/loop")])
        if p == "/chunked":
            self.send_response(200); self.send_header("Transfer-Encoding", "chunked"); self.end_headers()
            for part in [b"abc", b"defgh", b"X" * 3000]: self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n"); return
        # Deliberately fragmented or malformed wire responses (no public network needed).
        wire = {
            "/empty": b"",
            "/short-header": b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n",
            "/short-body": b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab",
            "/close-body": b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nclosed-body",
            "/bad-chunk": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n",
            "/huge-chunk": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\r\n",
            "/bad-terminator": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nabXX\r\n0\r\n\r\n",
            "/short-trailer": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Trailer: x\r\n",
            "/chunk-ext": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;foo=bar\r\nab\r\n0\r\nX-Trailer: x\r\n\r\n",
            "/long-header": b"HTTP/1.1 200 OK\r\nX-Long: " + b"x" * 300 + b"\r\n\r\n",
        }
        if p in wire:
            self.close_connection = True
            # Splitting inside CRLF and chunk sizes exercises the buffered reader.
            for i in range(0, len(wire[p]), 3):
                try: self.connection.sendall(wire[p][i:i+3])
                except (BrokenPipeError, ConnectionResetError): break
            return
        if p == "/missing": return self.reply(404, b"nf!")
        if p == "/json": return self.reply(200, b'{"temp":21.5,"ok":true}', "application/json")
        if p == "/big": return self.reply(200, b"B" * 70000)
        body = ("hello %s ua=%s key=%s" % (p, self.headers.get("User-Agent", ""), self.headers.get("X-Key"))).encode()
        self.reply(200, body)
    do_HEAD = do_GET
    def body_echo(self):
        n = int(self.headers.get("Content-Length", "0"))
        d = self.rfile.read(n)
        self.reply(201 if self.command == "POST" else 200,
                   b"%s %s ct=%s" % (self.command.encode(), d, self.headers.get("Content-Type", "-").encode()))
    do_POST = do_PUT = do_PATCH = do_DELETE = body_echo

SCRIPT = r'''
var b = "http://127.0.0.1:PORT";
var r = Http.Get(b + "/a", new string[] { "X-Key: 42" });
Console.WriteLine(r + " | " + r.Body + " | " + r.ContentType + " | " + r.Header("content-length") + " | " + r.IsSuccess);
r = Http.Get(b + "/redir"); Console.WriteLine(r.StatusCode + " " + r.Url + " " + r.Body.Length);
r = Http.Get(b + "/redir-rel"); Console.WriteLine(r.StatusCode + " " + r.Url);
r = Http.Get(b + "/loop"); Console.WriteLine("loop " + r.StatusCode);
Http.FollowRedirects = false; Console.WriteLine("no follow " + Http.Get(b + "/redir").StatusCode + " " + Http.Get(b + "/redir").Header("Location")); Http.FollowRedirects = true;
r = Http.Get(b + "/chunked"); Console.WriteLine("chunked " + r.StatusCode + " " + r.ContentLength + " " + r.Body.Substring(0, 8) + " " + r.Bytes.Length);
Console.WriteLine(Http.Post(b + "/p", "{\"a\":1}").Body);
Console.WriteLine(Http.Post(b + "/p", "t=1", "application/x-www-form-urlencoded").StatusCode);
Console.WriteLine(Http.Put(b + "/p", new byte[] { 65, 66 }, "application/octet-stream").Body);
Console.WriteLine(Http.Patch(b + "/p", "x").Body + " | " + Http.Delete(b + "/p").Body);
Console.WriteLine(Http.Request("post", b + "/p", "q", null, "Content-Type: text/csv").Body);
r = Http.Head(b + "/a"); Console.WriteLine("head " + r.StatusCode + " " + r.ContentLength + " " + r.Header("Content-Length"));
r = Http.Get(b + "/missing"); Console.WriteLine(r.StatusCode + " " + r.Reason + " " + r.IsSuccess + " " + r.Body);
try { Http.GetString(b + "/missing"); } catch (HttpRequestException e) { Console.WriteLine("HttpRequestException " + e.Message); }
try { Http.Get(b + "/missing").EnsureSuccess(); } catch (HttpRequestException) { Console.WriteLine("EnsureSuccess threw"); }
Console.WriteLine(Http.GetString(b + "/json"));
try { Http.Get(b + "/big"); } catch (IOException e) { Console.WriteLine("too big: IOException"); }
foreach (var path in new string[] { "/empty", "/short-header", "/short-body", "/bad-chunk", "/huge-chunk", "/bad-terminator", "/short-trailer", "/long-header" })
{
    try { Http.Get(b + path); Console.WriteLine("BAD accepted " + path); }
    catch (IOException e) { Console.WriteLine(path + ": " + e.Message); }
}
Console.WriteLine("close " + Http.GetString(b + "/close-body"));
Console.WriteLine("extension " + Http.GetString(b + "/chunk-ext"));
Console.WriteLine("download " + Http.Download(b + "/big", "/big.bin") + " " + File.ReadAllBytes("/big.bin").Length + " tmp gone " + !File.Exists("/big.bin.tmp"));
try { Http.Download(b + "/missing", "/m.txt"); } catch (HttpRequestException) { Console.WriteLine("download 404: " + File.Exists("/m.txt") + " " + File.Exists("/m.txt.tmp")); }
Http.UserAgent = "Greenhouse/2"; Console.WriteLine(Http.GetString(b + "/ua"));
Http.Timeout = 3000; Console.WriteLine("timeout " + Http.Timeout);
try { Http.Get("http://127.0.0.1:1/"); } catch (SocketException) { Console.WriteLine("refused"); }
try { Http.Get("http://no-such-host.invalid/"); } catch (SocketException e) { Console.WriteLine("unresolved"); }
var c = new TcpClient("127.0.0.1", PORT);
c.Write("GET /raw HTTP/1.0\r\nHost: x\r\n\r\n");
Console.WriteLine(c.ReadLine());
while (c.ReadLine() != "") {}
Console.WriteLine(c.ReadToEnd());
c.Close();
'''

EXPECT = """200 OK (SIZE_A bytes) | hello /a ua=MicroCS/VER key=42 | text/plain | SIZE_A | True
200 http://127.0.0.1:PORT/hello?x=1 SIZE_REDIRECT
200 http://127.0.0.1:PORT/hello
loop 302
no follow 302 /hello?x=1
chunked 200 3008 abcdefgh 3008
POST {"a":1} ct=application/json
201
PUT AB ct=application/octet-stream
PATCH x ct=text/plain; charset=utf-8 | DELETE  ct=-
POST q ct=text/csv
head 200 0 SIZE_HEAD
404 Not Found False nf!
HttpRequestException HTTP 404 Not Found: http://127.0.0.1:PORT/missing
EnsureSuccess threw
{"temp":21.5,"ok":true}
too big: IOException
/empty: Http: server closed before sending an HTTP response (try HTTPS or another endpoint)
/short-header: Http: connection closed while reading response headers
/short-body: Http: connection closed before the whole body arrived
/bad-chunk: Http: invalid chunk size
/huge-chunk: Http: invalid chunk size
/bad-terminator: Http: invalid chunk terminator
/short-trailer: Http: connection closed while reading chunk trailers
/long-header: Http: response headers too long
close closed-body
extension ab
download 70000 70000 tmp gone True
download 404: False False
hello /ua ua=Greenhouse/2 key=None
timeout 3000
refused
unresolved
HTTP/1.1 200 OK
hello /raw ua= key=None
"""

def main():
    mcs = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./mcs")
    feats = subprocess.run([mcs, "--features"], capture_output=True, text=True).stdout
    if " net" not in feats:
        print("SKIP network tests (built without MCS_ENABLE_NET)"); return 0
    ver = feats.split()[1].rstrip(",")
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "http.cs")
        open(src, "w").write(SCRIPT.replace("PORT", str(port)))
        out = subprocess.run([mcs, "--fs", d, src], capture_output=True, text=True, timeout=120)
    srv.shutdown()
    got = out.stdout + out.stderr
    # The fixture echoes the full runtime User-Agent; prerelease labels are longer.
    # Compute its exact UTF-8 lengths, never suppress content-length assertions.
    sizes = {"SIZE_A": len(("hello /a ua=MicroCS/%s key=42" % ver).encode()),
             "SIZE_REDIRECT": len(("hello /hello?x=1 ua=MicroCS/%s key=None" % ver).encode()),
             "SIZE_HEAD": len(("hello /a ua=MicroCS/%s key=None" % ver).encode())}
    want = EXPECT.replace("PORT", str(port)).replace("VER", ver)
    for token, size in sizes.items():
        want = want.replace(token, str(size))
    if got != want:
        import difflib
        print("FAIL network tests"); sys.stdout.writelines(difflib.unified_diff(want.splitlines(True), got.splitlines(True), "expected", "got"))
        return 1
    print("PASS network tests (%d checks)" % len(want.splitlines()))
    return 0

if __name__ == "__main__":
    sys.exit(main())
