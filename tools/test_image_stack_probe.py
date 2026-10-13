#!/usr/bin/env python3
"""Reproduce host-only stack-flow RESEARCH, not production image verification.
Compiles repo scripts/templates in optimized/unoptimized form, then LOADS only.
The probe explicitly accepts a known type gap and has no lifetime/MCU budget proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--mcs', type=Path, default=ROOT / 'mcs')
p.add_argument('--probe', type=Path, required=True)
p.add_argument('--out', type=Path, default=ROOT / 'build/stack-flow-research')
p.add_argument('--trusted-only', action='store_true',
               help='Compile/load only the trusted corpus; explicitly skip handcrafted/file-error fixtures')
a = p.parse_args()
a.mcs = a.mcs.resolve()
a.probe = a.probe.resolve()
a.out.mkdir(parents=True, exist_ok=True)
sources = sorted(ROOT.glob('tests/t*.cs')) + sorted(ROOT.glob('tools/studio/templates/**/*.cs'))
records = []
images = []
for mode in ('opt', 'noopt'):
    for i, source in enumerate(sources):
        image = (a.out / f'{mode}-{i:03d}.mcsb').resolve()
        cmd = [str(a.mcs)] + (['-O0'] if mode == 'noopt' else [])
        cmd += ['-c', str(source), '-o', str(image)]
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        record = {'source': str(source.relative_to(ROOT)), 'mode': mode,
                  'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                  'compile_exit': result.returncode}
        if result.returncode:
            record['diagnostic'] = result.stdout + result.stderr
        else:
            record['image_sha256'] = hashlib.sha256(image.read_bytes()).hexdigest()
            images.append(image)
        records.append(record)
(a.out / 'compiled-corpus.json').write_text(json.dumps(records, indent=2) + '\n')
if len(images) != len(records):
    raise SystemExit(f'FAIL compilation: {len(images)}/{len(records)}; see compiled-corpus.json')
for name, cmd in [('self-test', [str(a.probe), '--self-test']),
                  ('corpus', [str(a.probe)] + [str(x) for x in images])]:
    if a.trusted_only and name == 'self-test':
        continue
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    (a.out / f'{name}.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(f'FAIL research {name}, exit {result.returncode}; see {name}.log')
    print(result.stdout.strip(), flush=True)
if a.trusted_only:
    print(f'PASS trusted research corpus: {len(sources)} sources x2 modes, LOAD ONLY; '
          'handcrafted/file-error fixtures SKIPPED; NO production/type/lifetime/security claim', flush=True)
    raise SystemExit(0)
# Bounded malformed-file/error paths, still loader-only. Never call execution.
errors = []
for name, payload in [('empty', b''), ('bad-magic', b'not-an-image'),
                      ('truncated', images[0].read_bytes()[:-1])]:
    path = a.out / f'error-{name}.mcsb'
    path.write_bytes(payload)
    result = subprocess.run([str(a.probe), str(path.resolve())], cwd=ROOT,
                            capture_output=True, text=True)
    errors.append({'case': name, 'exit': result.returncode,
                   'output': result.stdout + result.stderr})
    if result.returncode != 2 or any(x in result.stderr for x in
            ('AddressSanitizer', 'LeakSanitizer', 'runtime error:')):
        (a.out / 'file-errors.json').write_text(json.dumps(errors, indent=2) + '\n')
        raise SystemExit(f'FAIL research file error case: {name}')
(a.out / 'file-errors.json').write_text(json.dumps(errors, indent=2) + '\n')
print('PASS 3 bounded malformed-file/error paths, LOAD ONLY', flush=True)
print(f'PASS research compatibility: {len(sources)} sources x2 modes, LOAD ONLY; '
      'NO production/type/lifetime/embedded-security claim', flush=True)
