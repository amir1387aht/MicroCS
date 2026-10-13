#!/usr/bin/env python3
"""Foreground server lifecycle: Stop latency, idle work, EOF / error cleanup and same-port reruns."""
import os, sys, tempfile, socket, time, threading, urllib.request, shlex
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
import mcs_remote as R

def port():
    with socket.socket() as s: s.bind(('127.0.0.1', 0)); return s.getsockname()[1]

def main():
    mcs = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else './mcs')
    import subprocess
    if ' net' not in subprocess.check_output([mcs, '--features']).decode():
        print('SKIP network lifecycle (net disabled)'); return
    with tempfile.TemporaryDirectory() as root:
        link = R.ProcLink(shlex.quote(mcs) + ' --shell --sim --fs ' + shlex.quote(root))
        dev = R.Device(link, timeout=4)
        assert dev.sync()[0] == 'OK'
        def put(name, text):
            assert dev.put(text.encode(), '/' + name)[0] == 'OK'
        def start(name):
            link.write(('run /' + name + '\n').encode())
            deadline = time.time() + 4
            while b'listening\n' not in dev.buf:
                dev._fill(deadline)
                assert b'\x04ERR' not in dev.buf, dev.buf
            dev.buf = dev.buf.split(b'listening\n', 1)[1]
        def stop():
            t = time.monotonic(); link.write(b'\x03')
            status, out = dev._status()
            assert status.startswith('ERR') and 'aborted' in status, (status, out)
            assert time.monotonic() - t < 2, 'Stop must not wait for a client timeout'
            assert dev.command('exec Console.WriteLine("alive");') == ('OK', b'alive\n')
        p = port(); base = 'http://127.0.0.1:' + str(p)
        setup = f'var web = new HttpServer({p}); web.Get("/", req => "ok"); web.Start(); '
        try:
            # Completion / errors: an unreachable server must not wait for GC to release its port.
            put('end.cs', setup + 'Console.WriteLine("ended");')
            put('error.cs', setup + 'throw new InvalidOperationException("test");')
            for _ in range(12):
                assert dev.command('run /end.cs') == ('OK', b'ended\n')
                assert dev.command('run /error.cs')[0].startswith('ERR Unhandled exception')
            # An existing interactive listener belongs to its earlier scope, not this file.
            prior = port()
            assert dev.command(f'exec var prior = new HttpServer({prior}); prior.Start();')[0] == 'OK'
            assert dev.command('run /end.cs')[0] == 'OK'
            assert dev.command('exec Console.WriteLine(prior.Running);') == ('OK', b'True\n')
            assert dev.command('exec prior.Dispose();')[0] == 'OK'
            put('run.cs', setup + 'Console.WriteLine("listening"); web.Run();')
            for _ in range(3):
                start('run.cs')
                with urllib.request.urlopen(base, timeout=2) as r: assert r.read() == b'ok'
                stop()   # immediately rebind the exact same port, including TIME_WAIT connections
            start('run.cs')
            with socket.create_connection(('127.0.0.1', p), timeout=2) as client:
                client.sendall(b'GET / HTTP/1.1\r\nHost: ')  # stalled partial request
                time.sleep(.1); stop()
                client.settimeout(2); assert client.recv(1000) == b''
            # Cancellation inside a nested route callback cannot be caught as an exception.
            put('handler.cs', f'var web = new HttpServer({p}); ' +
                'web.Get("/", req => { Thread.Sleep(5000); return "late"; }); web.Start(); ' +
                'Console.WriteLine("listening"); try { web.Run(); } catch (Exception) { Console.WriteLine("caught"); }')
            start('handler.cs')
            with socket.create_connection(('127.0.0.1', p), timeout=2) as client:
                client.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n')
                time.sleep(.15); stop(); client.settimeout(2); assert client.recv(1000) == b''
            assert dev.command('run /end.cs')[0] == 'OK'
            # Open WebSockets are force-closed without waiting for a close handshake.
            put('ws.cs', f'var web = new HttpServer({p}); web.WebSocket("/ws", (ws, msg) => ws.Send(msg)); ' +
                'web.Start(); Console.WriteLine("listening"); web.Run();')
            start('ws.cs')
            with socket.create_connection(('127.0.0.1', p), timeout=2) as client:
                client.sendall(b'GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                    b'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n')
                response = b''
                while b'\r\n\r\n' not in response: response += client.recv(1)
                assert b'101 Switching Protocols' in response, response
                stop(); client.settimeout(2); assert client.recv(1000) == b''
            assert dev.command('run /end.cs')[0] == 'OK'
            # Poll(0) under ready traffic still runs hook / idle work.
            put('busy.cs', setup + 'Console.WriteLine("listening"); while (true) web.Poll(0);')
            start('busy.cs'); done = threading.Event()
            def requests():
                while not done.is_set():
                    try:
                        with urllib.request.urlopen(base, timeout=.5) as r: r.read()
                    except Exception: pass
            thread = threading.Thread(target=requests, daemon=True); thread.start()
            time.sleep(.2); stop(); done.set(); thread.join(2)
            # HAL / scheduler idle callback is serviced inside a native Run(), not just Sleep().
            put('idle.cs', 'Scheduler.Every(20, () => File.AppendAllText("/ticks", "x")); ' +
                setup + 'Console.WriteLine("listening"); web.Run();')
            start('idle.cs'); time.sleep(.15); stop()
            assert dev.command('cancel all')[0] == 'OK'
            assert len(dev.command('cat /ticks')[1].strip()) >= 2, 'scheduler starved in web.Run()'
            # Raw TCP listener and accepted client are also cleaned on Ctrl-C.
            put('raw.cs', f'var raw = new TcpListener({p}); raw.Start(); Console.WriteLine("listening"); '
                'var client = raw.AcceptTcpClient(); client.ReadLine();')
            start('raw.cs')
            with socket.create_connection(('127.0.0.1', p), timeout=2) as client:
                time.sleep(.1); stop(); client.settimeout(2); assert client.recv(1) == b''
            assert dev.command('run /end.cs')[0] == 'OK'
        finally:
            link.write(b'\x03'); link.close()
    print('PASS network lifecycle: normal/error/abort cleanup, repeated binds, partial clients, busy traffic, idle jobs')

if __name__ == '__main__': main()
