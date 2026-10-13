#!/usr/bin/env python3
"""Explicit static-font/decoder contracts, not hardware or mocked runtime proof."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def command(args, *, expected=None):
    result = subprocess.run(args, capture_output=True, text=True)
    if expected:
        if result.returncode == 0 or expected not in result.stdout + result.stderr:
            raise SystemExit(f'Expected failure {expected}: {result.returncode}\n{result.stdout}\n{result.stderr}')
    elif result.returncode:
        raise SystemExit(f'Command failed: {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cmake', default='cmake')
    ap.add_argument('--v8', type=Path, required=True)
    ap.add_argument('--v9', type=Path, required=True)
    ap.add_argument('--fixed-unit', type=Path, required=True)
    ap.add_argument('--unfixed-library', type=Path, required=True)
    a = ap.parse_args()
    original = (a.v8/'src/font/lv_font_fmt_txt.c').read_text()
    expected = original.replace('out[byte_pos] &= ((~bit_mask) << bit_pos);',
                                'out[byte_pos] &= ((uint32_t)(uint8_t)(~bit_mask) << bit_pos);')
    expected += '\n/* Explicit MicroCS build-local unsigned-mask compatibility fix; no ABI change. */\nuint32_t mcs_lvgl8_decoder_compat_version(void) { return UINT32_C(0x08040001); }\n'
    if a.fixed_unit.read_text() != expected:
        raise SystemExit('Build-local decoder contains changes beyond exact unsigned-mask cast/marker')
    for root in (a.v8, a.v9):
        if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True).strip():
            raise SystemExit('Upstream was modified: ' + str(root))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    with tempfile.TemporaryDirectory(prefix='microcs-font-contract-') as directory:
        tmp = Path(directory)
        # No suppression: compressed LVGL8 without linked compatibility proof must fail.
        command(cc + ['-std=gnu99', '-fsyntax-only', '-I'+str(ROOT/'include'), '-I'+str(a.v8),
                      '-DMCS_USER_CONFIG=0', '-DMCS_ENABLE_LVGL=1', '-DMCS_LVGL_MAJOR=8',
                      '-DMCS_ENABLE_THREADS=0', '-DLV_CONF_PATH='+str(ROOT/'tests/lvgl/lv_conf8_fonts.h'),
                      str(ROOT/'modules/lvgl/mcs_lvgl.c')], expected='known negative-shift UB')
        command([a.cmake, '-S', str(ROOT), '-B', str(tmp/'wrong-major'), '-DMICROCS_CONFIG=NONE',
                 '-DMICROCS_LVGL=ON', '-DMICROCS_LVGL_MAJOR=9', '-DMICROCS_LVGL_ROOT='+str(a.v9),
                 '-DMICROCS_LVGL_CONFIG='+str(ROOT/'tests/lvgl/lv_conf9.h'),
                 '-DMICROCS_LVGL8_FONT_DECODER_FIX=ON'], expected='source ROOT build')
        command([a.cmake, '-S', str(ROOT), '-B', str(tmp/'foreign-target'), '-DMICROCS_CONFIG=NONE',
                 '-DMICROCS_LVGL=ON', '-DMICROCS_LVGL_MAJOR=8', '-DMICROCS_LVGL_ROOT='+str(a.v8),
                 '-DMICROCS_LVGL_CONFIG='+str(ROOT/'tests/lvgl/lv_conf8.h'),
                 '-DMICROCS_LVGL_TARGET=foreign', '-DMICROCS_LVGL8_FONT_DECODER_FIX=ON'],
                expected='cannot be silently patched')
        # Configuration-only hash rejection, not a fake runtime/library implementation.
        decoder = tmp/'unknown-source/src/font/lv_font_fmt_txt.c'
        decoder.parent.mkdir(parents=True)
        decoder.write_text('/* intentionally unknown negative-test source */\n')
        script = tmp/'bad-hash.cmake'
        script.write_text(f'include("{ROOT}/cmake/MicroCSLVGL.cmake")\nmicrocs_lvgl8_font_decoder_fix("{tmp}/unknown-source")\n')
        command([a.cmake, '-P', str(script)], expected='reviewed exact8.4.0 source hash')
        probe = tmp/'marker.c'
        probe.write_text('#include <stdint.h>\nextern uint32_t mcs_lvgl8_decoder_compat_version(void);\nint main(void) { return mcs_lvgl8_decoder_compat_version() != UINT32_C(0x08040001); }\n')
        command(cc + [str(probe), str(a.unfixed_library), '-lm', '-o', str(tmp/'missing-marker')],
                expected='mcs_lvgl8_decoder_compat_version')
        mask = tmp/'mask.c'
        mask.write_text('''#include <stdint.h>
#include <stdio.h>
int main(void) {
    unsigned checks = 0;
    for (unsigned len = 1; len <= 8; ++len) {
        uint8_t bits = (uint8_t)((1u << len) - 1u);
        for (unsigned position = 0; position <= 8 - len; ++position)
            for (unsigned out = 0; out < 256; ++out)
                for (unsigned value = 0; value < 256; ++value) {
                    uint8_t fixed = (uint8_t)out;
                    fixed &= ((uint32_t)(uint8_t)(~bits) << position);
                    fixed |= (value << position);
                    unsigned clear = ((255u - bits) * (1u << position)) % 256u;
                    unsigned fill = (value * (1u << position)) % 256u;
                    unsigned expected = (out & clear) | fill;
                    if (fixed != expected) return 1;
                    checks++;
                }
    }
    printf("PASS %u unsigned-mask pixel-byte arithmetic checks\\n", checks);
    return 0;
}
''')
        command(cc + ['-std=c99', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                      '-fno-omit-frame-pointer', str(mask), '-o', str(tmp/'mask')])
        print(command([str(tmp/'mask')]).stdout.strip())
    print('PASS exact build-local fix, unchanged upstream, no-fix/wrong-major/foreign/hash/missing-marker failures')


if __name__ == '__main__':
    main()
