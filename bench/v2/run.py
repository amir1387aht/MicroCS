#!/usr/bin/env python3
"""Build/run checked host benchmarks; retain raw samples and build provenance."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys
import time

HOME = Path(__file__).resolve().parent
PHASES = ('vm_init', 'compile_opt', 'compile_noopt', 'source_run', 'image_opt',
          'image_noopt', 'xip_opt', 'xip_noopt', 'repl_cold', 'repl_warm')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(args, cwd=None):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def quantile(values, fraction):
    values = sorted(values)
    rank = (len(values) - 1) * fraction
    lo = int(rank)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (rank - lo)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=HOME.parents[1])
    parser.add_argument('--out', required=True, type=Path, help='new directory; never overwrite evidence')
    parser.add_argument('--repetitions', type=int, default=25)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--cflags', default='-O2 -std=gnu99 -Wall -Wextra -Werror')
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 10000:
        parser.error('repetitions must be 1..10000')
    repo = args.repo.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    flags = shlex.split(args.cflags)
    sources = sorted(repo.glob('src/*.c')) + sorted(repo.glob('modules/*/*.c'))
    workloads = sorted(HOME.glob('workloads/*.cs'))
    if not sources or not workloads:
        parser.error('source tree/workloads missing')
    build = [args.cc, *flags, '-I' + str(repo / 'include'), *map(str, sources),
             str(HOME / 'measure.c'), '-lm', '-Wl,-Map,' + str(out / 'host.map'), '-o', str(out / 'measure')]
    source_files = sources + sorted((repo / 'include').rglob('*.h')) + sorted((repo / 'include').rglob('*.def'))
    manifest = {str(p.relative_to(repo)): sha(p) for p in source_files}
    cpu = next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                if line.startswith('model name')), platform.processor()) if Path('/proc/cpuinfo').exists() else platform.processor()
    metadata = {
        'schema_version': 1, 'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'source_commit': command(['git', 'rev-parse', 'HEAD'], repo),
        'source_tree': command(['git', 'rev-parse', 'HEAD^{tree}'], repo),
        'working_tree_status': command(['git', 'status', '--porcelain'], repo),
        'harness_commit': command(['git', 'rev-parse', 'HEAD'], HOME),
        'harness_sha256': sha(HOME / 'measure.c'), 'runner_sha256': sha(__file__),
        'compiler_version': command([args.cc, '--version']).splitlines()[0],
        'python_version': platform.python_version(), 'os': platform.platform(), 'cpu': cpu,
        'pointer_bits': 64 if sys.maxsize > 2**32 else 32,
        'build_argv': build, 'repetitions': args.repetitions,
        'source_sha256': manifest,
        'workload_sha256': {p.name: sha(p) for w in workloads for p in (w, w.with_suffix('.out'))},
        'timing': 'CLOCK_MONOTONIC wall microseconds; fresh VM per sample except persistent repl_warm; no startup subtraction',
        'memory': 'requested bytes via custom realloc, including VM; excludes libc overhead, source/expected buffers and external image copies; not RSS, pool high-water or MCU RAM',
        'cache': 'vm-cold (fresh VM) does not mean cold CPU/OS caches; phases run in fixed order; no warm-up samples discarded',
        'security': 'unchanged current default; no authenticated shell or signed image module added',
        'optional_modules': 'default disabled; existing optional LVGL/TFT integrations are not enabled by these benchmark flags',
        'validation': 'every sample checks result/output; all VMs must release every tracked byte; .out expectations are mathematical fixtures, not .NET comparison',
    }
    meta_path = out / 'metadata.json'
    meta_path.write_text(json.dumps(metadata, indent=2) + '\n')
    try:
        with (out / 'build.log').open('w') as log:
            subprocess.run(build, cwd=repo, stdout=log, stderr=subprocess.STDOUT, check=True)
        (out / 'size.txt').write_text(command(['size', str(out / 'measure')]) + '\n')
        metadata['binary_file_bytes'] = (out / 'measure').stat().st_size
        metadata['size_note'] = 'host ELF sections and link map; not MCU flash size'
        raw_path = out / 'samples.jsonl'
        with raw_path.open('w') as raw:
            for workload in workloads:
                result = subprocess.run([str(out / 'measure'), str(workload), str(workload.with_suffix('.out')),
                                         str(args.repetitions)], text=True, capture_output=True, timeout=600)
                (out / (workload.stem + '.stderr.log')).write_text(result.stderr)
                if result.returncode:
                    raise RuntimeError(f'{workload.name}: benchmark failed ({result.returncode}); see stderr log')
                samples = [json.loads(line) for line in result.stdout.splitlines()]
                if len(samples) != len(PHASES) * args.repetitions:
                    raise RuntimeError('sample count mismatch')
                for phase in PHASES:
                    group = [s for s in samples if s['phase'] == phase]
                    if [s['iteration'] for s in group] != list(range(args.repetitions)):
                        raise RuntimeError('missing/duplicate iterations')
                    if any(not s['verified'] or s['elapsed_us'] < 0 for s in group):
                        raise RuntimeError('unverified/invalid sample')
                for sample in samples:
                    sample['workload'] = workload.stem
                    raw.write(json.dumps(sample, separators=(',', ':')) + '\n')
        # Derive the summary from the persisted raw file, not a separate timing pass.
        rows = [json.loads(line) for line in raw_path.read_text().splitlines()]
        summaries = []
        for workload in (p.stem for p in workloads):
            for phase in PHASES:
                group = [r for r in rows if r['workload'] == workload and r['phase'] == phase]
                values = [r['elapsed_us'] for r in group]
                median = statistics.median(values)
                # Independent headline check (rank interpolation versus statistics).
                if median != quantile(values, .5):
                    raise RuntimeError('median cross-check failed')
                summaries.append(dict(workload=workload, phase=phase, samples=len(values),
                                      median_us=median, p10_us=quantile(values, .1), p90_us=quantile(values, .9),
                                      min_us=min(values), max_us=max(values),
                                      max_requested_peak_bytes=max(r['requested_peak_bytes'] for r in group),
                                      max_requested_retained_bytes=max(r['requested_retained_bytes'] for r in group),
                                      image_bytes=group[0]['image_bytes']))
        with (out / 'summary.csv').open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(summaries[0]))
            writer.writeheader(); writer.writerows(summaries)
        metadata.update(completed_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                        samples_sha256=sha(raw_path), binary_sha256=sha(out / 'measure'),
                        sample_count=len(rows), status='passed')
        print(f'PASS {len(rows)} checked samples; raw/provenance/summary: {out}')
    except Exception as exc:
        metadata.update(status='failed', failure=str(exc))
        raise
    finally:
        meta_path.write_text(json.dumps(metadata, indent=2) + '\n')


if __name__ == '__main__':
    main()
