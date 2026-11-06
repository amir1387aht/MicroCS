#!/usr/bin/env python3
"""A worker's console output must never become part of a binary download.

Run against a POSIX-threads build: python3 tests/test_shell_concurrent_download.py ./mcs
"""
import os, sys, tempfile, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import mcs_remote as remote

exe = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else './mcs')
with tempfile.TemporaryDirectory(prefix='mcs_frame_') as root:
    # Several output chunks are emitted during each transfer, including bytes
    # which would be valid-looking protocol text if interleaved.
    with open(os.path.join(root, 'log.cs'), 'w') as f:
        f.write('while (true) { Console.WriteLine("BACKGROUND_OUTPUT_0123456789"); Thread.Sleep(1); }\n')
    blob = bytes(range(256)) * 1024 + b'\x04OK\n\x03'
    with open(os.path.join(root, 'blob.bin'), 'wb') as f:
        f.write(blob)
    dev = remote.Device(remote.ProcLink(exe + ' --shell --no-boot --fs ' + root), timeout=20)
    try:
        assert dev.sync()[0] == 'OK'
        status, _ = dev.command('exec var logger = Thread.Start("/log.cs");')
        assert status == 'OK', status
        time.sleep(.05)
        log_seen = 0
        for i in range(10):
            dev.link.write(b'get /blob.bin\n')
            status, before = dev._status()
            assert status == 'DATA %d' % len(blob), (i, status)
            deadline = time.time() + dev.timeout
            while len(dev.buf) < len(blob):
                dev._fill(deadline)
            data, dev.buf = dev.buf[:len(blob)], dev.buf[len(blob):]
            status, after = dev._status()
            log_seen += before.count(b'BACKGROUND_OUTPUT_') + after.count(b'BACKGROUND_OUTPUT_')
            assert status == 'OK', (i, status)
            assert data == blob, ('corrupted download', i, len(data))
        status, text = dev.command('exec logger.Stop(); logger.Join(2000);')
        assert status == 'OK', (status, text)
        log_seen += text.count(b'BACKGROUND_OUTPUT_')
        assert log_seen > 0, 'worker must actually print while downloads run'
        print('PASS: binary downloads remain intact while a worker logs')
        dev.command('quit')
    finally:
        dev.link.close()
