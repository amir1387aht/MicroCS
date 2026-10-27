#!/usr/bin/env python3
"""Conservative, config-independent LVGL header discovery; not an ABI parser.

Lists unpreprocessed public-looking declarations (including disabled features).
Never equates native calls made internally with public C# API coverage. A binding
operation represents only the documented constrained semantics, not all arguments
or overloads of a native declaration. Output is a review backlog, not certification.
"""
import argparse
import json
import re
from pathlib import Path

# Explicit semantic mapping, not a grep of functions used in the implementation.
OPERATIONS = {
    'LvObject(Object)': ['lv_obj_create'],
    'LvObject(Label)': ['lv_label_create'],
    'LvObject(Button)': ['lv_btn_create', 'lv_button_create'],
    'LvObject(Slider)': ['lv_slider_create'],
    'LvObject(Bar)': ['lv_bar_create'],
    'LvObject(Checkbox)': ['lv_checkbox_create'],
    'LvObject(Switch)': ['lv_switch_create'],
    'LvObject(Arc)': ['lv_arc_create'],
    'LvObject(Image)': ['lv_img_create', 'lv_image_create'],
    'Dispose': ['lv_obj_del', 'lv_obj_delete'],
    'Center': ['lv_obj_center'],
    'SetPosition': ['lv_obj_set_pos'],
    'SetSize': ['lv_obj_set_size'],
    'SetBackground': ['lv_obj_set_style_bg_color', 'lv_obj_set_style_bg_opa'],
    'SetTextColor': ['lv_obj_set_style_text_color'],
    'SetFont': ['lv_obj_set_style_text_font', 'lv_font_load', 'lv_binfont_create'],
    'Text': ['lv_label_set_text', 'lv_checkbox_set_text'],
    'Source': ['lv_img_set_src', 'lv_image_set_src'],
    'Value': ['lv_slider_set_value', 'lv_slider_get_value', 'lv_bar_set_value',
              'lv_bar_get_value', 'lv_arc_set_value', 'lv_arc_get_value'],
    'Checked': ['lv_obj_add_state', 'lv_obj_clear_state', 'lv_obj_remove_state', 'lv_obj_has_state'],
    'OnClicked/OnChanged': ['lv_obj_add_event_cb'],
}
HOST_ONLY = {
    'lv_init': 'Host owns LVGL initialization',
    'lv_deinit': 'Host owns LVGL shutdown',
    'lv_tick_inc': 'Host owns time source',
    'lv_tick_set_cb': 'Host owns time source',
    'lv_timer_handler': 'Host owns GUI scheduling',
    'lv_task_handler': 'Host owns GUI scheduling',
    'lv_display_create': 'Host owns displays and buffers',
    'lv_disp_drv_register': 'Host owns displays and buffers',
    'lv_indev_create': 'Host owns input devices',
    'lv_indev_drv_register': 'Host owns input devices',
}

def declarations(root):
    found = {}
    # Avoid private headers. Public-looking internal/support declarations can
    # remain: review required. Macros/function-pointer typedefs are not counted.
    pattern = re.compile(r'^\s*(?:[A-Za-z_]\w*[ \t*]+)+(lv_[A-Za-z0-9_]+)\s*\(', re.M)
    headers = set((root / 'src').rglob('*.h')) | set((root / 'include').rglob('*.h'))
    for path in sorted(headers):
        if 'private' in path.stem or path.name.startswith('_'):
            continue
        text = path.read_text(errors='replace')
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
        for match in pattern.finditer(text):
            prefix = match.group(0).split(match.group(1))[0]
            if re.search(r'\b(return|typedef|else)\b', prefix):
                continue
            if match.group(1).endswith('_internal'):
                continue
            found.setdefault(match.group(1), []).append(str(path.relative_to(root)))
    return found

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('lvgl', type=Path)
    parser.add_argument('--json', type=Path, required=True)
    args = parser.parse_args()
    declarations_by_name = declarations(args.lvgl)
    if 'lv_obj_create' not in declarations_by_name or len(declarations_by_name) < 100:
        parser.error('Expected an LVGL source checkout with public src headers')
    mapped = {}
    for operation, names in OPERATIONS.items():
        for name in names:
            mapped.setdefault(name, []).append(operation)
    rows = []
    for name, headers in sorted(declarations_by_name.items()):
        status = 'binding-operation' if name in mapped else 'host-owned' if name in HOST_ONLY else 'unwrapped-review'
        rows.append(dict(name=name, headers=sorted(set(headers)), status=status,
                         operations=mapped.get(name, []), reason=HOST_ONLY.get(name)))
    counts = {key: sum(r['status'] == key for r in rows)
              for key in ('binding-operation', 'host-owned', 'unwrapped-review')}
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(dict(
        scope='Unpreprocessed header discovery; not exact configured API or full semantic coverage. '
              'Excludes private-named headers, macros, types and function-pointer typedefs. '
              'Unwrapped does NOT mean unsafe: it is an implementation/review backlog.',
        counts=counts, declarations=rows), indent=2) + '\n')
    print(f'{args.lvgl.name}: {len(rows)} discovered declarations; ' + ', '.join(f'{k}={v}' for k,v in counts.items()))

if __name__ == '__main__':
    main()
