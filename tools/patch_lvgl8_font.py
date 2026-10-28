#!/usr/bin/env python3
"""Explicit opt-in repair of LVGL 8's compressed-font signed-left-shift UB.

Changes host-owned source only with --apply. Review/commit the dependency diff
in firmware; production MicroCS builds never run this tool automatically.
"""
import argparse
import re
from pathlib import Path

OLD = 'out[byte_pos] &= ((~bit_mask) << bit_pos);'
NEW = 'out[byte_pos] &= ((~(uint32_t)bit_mask) << bit_pos);'

def major_version(root):
    root = Path(root)
    match = None
    for relative in ('lvgl.h', 'lv_version.h', 'include/lvgl/lv_version.h'):
        version = root / relative
        if version.is_file():
            match = re.search(r'^#define\s+LVGL_VERSION_MAJOR\s+(\d+)\b', version.read_text(), re.M)
            if match:
                break
    if not match:
        raise ValueError('Cannot verify LVGL major version')
    return int(match[1])

def repair(root, apply=False):
    root = Path(root)
    if major_version(root) != 8:
        return 'not LVGL 8; no source changes'
    path = root / 'src/font/lv_font_fmt_txt.c'
    source = path.read_text()
    if source.count(NEW) == 1 and OLD not in source:
        return 'already repaired'
    if source.count(OLD) != 1 or NEW in source:
        raise ValueError('Unrecognized LVGL 8 font writer; review manually (nothing changed)')
    if not apply:
        raise ValueError('LVGL 8 signed-shift defect found; review and run with --apply')
    path.write_text(source.replace(OLD, NEW))
    return 'repaired unsigned mask shift in ' + str(path)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('lvgl', type=Path)
    p.add_argument('--apply', action='store_true')
    a = p.parse_args()
    try:
        print(repair(a.lvgl, a.apply))
    except (OSError, ValueError) as e:
        p.error(str(e))

if __name__ == '__main__':
    main()
