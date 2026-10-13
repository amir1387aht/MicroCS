#!/usr/bin/env python3
"""Post-fix trusted-source stack resource tests, never a crash reproducer or verifier."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    'default': [], 'gc-stress': ['-DMCS_GC_STRESS=1'],
    'compiler-off': ['-DMCS_ENABLE_COMPILER=0'], 'xip-off': ['-DMCS_ENABLE_XIP=0'],
    'superops-off': ['-DMCS_ENABLE_SUPEROPS=0'],
    'source-opt': ['-DMCS_OPTIMIZE_SOURCE=1'],
    'switch': ['-DMCS_COMPUTED_GOTO=0', '-DMCS_FIELD_CACHE=0'],
    'compact': ['-DMCS_COMPACT_VALUES=1'], 'int64': ['-DMCS_INT64=1'],
    'float32': ['-DMCS_FLOAT_DOUBLE=0'], 'no-float': ['-DMCS_ENABLE_FLOAT=0'],
    'min': ['-DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN'],
    'lowram': ['-DMCS_DEFAULT_PROFILE=MCS_PROFILE_LOWRAM'],
    'mcu': ['-DMCS_DEFAULT_PROFILE=MCS_PROFILE_MCU'],
}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--build-dir', type=Path, default=ROOT / 'build/stack-capacity')
    p.add_argument('--variant', action='append', choices=VARIANTS)
    a = p.parse_args()
    a.build_dir = a.build_dir.resolve()
    a.build_dir.mkdir(parents=True, exist_ok=True)
    fixtures = a.build_dir / 'fixtures'
    fixtures.mkdir(exist_ok=True)
    sources = sorted(ROOT.glob('src/*.c')) + sorted(ROOT.glob('modules/*/*.c'))
    base = shlex.split(a.cc) + ['-std=gnu99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                              '-I' + str(ROOT / 'include'), '-DMCS_USER_CONFIG=0']
    if a.sanitize:
        base += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    provenance = {
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'working_tree': subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True),
        'compiler': subprocess.check_output(shlex.split(a.cc) + ['--version'], text=True).splitlines()[0],
        'source_sha256': {str(x.relative_to(ROOT)): hashlib.sha256(x.read_bytes()).hexdigest()
                          for x in sources + sorted((ROOT / 'include').rglob('*.h')) +
                          [ROOT / 'tests/c/test_stack_capacity.c', Path(__file__)]},
        'commands': [], 'completed': [],
        'scope': 'patched runtime, trusted compiler output only; no pre-fix crash or hostile-image execution',
    }
    def record():
        (a.build_dir / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    def build(name, defines):
        binary = a.build_dir / name
        cmd = base + defines + list(map(str, sources)) + [str(ROOT / 'tests/c/test_stack_capacity.c'), '-lm', '-o', str(binary)]
        provenance['commands'].append(cmd)
        record()
        print('BUILD', name, flush=True)
        with (a.build_dir / (name + '-build.log')).open('w') as log:
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        return binary
    default = build('default', [])
    subprocess.run([str(default), '--emit', str(fixtures)], cwd=ROOT, check=True)
    provenance['fixture_sha256'] = {x.name: hashlib.sha256(x.read_bytes()).hexdigest() for x in sorted(fixtures.glob('*.mcsb'))}
    selected = a.variant or list(VARIANTS)
    for name in selected:
        binary = default if name == 'default' else build(name, VARIANTS[name])
        with (a.build_dir / (name + '.log')).open('w') as log:
            subprocess.run([str(binary), str(fixtures)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        print(name, (a.build_dir / (name + '.log')).read_text().strip(), flush=True)
        provenance['completed'].append(name)
        record()
    print('PASS:', len(selected), 'functional stack configurations; full v2 gates remain separate', flush=True)

if __name__ == '__main__':
    main()
