#!/usr/bin/env python3
"""Linux-only full-app profiler runner; instrumentation never enters firmware.
Build tests/c/test_oled_settings_memory.c with allocator/compiler --wrap flags.
32-bit, 8-byte numeric alignment is required for the MCU-sized budget cases.
Results are host measurements, not an ESP-IDF BLE/native heap measurement.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def semantic_map(path):
    result = {}
    def put(pointer, category):
        if pointer != '(nil)':
            result[pointer] = category
    for line in path.read_text().splitlines():
        if line.startswith('PTR '):
            for key, pointer in re.findall(r'(\w+)=(0x[\da-f]+|\(nil\))', line):
                put(pointer, {'fn': 'Functions and closures', 'code': 'Bytecode instructions',
                              'const': 'Function constants', 'lines': 'Debug line tables',
                              'cache': 'Field/method caches'}[key])
        elif line.startswith('VM '):
            for key, pointer in re.findall(r'(\w+)=(0x[\da-f]+|\(nil\))', line):
                put(pointer, 'VM core, stack and symbol indices')
        elif line.startswith('OBJ '):
            fields = dict(x.split('=', 1) for x in line.split()[1:])
            kind, pointer = int(fields['kind']), fields['p']
            category = {0: 'Names and literal strings', 1: 'Functions and closures',
                        2: 'Functions and closures', 3: 'Functions and closures',
                        4: 'Native binding objects', 5: 'Classes and member tables',
                        6: 'App instances', 7: 'Functions and closures',
                        8: 'Functions and closures', 9: 'Collections', 10: 'Collections',
                        11: 'Collections', 12: 'Native userdata'}.get(kind, 'Other')
            if kind == 12:
                category += ': ' + fields['name']
            result.setdefault(pointer, category)
            for key in ['methods', 'getters', 'setters', 'statics', 'fields', 'defaults']:
                if key in fields:
                    put(fields[key], 'Classes and member tables')
            for key in ['items', 'keys', 'idx']:
                if key in fields:
                    put(fields[key], 'Collections')
    return result


def analyze(binary, prefix, log):
    allocations = list(prefix.parent.glob(prefix.name + '-*.alloc'))
    addresses = sorted({x for p in allocations for line in p.read_text().splitlines()
                        for x in line.split()[4:]})
    symbols = subprocess.check_output(['addr2line', '-f', '-e', str(binary), *addresses], text=True).splitlines()
    resolved = {a: (symbols[i*2], symbols[i*2+1]) for i, a in enumerate(addresses)}
    stages = {}
    for line in log.splitlines():
        if line.startswith('SNAP '):
            fields = line.split()
            stages[fields[1]] = {key: int(value) for key, value in (x.split('=') for x in fields[2:])}
    for path in allocations:
        tag = path.stem[len(prefix.name)+1:]
        identities = prefix.parent / (prefix.name + '-' + tag + '.functions')
        semantic = semantic_map(identities) if identities.exists() else {}
        groups, sites = collections.Counter(), collections.Counter()
        for line in path.read_text().splitlines():
            pointer, requested, owned, temporary, *frames = line.split()
            names = [resolved[x][0] for x in frames]
            site = ' > '.join(names[3:13])
            sites[site] += int(owned)
            category = semantic.get(pointer)
            if not category:
                if int(temporary):
                    category = 'Source/file buffers' if 'mcs_vfs_read_file' in names else 'Compiler temporary workspace'
                elif 'arena_alloc' in names:
                    category = 'Compiler temporary workspace'
                elif 'mcs_fn_emit' in names:
                    category = 'Emitted function buffers'
                elif 'alloc_string' in names or 'mcs_intern' in names:
                    category = 'Names and literal strings'
                elif 'mcs_alloc_obj' in names:
                    category = 'GC object structures'
                else:
                    category = 'Other VM/binding allocations'
            groups[category] += int(owned)
        physical = sum(groups.values())
        if tag in stages:
            assert physical == stages[tag]['used'], (tag, physical, stages[tag])
        stages.setdefault(tag, {})['allocation_bytes'] = physical
        stages[tag]['categories'] = dict(groups.most_common())
        stages[tag]['top_allocation_stacks'] = dict(sites.most_common(20))
    match = re.search(r'RESULT ok=(\d+) failed_requests=(\d+) peak=(\d+)', log)
    result = dict(zip(['ok', 'failed_requests', 'peak'], map(int, match.groups()))) if match else {}
    if result:
        # The pool may briefly hold old + replacement blocks inside realloc,
        # before the external wrapper regains control. Never relabel the lower
        # observed stack sample as the allocator's authoritative high-water mark.
        result['unobserved_realloc_overlap'] = result['peak'] - stages['peak']['allocation_bytes']
        assert result['unobserved_realloc_overlap'] >= 0
    return {'stages': stages, 'result': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--out', type=Path, default=Path('build/oled-memory-profile'))
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    out = args.out.resolve()
    source = Path('tests/projects/oled_settings_memory/app.cs').read_text()
    evidence = {'scope': 'Supplied executable app logic, normalized whitespace; commented BLE startup omitted. '
                         '32-bit Linux, 8-byte numeric alignment, embedded profile, real U8g2 2.37.1, '
                         'simulated HAL/BLE backend, BLE compiled but not started, one sample text file. '
                         'Host POSIX threads substitute for FreeRTOS; no ESP-IDF radio RAM measured.',
                'source_bytes': len(source.encode()), 'runs': {}}
    def run(name, file, contents, setting='valid', backing=196608, ceiling=None, extra=(), no_workload=False, expected=0):
        with tempfile.TemporaryDirectory(prefix='microcs-app-profile-') as folder:
            root = Path(folder)
            (root/'files/text').mkdir(parents=True)
            (root/'files/text/sample.txt').write_text('hello')
            (root/file).write_bytes(contents)
            if setting == 'valid':
                (root/'setting.json').write_text('[{"name":"Contrast","m_value":3}]')
            elif setting == 'invalid':
                (root/'setting.json').write_text('{invalid]')
            env = os.environ.copy()
            if ceiling:
                env['PROFILE_CEILING'] = str(ceiling)
            if no_workload:
                env['PROFILE_NO_WORKLOAD'] = '1'
            prefix = out/name
            process = subprocess.run([str(binary), folder, '/'+file, str(backing), str(prefix), *extra],
                                     text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            (out/(name+'.log')).write_text(process.stdout)
            assert process.returncode == expected, (name, process.returncode, process.stdout)
            if expected:
                assert 'out of memory' in process.stdout, (name, process.stdout)
            if not extra:
                evidence['runs'][name] = analyze(binary, prefix, process.stdout)
            print(name + ': ' + next((x for x in process.stdout.splitlines() if x.startswith('RESULT ')),
                                     next((x for x in process.stdout.splitlines() if x.startswith('IMAGE ')), 'no result')))
    run('compile-debug','app.cs',source.encode(),backing=262144,extra=('debug',),no_workload=True)
    run('compile-strip','app.cs',source.encode(),backing=262144,extra=('strip',),no_workload=True)
    debug = (out/'compile-debug.mcsb').read_bytes()
    strip = (out/'compile-strip.mcsb').read_bytes()
    evidence['image_bytes'] = {'debug': len(debug), 'stripped': len(strip)}
    for state in ['missing', 'valid', 'invalid']:
        run('source-'+state,'app.cs',source.encode(),setting=state)
    run('source-padding','app.cs',(source+'\n/*'+' '*8192+'*/\n').encode())
    run('source-owned57','app.cs',source.encode(),backing=57000,ceiling=158720,expected=1)
    run('debug-owned57','app.mcsb',debug,backing=57000,ceiling=158720)
    run('strip-owned57','app.mcsb',strip,backing=57000,ceiling=158720)
    declarations = source[:source.rindex('var app = new MainApp();')]
    run('declarations','app.cs',declarations.encode(),no_workload=True)
    (out/'evidence.json').write_text(json.dumps(evidence, indent=2))
    print('Evidence: ' + str(out/'evidence.json'))


if __name__ == '__main__':
    main()
