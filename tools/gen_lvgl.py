#!/usr/bin/env python3
"""Generate explicitly limited WIP scalar style bindings from pinned real headers.
No dependency fetch. Unbound pointer/asset/enum APIs remain in coverage inventory.
Run --check against BOTH roots to detect drift; do not generate on an unknown ABI.
"""
import argparse, hashlib, json, re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINS = {'8.4.0': '4495f428630cc1741bd8bfd977f080e8460e8e8d',
        '9.4.0': 'c016f72d4c125098287be5e83c0f1abed4706ee5'}


def styles(root):
    h = (root / 'src/core/lv_obj_style_gen.h').read_text()
    return dict(re.findall(r'void lv_obj_set_style_(\w+)\([^,]+,\s*([^,]+?)\s+value,\s*lv_style_selector_t selector\)', h))


def widget_declarations(root):
    out = {}
    paths = list((root / 'src/widgets').rglob('*.h')) + list((root / 'src/extra/widgets').rglob('*.h'))
    for path in sorted(paths):
        if 'private' in path.name:
            continue
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
        for m in re.finditer(r'(?m)^\s*(?:static\s+inline\s+)?((?:const\s+)?[a-zA-Z_]\w*\s*(?:\*)?)\s*(lv_\w+)\s*\(([^;{}]*)\)\s*(?:;|\{)', text):
            ret, fn, args = m.groups()
            if not re.match(r'\s*(?:const\s+)?lv_obj_t\s*\*\s*\w+\s*(?:,|$)', args):
                continue
            canonical = fn.replace('lv_btn_', 'lv_button_').replace('lv_img_', 'lv_image_')
            params = [re.sub(r'\s+\w+$', '', a.strip()).strip() for a in args.split(',')[1:]]
            out[canonical] = {'fn': fn, 'return': ret.strip(), 'params': params, 'header': str(path.relative_to(root))}
    return out


