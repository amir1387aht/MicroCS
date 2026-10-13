#!/usr/bin/env python3
"""The benchmark must reject invalid runs rather than publish good-looking timings."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
workload = Path(__file__).resolve().parents[1] / 'bench/v2/workloads/calls.cs'
expected = workload.with_suffix('.out')

def run(source, output, repetitions):
    return subprocess.run([binary, str(source), str(output), str(repetitions)], capture_output=True, text=True, timeout=30)

for value in ('0', '-1', 'abc', '1junk', '10001', '999999999999999999999999'):
    result = run(workload, expected, value)
    assert result.returncode != 0 and not result.stdout, f'invalid repetitions accepted: {value}'
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    wrong = folder / 'wrong.out'; wrong.write_text('0\n')
    assert run(workload, wrong, 1).returncode != 0, 'wrong output accepted'
    invalid = folder / 'invalid.cs'; invalid.write_text('int x = ;')
    assert run(invalid, expected, 1).returncode != 0, 'invalid source accepted'
    invalid.write_bytes(b'Console.WriteLine(2584);\0hostile')
    assert run(invalid, expected, 1).returncode != 0, 'embedded NUL accepted'
    assert run(folder / 'missing.cs', expected, 1).returncode != 0, 'missing file accepted'
result = run(workload, expected, 2)
assert result.returncode == 0, result.stderr
samples = [json.loads(line) for line in result.stdout.splitlines()]
assert len(samples) == 20 and all(row['verified'] for row in samples)
assert {row['phase'] for row in samples} == {'vm_init', 'compile_opt', 'compile_noopt', 'source_run',
                                             'image_opt', 'image_noopt', 'xip_opt', 'xip_noopt', 'repl_cold', 'repl_warm'}
assert all(row['elapsed_us'] >= 0 for row in samples)
print('PASS benchmark: valid samples, output/status checks, invalid count/source/NUL/file rejection, tracked teardown')
