"""Machine clients own input echo; human REPL echo and genuine script output stay intact."""
import subprocess, sys
from pathlib import Path
MCS=str(Path(sys.argv[1] if len(sys.argv)>1 else './mcs').resolve())
code='Console.Write("hex? "); var h = Console.ReadLine(); Console.WriteLine("result=" + Convert.ToInt32(h, 16));'
for mode in ['--shell','--repl']:
    for echo in [False,True]:
        # Ctrl-A selects machine mode, including when the device booted into an echoing REPL.
        p=subprocess.run([MCS,mode,*(['--echo'] if echo else [])],input=('\x01exec '+code+'\n0x01\r\nquit\n').encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
        assert p.returncode==0, p.stderr
        assert b'hex? result=1\n' in p.stdout, p.stdout
        assert b'0x01' not in p.stdout, p.stdout
for echo in [False,True]:
    p=subprocess.run([MCS,'--repl',*(['--echo'] if echo else [])],input=(code+'\n0x01\r\n').encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
    assert p.returncode==0 and b'result=1\n' in p.stdout, (p.stdout,p.stderr)
    assert (b'hex? 0x01\r\n' in p.stdout)==echo, p.stdout
# Do not suppress script output just because it equals the input (no heuristic deduplication).
p=subprocess.run([MCS,'--shell','--echo'],input=b'\x01exec Console.WriteLine(Console.ReadLine());\n0x01\nquit\n',stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
assert p.stdout.count(b'0x01')==1, p.stdout
print('PASS Console.ReadLine echo: machine/REPL, echo on/off, CRLF, and identical application output')