def widget_calls(a, b):
    tables = [widget_declarations(a), widget_declarations(b)]
    bounds = {'lv_coord_t': (-32768, 32767), 'int8_t': (-128, 127), 'uint8_t': (0, 255),
              'int16_t': (-32768, 32767), 'uint16_t': (0, 65535),
              'int32_t': (-2147483648, 2147483647), 'uint32_t': (0, 4294967295),
              'lv_value_precise_t': (-32768, 32767), 'lv_anim_enable_t': (0, 1),
              'lv_color_t': (0, 0xFFFFFF)}
    scalar = set(bounds) | {'bool'}
    # Verified copying string implementations; never generate retained string,
    # static text, printf/varargs, output buffers or native pointer arguments.
    copied = {'lv_label_set_text', 'lv_label_ins_text', 'lv_checkbox_set_text',
              'lv_textarea_set_text', 'lv_textarea_add_text', 'lv_textarea_set_placeholder_text',
              'lv_dropdown_set_options', 'lv_dropdown_add_option'}
    kinds = {'arc', 'bar', 'slider', 'switch', 'checkbox', 'dropdown', 'roller', 'textarea',
             'table', 'chart', 'canvas', 'line', 'list', 'menu', 'keyboard', 'spinbox', 'led',
             'calendar', 'tileview', 'spangroup', 'animimg', 'button', 'image', 'label', 'tabview', 'win', 'spinner'}
    # Animation start without owned asset descriptors is unsafe. Pointer-backed
    # chart series, canvas buffers, drawing and other asset APIs remain unbound.
    excluded = {'lv_animimg_start'}
    out = ['/* Generated from actual public LVGL8/9 widget declarations; safe common subset.',
           ' * Receiver kind checked, exact arity/types/ranges checked; no raw pointers. */']
    inventory = {}
    for name in sorted(set(tables[0]) & set(tables[1])):
        family = name.split('_')[1]
        if family not in kinds or name in excluded:
            continue
        entries = [t[name] for t in tables]
        if any(e['return'] not in scalar | {'void', 'const char *'} for e in entries):
            continue
        if len(entries[0]['params']) != len(entries[1]['params']):
            continue
        if any(p not in scalar and not (p == 'const char *' and name in copied)
               for e in entries for p in e['params']):
            continue
        suffix = name[len('lv_' + family + '_'):]
        if suffix == 'create':
            continue  # constructors have separate ownership handling
        argc = len(entries[0]['params'])
        macros = []
        for v in (8, 9):
            macro = {'spangroup': 'SPAN', 'button': 'BTN' if v == 8 else 'BUTTON',
                     'image': 'IMG' if v == 8 else 'IMAGE'}.get(family, family.upper())
            macros.append(macro)
        if macros[0] == macros[1]:
            out.append(f'#if LV_USE_{macros[0]}')
        else:
            out += [f'#if (LVGL_VERSION_MAJOR == 8 && LV_USE_{macros[0]}) || (LVGL_VERSION_MAJOR == 9 && LV_USE_{macros[1]})']
        out += [f'    if (!strcmp(s->kind, "{family}") && !strcmp(method, "{suffix}")) {{',
                f'        if (argc != {argc + 1}) {{ mcs_raise(vm, "ArgumentException", "{family}.{suffix}: expected {argc} arguments"); return mcs_null(); }}']
        for i, ps in enumerate(zip(entries[0]['params'], entries[1]['params'])):
            index = i + 1
            if ps == ('const char *', 'const char *'):
                out.append(f'        const char* a{i} = mcs_to_cstr(vm, argv[{index}]);')
            elif ps == ('bool', 'bool'):
                out.append(f'        bool a{i} = mcs_to_bool(vm, argv[{index}]);')
            else:
                if 'bool' in ps or 'const char *' in ps:
                    raise ValueError(f'incompatible parameter {name}: {ps}')
                lo = max(bounds[t][0] for t in ps); hi = min(bounds[t][1] for t in ps)
                # Bounded dynamic resource sizes; zero counts rejected where
                # upstream uses division/allocation. Full ownership/OOM audit pending.
                if name == 'lv_chart_set_point_count': lo, hi = 1, 512
                if name == 'lv_chart_set_div_line_count': lo, hi = 0, 512
                if name == 'lv_textarea_set_max_length': lo, hi = 0, 65535
                if name == 'lv_spinbox_set_digit_format': lo, hi = (1, 9) if i == 0 else (0, 9)
                if name == 'lv_calendar_set_today_date': lo, hi = [(1, 9999), (1, 12), (1, 31)][i]
                out += [f'        mcs_int_t a{i} = mcs_to_int(vm, argv[{index}]);',
                        f'        if (mcs_has_exception(vm)) return mcs_null();',
                        f'        if (!number_range((int64_t)a{i}, INT64_C({lo}), INT64_C({hi}))) {{',
                        f'            mcs_raise(vm, "ArgumentOutOfRangeException", "{family}.{suffix} argument {index}: {lo}..{hi}"); return mcs_null();', '        }']
        out.append('        if (mcs_has_exception(vm)) return mcs_null();')
        if name.endswith('_set_range') and argc == 2:
            out.append('        if (a0 >= a1) { mcs_raise(vm, "ArgumentException", "range minimum must be below maximum"); return mcs_null(); }')
        if name == 'lv_spinbox_set_digit_format':
            out.append('        if (a1 > a0) { mcs_raise(vm, "ArgumentException", "separator position exceeds digit count"); return mcs_null(); }')
        for v, entry in zip((8, 9), entries):
            out.append('#if LVGL_VERSION_MAJOR == 8' if v == 8 else '#else')
            args = ['s->obj']
            for i, typ in enumerate(entry['params']):
                if typ == 'const char *': args.append(f'a{i}')
                elif typ == 'lv_color_t': args.append(f'lv_color_hex((uint32_t)a{i})')
                else: args.append(f'({typ})a{i}')
            call = entry['fn'] + '(' + ', '.join(args) + ')'
            ret = entry['return']
            if ret == 'void': out += [f'        {call};', '        return mcs_null();']
            elif ret == 'bool': out.append(f'        return mcs_bool({call});')
            elif ret == 'const char *': out += [f'        const char* text = {call};', '        return text ? mcs_string(vm, text) : mcs_null();']
            elif ret == 'lv_color_t':
                convert = 'lv_color_to32' if v == 8 else 'lv_color_to_u32'
                out.append(f'        return mcs_int((mcs_int_t)({convert}({call}) & 0xFFFFFFu));')
            else: out.append(f'        return mcs_int((mcs_int_t){call});')
        out += ['#endif', '    }', '#endif']
        inventory[name] = {'receiver': family, 'call_name': suffix, 'versions': dict(zip(('8.4.0', '9.4.0'), entries))}
    return '\n'.join(out) + '\n', inventory

