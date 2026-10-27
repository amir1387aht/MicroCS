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
if name=='timeout':
    assert a[:1]==['--kill-after=15s'] and a[1] in ('5m','10m')
    os.execvp(a[2],a[2:])
if name=='sudo':
    assert a[:3]==['-n','env','DEBIAN_FRONTEND=noninteractive']
    os.environ['DEBIAN_FRONTEND']='noninteractive'
    os.execvp(a[3],a[3:])
assert os.environ['DEBIAN_FRONTEND']=='noninteractive'
assert all(x in a for x in ['Acquire::http::Timeout=30','Acquire::https::Timeout=30','Acquire::Retries=2','DPkg::Lock::Timeout=60'])
if 'update' in a:
    assert 'APT::Update::Error-Mode=any' in a
    sys.exit(int(os.environ.get('UPDATE_RC','0')))
assert 'install' in a and '--no-install-recommends' in a
assert a[-2:]==['gcc-arm-none-eabi','libnewlib-arm-none-eabi']
sys.exit(int(os.environ.get('INSTALL_RC','0')))
'''
    for name in ('sudo','timeout','apt-get'):
        p = d/name
        p.write_text(mock)
        p.chmod(0o755)
    env = dict(os.environ, PATH=str(d)+os.pathsep+os.environ['PATH'], APT_TEST_LOG=str(d/'log'))
    def run(args, update=0, install=0):
        (d/'log').write_text('')
        r=subprocess.run(['sh',str(ROOT/'tools/ci_apt.sh')]+args,
                         env=dict(env,UPDATE_RC=str(update),INSTALL_RC=str(install)),capture_output=True,text=True)
        return r.returncode,[json.loads(x) for x in (d/'log').read_text().splitlines()]
    args=['gcc-arm-none-eabi','libnewlib-arm-none-eabi']
    rc, rows=run(args)
    assert rc==0 and len(rows)==6
    assert [row[2] for row in rows if row[0]=='timeout']==['5m','10m']
    rc, rows=run(args,update=41)
    assert rc==41 and len(rows)==3 and not any('install' in row for row in rows)
    rc, rows=run(args,install=42)
    assert rc==42 and len(rows)==6
    rc, rows=run([])
    assert rc==2 and not rows
print('PASS CI APT: bounded waits, retries, strict indexes, noninteractive install and failure propagation')
