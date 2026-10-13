#!/usr/bin/env python3
"""Bind actual pinned public static fonts; never fetch or rewrite lv_conf.h.
Dynamic/binary/custom font ownership is NOT implemented by these static bindings.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINS = {8: '4495f428630cc1741bd8bfd977f080e8460e8e8d',
        9: 'c016f72d4c125098287be5e83c0f1abed4706ee5'}


def declarations(root):
    header = root / 'src/font/lv_font.h'
    entries = dict(re.findall(r'#if (LV_FONT_[A-Z0-9_]+)\s*\nLV_FONT_DECLARE\((lv_font_\w+)\)',
                              header.read_text()))
    if len(entries) != 27:
        raise SystemExit(f'Unexpected pinned font declarations in {header}')
    return entries, hashlib.sha256(header.read_bytes()).hexdigest()


def generate(a, b):
    tables = [declarations(a), declarations(b)]
    lookup = ['/* Generated from actual public static font declarations; firmware-owned lifetimes. */']
    tests = ['/* Generated native pointer/height and disabled-font checks for every declared built-in. */',
             'run(vm, "var fontsGui=new LvGui(80,60); var fontsObj=fontsGui.Screen.Create(\\"label\\"); fontsObj.Text=\\"Aa 123\\";");',
             '#if LVGL_VERSION_MAJOR == 9',
             'lv_obj_t *font_obj = lv_obj_get_child(lv_screen_active(), 0);',
             '#else', 'lv_obj_t *font_obj = lv_obj_get_child(lv_scr_act(), 0);', '#endif',
             'CHECK(font_obj != NULL);',
             'run(vm, "fontsObj.SetFont(\\"Default\\"); if(fontsObj.FontHeight<=0) throw new Exception(\\"default height\\");");',
             'CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == LV_FONT_DEFAULT);']
    inventory = {'status': 'static built-in fonts only; dynamic/custom/binary assets remain unsupported',
                 'default': 'Default selects LV_FONT_DEFAULT, which must remain firmware-owned',
                 'versions': {v: {'header_sha256': table[1], 'declarations': table[0]}
                              for v, table in zip(('8.4.0', '9.4.0'), tables)}}
    for macro in sorted(set(tables[0][0]) | set(tables[1][0])):
        present = [i for i in range(2) if macro in tables[i][0]]
        names = [tables[i][0][macro] for i in present]
        if len(set(names)) != 1:
            raise SystemExit(f'Font symbol mismatch: {macro}')
        name = names[0]
        condition = macro if len(present) == 2 else f'LVGL_VERSION_MAJOR == {8 + present[0]} && {macro}'
        lookup += [f'#if {condition}', f'    if (!strcmp(name, "{name}")) return &{name};', '#endif']
        tests += [f'#if {condition}',
                  f'run(vm, "if(!LvGui.HasFont(\\"{name}\\")) throw new Exception(\\"enabled font absent\\"); fontsObj.SetFont(\\"{name}\\"); fontsGui.Pump(40);");',
                  f'CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &{name});',
                  f'{{ char check_height[160]; snprintf(check_height, sizeof check_height, \"if(fontsObj.FontHeight != %d) throw new Exception(\\\"font height\\\");\", (int)lv_font_get_line_height(&{name})); run(vm, check_height); }}',
                  '#else',
                  f'run(vm, "if(LvGui.HasFont(\\"{name}\\")) throw new Exception(\\"disabled font present\\"); try {{ fontsObj.SetFont(\\"{name}\\"); throw new Exception(\\"disabled font accepted\\"); }} catch(NotSupportedException e) {{}}");',
                  '#endif']
    tests += ['run(vm, "fontsObj.SetFont(\\"Default\\"); fontsGui.Dispose();");']
    return ('\n'.join(lookup) + '\n', '\n'.join(tests) + '\n',
            json.dumps(inventory, indent=2, sort_keys=True) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--v8', required=True, type=Path)
    ap.add_argument('--v9', required=True, type=Path)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    for root, major in ((args.v8, 8), (args.v9, 9)):
        revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
        if revision != PINS[major] or subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True).strip():
            ap.error(f'Font generation requires unchanged pinned LVGL{major} checkout')
        header = root / ('lv_version.h' if major == 9 else 'lvgl.h')
        version = tuple(int(re.search(r'#define\s+LVGL_VERSION_' + part + r'\s+(\d+)',
                                     header.read_text())[1]) for part in ('MAJOR', 'MINOR', 'PATCH'))
        if version != (major, 4, 0):
            ap.error(f'Unsupported real headers: {version}')
    paths = (ROOT/'modules/lvgl/mcs_lvgl_fonts.inc', ROOT/'tests/lvgl/font_readback.inc',
             ROOT/'docs/development/lvgl-fonts.json')
    for path, text in zip(paths, generate(args.v8, args.v9)):
        if args.check:
            if not path.exists() or path.read_text() != text:
                raise SystemExit(f'LVGL static font generated output drift: {path}')
        else:
            path.write_text(text)
    print('PASS actual LVGL static font declarations/output parity')


if __name__ == '__main__':
    main()
