#!/usr/bin/env python3
"""Record actual opt-in TFT cross-build artifacts/pins; never hardware evidence.
Run only after a successful complete five-environment tests/tft matrix.
No network, no synthesized build results, no sources or dependencies modified.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINS = {
    'TFT_eSPI': '16e37595040eac69cd628e4bffb56fc30cad6299',
    'LVGL8': '4495f428630cc1741bd8bfd977f080e8460e8e8d',
    'LVGL9': 'c016f72d4c125098287be5e83c0f1abed4706ee5',
}
ENVS = ['tft-standalone', 'tft-lvgl8', 'tft-lvgl9', 'tft-touch-lvgl9', 'graphics-off']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--log', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--tft-root', type=Path, required=True)
    ap.add_argument('--lvgl8-root', type=Path, required=True)
    ap.add_argument('--lvgl9-root', type=Path, required=True)
    a = ap.parse_args()
    log = a.log.read_text()
    report = {
        'validation': 'cross-compile-and-link ONLY; NOT hardware execution',
        'platform': 'espressif32@6.10.0', 'board': 'esp32dev',
        'ram_note': 'PIO static section usage, excludes runtime VM/upstream heap/stack',
        'log_sha256': sha(a.log), 'dependencies': {}, 'environments': {},
    }
    for name, path in zip(PINS, (a.tft_root, a.lvgl8_root, a.lvgl9_root)):
        commit = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
        if commit != PINS[name]:
            raise SystemExit(f'{name}: unexpected dependency commit {commit}')
        report['dependencies'][name] = commit
    for name in ENVS:
        match = re.search(rf'^Processing {re.escape(name)} \(.*?(?=^Processing |\Z)', log, re.M | re.S)
        if not match or '[SUCCESS]' not in match.group() or '[FAILED]' in match.group():
            raise SystemExit(f'{name}: no successful build section in log')
        sizes = {}
        for label, key in [('RAM', 'static_ram_bytes'), ('Flash', 'flash_usage_bytes')]:
            m = re.search(rf'{label}:.*?used (\d+) bytes from (\d+) bytes', match.group())
            if not m:
                raise SystemExit(f'{name}: missing {label} section size')
            sizes[key] = int(m[1])
            sizes[key.replace('_bytes', '_limit_bytes')] = int(m[2])
        for suffix in ('elf', 'bin'):
            file = ROOT / 'tests/tft/.pio/build' / name / f'firmware.{suffix}'
            if not file.is_file():
                raise SystemExit(f'{name}: missing actual {suffix} artifact')
            sizes[suffix + '_sha256'] = sha(file)
            sizes[suffix + '_file_bytes'] = file.stat().st_size
        report['environments'][name] = sizes
    files = ['include/mcs_tft.h', 'modules/shell/mcs_shell.c', 'include/mcs.h', 'include/mcs_config.h', 'include/mcs_capabilities.def', 'src/mcs_vm.c',
             'ports/arduino/mcs_arduino_tft.cpp', 'tools/pio_build.py', 'library.json',
             'tests/tft/platformio.ini', 'tests/tft/src/main.cpp',
             'tests/tft/src/sprite_contract.h', 'tests/tft/sprite_contract.cs',
             'include/mcs_tft_sprite_budget.h', 'ports/arduino/mcs_tft_sprite_native.h',
             'tests/tft/configs/lvgl8/lv_conf.h', 'tests/tft/configs/lvgl9/lv_conf.h',
             'modules/tft/mcs_display_rgb565.c', 'include/mcs_display_rgb565.h',
             'modules/lvgl/mcs_lvgl.c', 'modules/lvgl/mcs_lvgl_calls.inc',
             'modules/lvgl/mcs_lvgl_styles.inc', 'modules/lvgl/mcs_lvgl_widgets.inc',
             'modules/lvgl/mcs_lvgl_fonts.inc']
    report['integration_source_sha256'] = {f: sha(ROOT / f) for f in files}
    a.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Recorded five successful actual cross-build artifacts; no hardware claim')


if __name__ == '__main__':
    main()
