#!/usr/bin/env python3
"""Compile/run targeted v1-v4 metadata fixtures. Not an untrusted-image verifier.
Malformed fixtures only reach public exec once an independent load rejects them.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default=os.environ.get('CC', 'cc'))
p.add_argument('--sanitize', action='store_true')
p.add_argument('--build-dir', type=Path, default=ROOT / 'build/image-metadata')
p.add_argument('--variant', action='append', help='Run only named variants (repeatable)')
a = p.parse_args()
a.build_dir.mkdir(parents=True, exist_ok=True)
variants = {
    'default': [],
    'gc-stress': ['-DMCS_GC_STRESS=1'],
    'compiler-off': ['-DMCS_ENABLE_COMPILER=0'],
    'xip-off': ['-DMCS_ENABLE_XIP=0'],
    'compact': ['-DMCS_COMPACT_VALUES=1'],
    'int64': ['-DMCS_INT64=1'],
    'float32': ['-DMCS_FLOAT_DOUBLE=0'],
    'no-float': ['-DMCS_ENABLE_FLOAT=0'],
    'min': ['-DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN'],
}
sources = sorted(ROOT.glob('src/*.c')) + sorted(ROOT.glob('modules/*/*.c'))
if a.variant:
    unknown = set(a.variant) - set(variants)
    if unknown:
        p.error('unknown variants: ' + ', '.join(sorted(unknown)))
    variants = {k: v for k, v in variants.items() if k in a.variant}
for name, defines in variants.items():
    binary = (a.build_dir / name).resolve()
    cmd = shlex.split(a.cc) + ['-std=gnu99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
          '-I'+str(ROOT/'include'), '-DMCS_USER_CONFIG=0'] + defines
    if a.sanitize:
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    cmd += [str(x) for x in sources] + [str(ROOT/'tests/c/test_image_metadata.c'), '-lm', '-o', str(binary)]
    print('BUILD', name, shlex.join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(binary)], check=True, cwd=ROOT)
print(f'PASS: {len(variants)} targeted metadata variants; no full stack/type safety claim', flush=True)
