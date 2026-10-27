"""No root/network: verify bounded CI package commands and failure propagation."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    d = Path(tmp)
    mock = '''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
name=Path(sys.argv[0]).name
with open(os.environ['APT_TEST_LOG'],'a') as f: f.write(json.dumps([name]+sys.argv[1:])+'\\n')
a=sys.argv[1:]
if name=='lsb_release':
    assert a==['-sc']
    print(os.environ.get('TEST_SUITE','noble')); sys.exit(0)
if name=='dpkg':
    assert a==['--print-architecture']
    print(os.environ.get('TEST_ARCH','amd64')); sys.exit(0)
if name=='timeout':
    assert a[:1]==['--kill-after=15s'] and a[1] in ('5m','10m')
    os.execvp(a[2],a[2:])
if name=='sudo':
    assert a[:3]==['-n','env','DEBIAN_FRONTEND=noninteractive']
    os.environ['DEBIAN_FRONTEND']='noninteractive'
    os.execvp(a[3],a[3:])
assert os.environ['DEBIAN_FRONTEND']=='noninteractive'
assert all(x in a for x in ['Acquire::http::Timeout=30','Acquire::https::Timeout=30','Acquire::Retries=2','DPkg::Lock::Timeout=60'])
source=next(x.split('=',1)[1] for x in a if x.startswith('Dir::Etc::sourcelist='))
text=Path(source).read_text()
assert Path(source).parent.stat().st_mode & 0o777 == 0o755
assert len(text.splitlines())==3 and 'http:' not in text and 'azure' not in text
assert text.count('signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg')==3
assert 'noble-updates' in text and 'noble-security' in text
if os.environ.get('TEST_ARCH','amd64')=='arm64':
    assert text.count('https://ports.ubuntu.com/ubuntu-ports')==3
else:
    assert text.count('https://archive.ubuntu.com/ubuntu')==2
    assert text.count('https://security.ubuntu.com/ubuntu')==1
assert 'Dir::Etc::sourceparts=-' in a and 'APT::Get::List-Cleanup=0' in a
if 'update' in a:
    assert 'APT::Update::Error-Mode=any' in a
    sys.exit(int(os.environ.get('UPDATE_RC','0')))
assert 'install' in a and '--no-install-recommends' in a
assert a[-2:]==['gcc-arm-none-eabi','libnewlib-arm-none-eabi']
sys.exit(int(os.environ.get('INSTALL_RC','0')))
'''
    for name in ('sudo','timeout','apt-get','lsb_release','dpkg'):
        p = d/name
        p.write_text(mock)
        p.chmod(0o755)
    env = dict(os.environ, PATH=str(d)+os.pathsep+os.environ['PATH'], APT_TEST_LOG=str(d/'log'))
    def run(args, update=0, install=0, arch="amd64", suite="noble"):
        (d/'log').write_text('')
        r=subprocess.run(['sh',str(ROOT/'tools/ci_apt.sh')]+args,
                         env=dict(env,UPDATE_RC=str(update),INSTALL_RC=str(install),TEST_ARCH=arch,TEST_SUITE=suite),capture_output=True,text=True)
        rows=[json.loads(x) for x in (d/'log').read_text().splitlines()]
        for row in rows:
            for arg in row:
                if arg.startswith('Dir::Etc::sourcelist='):
                    assert not Path(arg.split('=',1)[1]).exists(), 'temporary sources leaked'
        return r.returncode,[row for row in rows if row[0] not in ('lsb_release','dpkg')]
    args=['gcc-arm-none-eabi','libnewlib-arm-none-eabi']
    rc, rows=run(args)
    assert rc==0 and len(rows)==6
    assert [row[2] for row in rows if row[0]=='timeout']==['5m','10m']
    rc, rows=run(args,update=41)
    assert rc==41 and len(rows)==3 and not any('install' in row for row in rows)
    rc, rows=run(args,install=42)
    assert rc==42 and len(rows)==6
    rc, rows=run(args,arch='arm64')
    assert rc==0 and len(rows)==6
    rc, rows=run(args,suite='../bad')
    assert rc==2 and not rows
    rc, rows=run([])
    assert rc==2 and not rows
print('PASS CI APT: official signed HTTPS sources, architecture selection, cleanup, bounded waits and failure propagation')
