#!/usr/bin/env python3
"""Offline configure failures, exact version/config and graphics-OFF contract."""
import argparse, subprocess, tempfile
from pathlib import Path
p = argparse.ArgumentParser(); p.add_argument('--cmake', default='cmake'); p.add_argument('--root', required=True); p.add_argument('--major', required=True, choices=('8', '9'))
a = p.parse_args(); root = Path(__file__).resolve().parents[2]
base = [a.cmake, '-S', str(root), '-DMICROCS_CONFIG=NONE']
with tempfile.TemporaryDirectory(prefix='microcs-lvgl-contract-') as td:
    i = 0
    def case(opts, text, success=False):
        global i
        i += 1
        cmd = base + ['-B', str(Path(td)/str(i))] + opts
        r = subprocess.run(cmd, text=True, capture_output=True)
        assert (r.returncode == 0) == success, (cmd, r.stdout[-2000:], r.stderr[-2000:])
        assert text in " ".join((r.stdout + r.stderr).split()), (text, r.stdout, r.stderr)
    conf = str(root/f'tests/lvgl/lv_conf{a.major}.h')
    enabled = ['-DMICROCS_LVGL=ON', '-DMICROCS_LVGL_MAJOR='+a.major]
    case(enabled, 'missing lvgl.h')
    case(enabled+['-DMICROCS_LVGL_ROOT='+a.root], 'MICROCS_LVGL_CONFIG')
    case(['-DMICROCS_LVGL=ON', '-DMICROCS_LVGL_MAJOR='+('9' if a.major=='8' else '8'), '-DMICROCS_LVGL_ROOT='+a.root], 'no automatic version substitution')
    case(enabled+['-DMICROCS_LVGL_ROOT='+a.root, '-DMICROCS_LVGL_CONFIG='+conf, '-DMICROCS_LVGL_TARGET=missing'], 'not an existing CMake target')
    case(enabled+['-DMICROCS_LVGL_INCLUDE_DIR='+a.root, '-DMICROCS_LVGL_CONFIG='+conf], 'headers found but no linked library')
    case(enabled+['-DMICROCS_LVGL_ROOT='+a.root, '-DMICROCS_LVGL_CONFIG='+conf, '-DMICROCS_MODULES=OFF'], 'requires MICROCS_MODULES')
    # Off must configure with invalid/missing dependency inputs: absolutely no lookup.
    case(['-DMICROCS_LVGL=OFF', '-DMICROCS_LVGL_ROOT=/nonexistent', '-DMICROCS_LVGL_MAJOR=7'], 'Configuring done', True)
    print('PASS 7 real-upstream LVGL build-contract cases, including dependency-free OFF')
