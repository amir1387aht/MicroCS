#!/usr/bin/env python3
"""Independent reviewer regression using a genuine, owner-created bundle copy."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import verify_preview as V
SHA = 'f7fd44bc08fb408c8be21e165f5ada36b7b5ca3e'
VERSION = '2.0.0-preview.1'
source = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='review-verifier-') as tmp:
    bundle = Path(tmp) / 'bundle'
    shutil.copytree(source, bundle)
    def snapshot():
        return {p.name: (p.read_bytes(),p.stat().st_mode,p.stat().st_mtime_ns,p.stat().st_ctime_ns) for p in bundle.iterdir()}
    before = snapshot()
    manifest = V.verify_bundle(bundle, SHA, VERSION)
    assert manifest['source_tree'] == '1002dd8af7061f500bb4b1024c6346a9bf1ab56c'
    assert snapshot() == before, 'verifier must not mutate bytes/mode/mtime/ctime'
    print('PASS genuine exact owner package, pinned commit/version/tree and read-only verification')
    def cli(*args):
        return subprocess.run([sys.executable,'-B',str(ROOT/'tools/verify_preview.py'),str(bundle),*args],capture_output=True,text=True,timeout=10)
    def rejected(label,*args):
        result=cli(*args)
        assert result.returncode == 1 and result.stdout == '' and 'ERROR:' in result.stderr and 'Traceback' not in result.stderr, (label,result)
        print('PASS rejection:',label)
    rejected('independent commit mismatch','--expected-commit','0'*40)
    archive = bundle / next(iter(manifest['artifacts']))
    original = archive.read_bytes()
    archive.write_bytes(bytes([original[0]^1])+original[1:])
    rejected('same-size changed archive digest')
    archive.write_bytes(original)
    sums=bundle/'SHA256SUMS'; sums_original=sums.read_bytes()
    sums.write_bytes(sums_original+sums_original.splitlines(keepends=True)[0])
    rejected('duplicate checksum record')
    sums.write_bytes(sums_original)
    extra=bundle/'review-extra.txt';extra.write_text('ordinary reviewer fixture')
    rejected('extra directory entry');extra.unlink()
    archive.unlink();archive.symlink_to(source/archive.name)
    rejected('archive symlink');archive.unlink();archive.write_bytes(original)
    # A benign archive-byte replacement with both metadata records updated passes:
    # direct demonstration that this verifier is consistency, NOT provenance/content.
    archive.write_bytes(b'ordinary reviewer opaque bytes, no archive contents')
    data=archive.read_bytes();manifest['artifacts'][archive.name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (bundle/'manifest.json').write_text(json.dumps(manifest)+'\n')
    sums.write_text(''.join(hashlib.sha256((bundle/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in sorted(set(manifest['artifacts'])|{'manifest.json'})))
    assert V.verify_bundle(bundle,SHA,VERSION)==manifest
    print('PASS documented limitation independently confirmed: coherent opaque replacement still verifies')
print('PASS independent verifier regression; peer bundle untouched')
