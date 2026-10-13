#!/usr/bin/env python3
"""Trusted-compiler arithmetic compatibility for the isolated LOAD-ONLY probe."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mcs', type=Path, default=ROOT / 'mcs')
    p.add_argument('--probe', type=Path, required=True)
    p.add_argument('--out', type=Path, default=ROOT / 'build/stack-arithmetic-research')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    a.mcs, a.probe, a.out = a.mcs.resolve(), a.probe.resolve(), a.out.resolve()
    sources = {
        'signed-immediates': 'int Calc() { int x = 1; x = x + 100; x = x - 120; return x; } Console.WriteLine(Calc());\n',
        'large-constant-pool': 'int Calc() { int x = 1;\n' +
            ''.join(f'x = x + {10000 + i};\n' for i in range(1024)) +
            'return x; } Console.WriteLine(Calc());\n',
    }
    records = []
    for name, text in sources.items():
        source = a.out / (name + '.cs')
        source.write_text(text)
        for noopt in (False, True):
            image = a.out / (name + ('-noopt' if noopt else '-opt') + '.mcsb')
            cmd = [str(a.mcs)] + (['-O0'] if noopt else []) + ['-c', str(source), '-o', str(image)]
            subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True, text=True)
            disasm = subprocess.check_output([str(a.mcs), '-d', str(image)], cwd=ROOT, text=True)
            (image.with_suffix('.disasm')).write_text(disasm)
            expected = 'BIN_LIS' if name == 'signed-immediates' else 'BIN_LKS'
            if not noopt and expected not in disasm:
                raise SystemExit('Missing expected fused opcode: ' + expected)
            result = subprocess.run([str(a.probe), str(image)], cwd=ROOT, capture_output=True, text=True)
            record = {'case': name, 'noopt': noopt, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                      'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(), 'compile_command': cmd,
                      'probe_exit': result.returncode, 'probe_output': result.stdout + result.stderr,
                      'expected_fused_opcode': None if noopt else expected}
            records.append(record)
            (a.out / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
            if result.returncode:
                raise SystemExit('Trusted-image arithmetic probe failed: ' + name)
    print('PASS 4 trusted compiler images, optimized/noopt; signed immediates and 1024 constants; LOAD ONLY')
    print('No image execution, initialization/type/lifetime/security proof or production integration')

if __name__ == '__main__':
    main()
