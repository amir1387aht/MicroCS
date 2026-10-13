#!/usr/bin/env python3
"""Explicit real TFT_eSPI generic RAM tests, not hardware or a production host port.
No dependency download, replacement TFT header/source or upstream modifications.
Only the test Arduino boundary is supplied; every physical IO attempt aborts.
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import resource
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PIN = '16e37595040eac69cd628e4bffb56fc30cad6299'

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tft-root', type=Path, required=True)
    ap.add_argument('--build-dir', type=Path, default=ROOT/'build/tft-headless')
    ap.add_argument('--sanitize', action='store_true')
    ap.add_argument('--gc-stress', action='store_true')
    a = ap.parse_args()
    dep = a.tft_root.resolve()
    revision = subprocess.check_output(['git', '-C', str(dep), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != PIN:
        raise SystemExit(f'Headless test requires inspected upstream pin {PIN}; got {revision}')
    if subprocess.check_output(['git', '-C', str(dep), 'status', '--porcelain'], text=True).strip():
        raise SystemExit('Headless test requires an unchanged upstream checkout')
    subprocess.run(['python3', str(ROOT/'tools/gen_tft_fixture.py'), '--check'], check=True)
    out = a.build_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    boundary = ROOT/'tests/tft/headless'
    flags = ['-O1', '-g', '-ffunction-sections', '-fdata-sections', '-I'+str(ROOT/'include'),
             '-DMCS_USER_CONFIG=0', '-DARDUINO=1', '-DMCS_ENABLE_TFT_ESPI=1',
             '-DMCS_ENABLE_THREADS=0', '-DMCS_ENABLE_LVGL=0']
    if a.gc_stress:
        flags += ['-DMCS_GC_STRESS=1']
    if a.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    cc = shlex.split(os.environ.get('CC', 'cc')); cxx = shlex.split(os.environ.get('CXX', 'g++'))
    def build_c(path):
        obj = out/(str(path.relative_to(ROOT)).replace('/', '_')+'.o')
        subprocess.run(cc+['-std=gnu99', '-Wall', '-Wextra', '-Werror']+flags+
                       ['-c', str(path), '-o', str(obj)], check=True)
        return str(obj)
    sources = sorted(ROOT.glob('src/*.c'))+sorted(ROOT.glob('modules/*/*.c'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        objects = list(pool.map(build_c, sources))
    cppflags = flags+['-std=gnu++17', '-I'+str(boundary), '-I'+str(dep),
                      '-DUSER_SETUP_LOADED=1', '-DILI9341_DRIVER=1', '-DTFT_WIDTH=240',
                      '-DTFT_HEIGHT=320', '-DTFT_CS=5', '-DTFT_DC=2', '-DLOAD_GLCD=1',
                      '-DDISABLE_ALL_LIBRARY_WARNINGS=1']
    for path in [dep/'TFT_eSPI.cpp', ROOT/'ports/arduino/mcs_arduino_tft.cpp', boundary/'test_sprite.cpp']:
        obj = out/(path.name+'.o')
        # Upstream warnings are not our warning policy; adapter/tests are strict.
        strict = [] if path.parent == dep else ['-Wall', '-Wextra', '-Werror']
        subprocess.run(cxx+cppflags+strict+['-c', str(path), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = out/'test_sprite'
    subprocess.run(cxx+flags+objects+['-Wl,--gc-sections', '-Wl,--wrap=calloc', '-lm', '-o', str(exe)], check=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    subprocess.run([str(exe)], check=True)
    guard = subprocess.run([str(exe), '--attempt-io'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if guard.returncode != -signal.SIGABRT or 'FORBIDDEN physical Arduino IO:' not in guard.stderr:
        raise SystemExit(f'Physical-IO guard failed: status{guard.returncode}, stderr{guard.stderr}')
    print('PASS physical-IO guard self-test aborts actual upstream init; no fake transfer success')
    print(f'PASS pinned real upstream {PIN}; test-only generic Arduino boundary, NOT hardware validation')

if __name__ == '__main__':
    main()