def generate(a, b):
    tables = [styles(a), styles(b)]
    out = ['/* Generated by tools/gen_lvgl.py from actual 8.4.0 / 9.4.0 headers.',
           ' * Scalar common subset only; pointer/asset/enum APIs need typed ownership wrappers. */']
    bound = []
    for n in sorted(set(tables[0]) & set(tables[1])):
        types = [d[n] for d in tables]
        if n == 'layout':
            continue  # layout IDs are registrations, NOT arbitrary integers
        if all(t in ('lv_coord_t', 'int32_t', 'uint32_t', 'uint16_t', 'lv_opa_t', 'lv_color_t', 'bool') for t in types):
            if types[0] == 'lv_color_t':
                expr, lo, hi = 'lv_color_hex((uint32_t)value)', 0, 0xFFFFFF
            elif types[0] == 'bool':
                expr, lo, hi = '(bool)value', 0, 1
            elif types[0] == 'lv_opa_t':
                expr, lo, hi = '(lv_opa_t)value', 0, 255
            elif types[0] == 'uint32_t':
                expr, lo, hi = '(uint32_t)value', 0, 0xFFFFFFFF
            elif types[0] == 'uint16_t':
                expr, lo, hi = '(uint16_t)value', 0, 65535
            else:
                expr, lo, hi = '(int32_t)value', -32767, 32767
            out += [f'    if (!strcmp(name, "{n}")) {{',
                    f'        if (value < {lo} || (int64_t)value > INT64_C({hi})) {{',
                    f'            mcs_raise(vm, "ArgumentOutOfRangeException", "style {n}: {lo}..{hi}");',
                    '            return mcs_null();', '        }',
                    f'        lv_obj_set_style_{n}(s->obj, {expr}, (lv_style_selector_t)selector);',
                    '        return mcs_null();', '    }']
            bound.append(n)
    calls, call_inventory = widget_calls(a, b)
    inventory = {'owned_text_calls': ['lv_dropdown_set_text', 'lv_dropdown_set_symbol', 'lv_textarea_set_accepted_chars', 'lv_textarea_set_password_bullet', 'lv_label_set_text_static', 'lv_checkbox_set_text_static'], 'bound_common_widget_calls': call_inventory, 'schema': 1, 'status': 'WIP: scalar styles/common widget methods/owned text subset; not a complete LVGL wrapper',
                 'pins': PINS, 'bound_common_style_setters': bound,
                 'versions': {}}
    for version, root, table in zip(PINS, (a, b), tables):
        headers = {}
        api = {}
        for p in sorted((root / 'src').rglob('*.h')):
            # Private/internal implementation files are not a public binding promise.
            if 'private' in p.name or p.name in ('lv_conf_internal.h', 'lv_conf_kconfig.h'):
                continue
            text = re.sub(r'/\*.*?\*/|//[^\n]*', '', p.read_text(errors='replace'), flags=re.S)
            names = sorted(set(re.findall(r'\b(lv_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*(?:;|\{)', text)))
            if names:
                rel = str(p.relative_to(root))
                headers[rel] = hashlib.sha256(p.read_bytes()).hexdigest()
                for name in names:
                    api.setdefault(name, []).append(rel)
        inventory['versions'][version] = {'public_header_hashes': headers,
            'declaration_candidates': api,
            'style_setters': {n: {'type': t, 'status': 'bound' if n in bound else 'unbound'} for n, t in sorted(table.items())},
            'inventory_limits': 'Lexical declaration candidate inventory, including compile-time-disabled APIs. Not a preprocessed public ABI proof. Every unlisted/unbound API is unsupported until implemented, built and tested.'}
    return '\n'.join(out) + '\n', json.dumps(inventory, indent=2, sort_keys=True) + '\n', calls


def style_readback_test(a, b):
    _, inventory_text, _ = generate(a, b)
    inventory = json.loads(inventory_text)
    style_types = styles(a)
    out = ['/* Generated functional readback for every advertised common scalar style setter. */',
           'run(vm, "var stylesGui = new LvGui(20, 20); var stylesObj = stylesGui.Screen.Create(\\\"object\\\");");']
    values = {}
    for name in inventory['bound_common_style_setters']:
        typ = style_types[name]
        value = 0x103050 if typ == 'lv_color_t' else 123 if typ == 'lv_opa_t' else 1
        values[name] = value
        out.append(f'run(vm, "stylesObj.SetStyle(\\\"{name}\\\", {value});");')
    out += ['#if LVGL_VERSION_MAJOR == 9', 'lv_obj_t* style_obj = lv_obj_get_child(lv_screen_active(), 0);',
            '#else', 'lv_obj_t* style_obj = lv_obj_get_child(lv_scr_act(), 0);', '#endif', 'CHECK(style_obj != NULL);']
    for name, value in values.items():
        get = f'lv_obj_get_style_{name}(style_obj, LV_PART_MAIN)'
        if style_types[name] == 'lv_color_t':
            out += ['#if LVGL_VERSION_MAJOR == 9', f'CHECK(lv_color_eq({get}, lv_color_hex({value})));', '#else',
                    f'CHECK(lv_color_to32({get}) == lv_color_to32(lv_color_hex({value})));', '#endif']
        else:
            out.append(f'CHECK({get} == {value});')
    out.append('run(vm, "stylesGui.Dispose();");')
    return '\n'.join(out) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--v8', required=True, type=Path); p.add_argument('--v9', required=True, type=Path)
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    for root, v in ((args.v8, 8), (args.v9, 9)):
        h = root / ('lv_version.h' if v == 9 else 'lvgl.h')
        version = tuple(int(re.search(r'#define\s+LVGL_VERSION_' + part + r'\s+(\d+)', h.read_text())[1]) for part in ('MAJOR', 'MINOR', 'PATCH'))
        if version != (v, 4, 0): p.error(f'actual headers {version}, expected {(v,4,0)}')
    outputs = zip((ROOT/'modules/lvgl/mcs_lvgl_styles.inc', ROOT/'docs/development/lvgl-coverage.json', ROOT/'modules/lvgl/mcs_lvgl_calls.inc', ROOT/'tests/lvgl/style_readback.inc'), (*generate(args.v8, args.v9), style_readback_test(args.v8, args.v9)))
    for path, text in outputs:
        if args.check:
            if not path.exists() or path.read_text() != text: raise SystemExit(f'LVGL generated output drift: {path}')
        else:
            path.write_text(text)
    print('PASS LVGL header-derived scalar styles / explicit unbound coverage' if args.check else 'Generated LVGL scalar styles and versioned candidate coverage')

if __name__ == '__main__': main()
