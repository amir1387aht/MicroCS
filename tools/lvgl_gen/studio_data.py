#!/usr/bin/env python3
"""Studio IntelliSense data for the LVGL binding: tools/studio/lvgl_api.js (generated, committed, loaded on demand).

  python3 tools/lvgl_gen/studio_data.py [--json tools/lvgl_gen/lvgl_api.json] [--out tools/studio/lvgl_api.js] [--check]

The input is lvgl_api.json, written by gen.py / build_api.py --all (`make lvgl-api`, build/lvgl-api/lvgl_api.json): every function,
constant, symbol and font of LVGL 8.0 ... 9.x. The output keeps one line per function (return type, parameters, first
sentence of the doc) in the C# spelling of the binding. --check fails when the committed file is out of date (CI)."""
import argparse, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# hand-written wrappers (gen.py OVERRIDE): callbacks are C# lambdas, arrays are C# arrays - not in lvgl_api.json
CB = {
    'event': 'Action<lv_event_t>', 'timer': 'Action<lv_timer_t>', 'anim_exec': 'Action<lv_anim_t, int>',
    'anim': 'Action<lv_anim_t>', 'anim_path': 'Func<lv_anim_t, int>', 'anim_get': 'Func<lv_anim_t, int>',
    'async': 'Action<object>', 'observer': 'Action<lv_observer_t, lv_subject_t>',
    'walk': 'Func<lv_obj_t, object, lv_obj_tree_walk_res_t>', 'log': 'Action<int, string>',
    'msg': 'Action<object, lv_msg_t>',
}
OVERRIDES = {
    'lv_obj_add_event_cb': ('lv_event_dsc_t', [('lv_obj_t', 'obj'), (CB['event'], 'event_cb'), ('lv_event_code_t', 'filter'), ('object', 'user_data')], 'Add an event handler (a C# lambda) to an object; `filter` is an LV_EVENT_* code or LV_EVENT_ALL'),
    'lv_obj_remove_event_cb': ('bool', [('lv_obj_t', 'obj'), (CB['event'], 'event_cb')], 'Remove the handler (null: the first one) from an object'),
    'lv_obj_remove_event_cb_with_user_data': ('uint32_t', [('lv_obj_t', 'obj'), (CB['event'], 'event_cb'), ('object', 'user_data')], 'Remove the handlers with this callback and user data'),
    'lv_obj_remove_event_dsc': ('bool', [('lv_obj_t', 'obj'), ('lv_event_dsc_t', 'dsc')], 'Remove one handler by the descriptor lv_obj_add_event_cb returned'),
    'lv_obj_remove_event': ('bool', [('lv_obj_t', 'obj'), ('uint32_t', 'index')], 'Remove the handler at an index'),
    'lv_event_get_user_data': ('object', [('lv_event_t', 'e')], 'The user data given to lv_obj_add_event_cb'),
    'lv_event_get_target': ('lv_obj_t', [('lv_event_t', 'e')], 'The object the event was sent to'),
    'lv_event_get_current_target': ('lv_obj_t', [('lv_event_t', 'e')], 'The object whose handler is running (bubbling)'),
    'lv_obj_set_user_data': ('void', [('lv_obj_t', 'obj'), ('object', 'user_data')], 'Attach any C# value to an object'),
    'lv_obj_get_user_data': ('object', [('lv_obj_t', 'obj')], 'The value set with lv_obj_set_user_data'),
    'lv_timer_create': ('lv_timer_t', [(CB['timer'], 'timer_cb'), ('uint32_t', 'period'), ('object', 'user_data')], 'Create a timer that calls a C# lambda every `period` ms'),
    'lv_timer_set_cb': ('void', [('lv_timer_t', 'timer'), (CB['timer'], 'timer_cb')], 'Change the timer callback'),
    'lv_timer_get_user_data': ('object', [('lv_timer_t', 'timer')], 'The timer user data'),
    'lv_timer_set_user_data': ('void', [('lv_timer_t', 'timer'), ('object', 'user_data')], 'Set the timer user data'),
    'lv_timer_delete': ('void', [('lv_timer_t', 'timer')], 'Delete a timer'),
    'lv_async_call': ('lv_result_t', [(CB['async'], 'async_xcb'), ('object', 'user_data')], 'Run a C# lambda once, at the next lv_timer_handler()'),
    'lv_async_call_cancel': ('lv_result_t', [(CB['async'], 'async_xcb'), ('object', 'user_data')], 'Cancel pending lv_async_call calls'),
    'lv_anim_set_exec_cb': ('void', [('lv_anim_t', 'a'), ('Action<lv_obj_t, int>', 'exec_cb')], 'Animate with a setter such as lv_obj_set_x or a C# lambda (var, value)'),
    'lv_anim_set_custom_exec_cb': ('void', [('lv_anim_t', 'a'), (CB['anim_exec'], 'exec_cb')], 'Animate with a C# lambda (anim, value)'),
    'lv_anim_set_path_cb': ('void', [('lv_anim_t', 'a'), (CB['anim_path'], 'path_cb')], 'Easing: lv_anim_path_linear / ease_in / ease_out / overshoot / bounce... or a C# lambda'),
    'lv_anim_set_ready_cb': ('void', [('lv_anim_t', 'a'), (CB['anim'], 'ready_cb')], 'Called when the animation finishes'),
    'lv_anim_set_start_cb': ('void', [('lv_anim_t', 'a'), (CB['anim'], 'start_cb')], 'Called when the animation starts'),
    'lv_anim_set_deleted_cb': ('void', [('lv_anim_t', 'a'), (CB['anim'], 'deleted_cb')], 'Called when the animation is deleted'),
    'lv_anim_set_get_value_cb': ('void', [('lv_anim_t', 'a'), (CB['anim_get'], 'get_value_cb')], 'Get the start value for lv_anim_set_values-less animations'),
    'lv_anim_set_user_data': ('void', [('lv_anim_t', 'a'), ('object', 'user_data')], 'Animation user data'),
    'lv_anim_get_user_data': ('object', [('lv_anim_t', 'a')], 'Animation user data'),
    'lv_anim_set_var': ('void', [('lv_anim_t', 'a'), ('lv_obj_t', 'var')], 'The object (or any value) the animation drives'),
    'lv_anim_start': ('lv_anim_t', [('lv_anim_t', 'a')], 'Start an animation'),
    'lv_anim_delete': ('bool', [('object', 'var'), ('Action<lv_obj_t, int>', 'exec_cb')], 'Delete the animations of a variable (exec_cb null: all)'),
    'lv_line_set_points': ('void', [('lv_obj_t', 'obj'), ('lv_point_precise_t[]', 'points'), ('uint32_t', 'point_num')], 'Set the points of a line (the array is kept alive for you)'),
    'lv_buttonmatrix_set_map': ('void', [('lv_obj_t', 'obj'), ('string[]', 'map')], 'Button map: strings, "\\n" for a new row, "" at the end'),
    'lv_keyboard_set_map': ('void', [('lv_obj_t', 'kb'), ('lv_keyboard_mode_t', 'mode'), ('string[]', 'map'), ('lv_buttonmatrix_ctrl_t[]', 'ctrl_map')], 'Set a keyboard layout'),
    'lv_dropdown_get_selected_str': ('void', [('lv_obj_t', 'obj'), ('byte[]', 'buf'), ('uint32_t', 'buf_size')], 'Selected option text into a byte[]'),
    'lv_roller_get_selected_str': ('string', [('lv_obj_t', 'obj'), ('byte[]', 'buf'), ('uint32_t', 'buf_size')], 'Selected option text (buf may be null)'),
    'lv_subject_add_observer': ('lv_observer_t', [('lv_subject_t', 'subject'), (CB['observer'], 'observer_cb'), ('object', 'user_data')], 'Call a C# lambda when the subject changes (LVGL 9.1+)'),
    'lv_subject_add_observer_obj': ('lv_observer_t', [('lv_subject_t', 'subject'), (CB['observer'], 'observer_cb'), ('lv_obj_t', 'obj'), ('object', 'user_data')], 'Observer that is removed with the object (LVGL 9.1+)'),
    'lv_observer_get_user_data': ('object', [('lv_observer_t', 'observer')], 'Observer user data'),
    'lv_subject_init_string': ('void', [('lv_subject_t', 'subject'), ('byte[]', 'buf'), ('byte[]', 'prev_buf'), ('size_t', 'size'), ('string', 'value')], 'String subject (buffers may be null: allocated for you)'),
    'lv_chart_set_ext_y_array': ('void', [('lv_obj_t', 'obj'), ('lv_chart_series_t', 'ser'), ('int[]', 'array')], 'Use a C# array as the series Y values (kept alive)'),
    'lv_chart_set_ext_x_array': ('void', [('lv_obj_t', 'obj'), ('lv_chart_series_t', 'ser'), ('int[]', 'array')], 'Use a C# array as the series X values (kept alive)'),
    'lv_calendar_set_highlighted_dates': ('void', [('lv_obj_t', 'obj'), ('lv_calendar_date_t[]', 'highlighted'), ('size_t', 'date_num')], 'Highlighted dates (kept alive)'),
    'lv_canvas_set_buffer': ('void', [('lv_obj_t', 'obj'), ('byte[]', 'buf'), ('int', 'w'), ('int', 'h'), ('lv_color_format_t', 'cf')], 'Canvas buffer; null allocates one that lives with the canvas'),
    'lv_obj_set_grid_dsc_array': ('void', [('lv_obj_t', 'obj'), ('int[]', 'col_dsc'), ('int[]', 'row_dsc')], 'Grid column / row sizes: pixels, LV_GRID_FR(n) or LV_GRID_CONTENT (kept alive)'),
    'lv_style_set_grid_column_dsc_array': ('void', [('lv_style_t', 'style'), ('int[]', 'value')], 'Grid column sizes of a style (kept alive)'),
    'lv_style_set_grid_row_dsc_array': ('void', [('lv_style_t', 'style'), ('int[]', 'value')], 'Grid row sizes of a style (kept alive)'),
    'lv_style_transition_dsc_init': ('void', [('lv_style_transition_dsc_t', 'tr'), ('int[]', 'props'), (CB['anim_path'], 'path_cb'), ('uint32_t', 'time'), ('uint32_t', 'delay'), ('object', 'user_data')], 'Style transition: properties, easing, time, delay'),
    'lv_log_register_print_cb': ('void', [(CB['log'], 'print_cb')], 'Receive LVGL log lines in C# (level, text); null removes it'),
    'lv_obj_tree_walk': ('void', [('lv_obj_t', 'start_obj'), (CB['walk'], 'cb'), ('object', 'user_data')], 'Visit an object tree; return LV_OBJ_TREE_WALK_NEXT / SKIP_CHILDREN / END'),
    'lv_msg_subscribe': ('object', [('uint32_t', 'msg_id'), (CB['msg'], 'cb'), ('object', 'user_data')], 'LVGL 8 messages: call a C# lambda for a message id; returns a token'),
    'lv_msg_subscribe_obj': ('object', [('uint32_t', 'msg_id'), ('lv_obj_t', 'obj'), ('object', 'user_data')], 'LVGL 8: send LV_EVENT_MSG_RECEIVED to an object'),
    'lv_msg_get_user_data': ('object', [('lv_msg_t', 'm')], 'User data of a subscription'),
    'lv_msg_unsubscribe': ('bool', [('object', 'token')], 'Remove a subscription'),
    'lvgl_host_run': ('void', [('int', 'ms')], 'Desktop test screen (mcs --lvgl): advance LVGL time by ms'),
    'lvgl_host_pixel': ('int', [('int', 'x'), ('int', 'y')], 'Desktop test screen: colour 0xRRGGBB of a pixel'),
    'lvgl_host_touch': ('void', [('int', 'x'), ('int', 'y'), ('bool', 'down')], 'Desktop test screen: move / press / release the simulated pointer'),
    'lvgl_host_screenshot': ('bool', [('string', 'path')], 'Desktop test screen: write the screen as a PPM file'),
}
MACROS = {   # function-like macros of mcs_lvgl_macros.inc: name -> (params, doc)
    'LV_PCT': ('x', 'Percentage size / position, e.g. lv_obj_set_width(o, LV_PCT(50))'), 'LV_GRID_FR': ('x', 'Grid track: x fractions of the free space'),
    'LV_ABS': ('x', 'Absolute value'), 'LV_MIN': ('a,b', 'Smaller of two values'), 'LV_MAX': ('a,b', 'Larger of two values'),
    'LV_MIN3': ('a,b,c', 'Smallest of three'), 'LV_MAX3': ('a,b,c', 'Largest of three'), 'LV_MIN4': ('a,b,c,d', 'Smallest of four'),
    'LV_MAX4': ('a,b,c,d', 'Largest of four'), 'LV_CLAMP': ('min,val,max', 'Limit a value to a range'),
    'LV_ROUND_UP': ('x', 'Round up to a multiple of 8'), 'LV_UDIV255': ('x', 'x / 255'), 'LV_OPA_MIX2': ('a,b', 'Mix two opacities'),
    'LV_OPA_MIX3': ('a,b,c', 'Mix three opacities'), 'LV_COORD_IS_PX': ('x', 'True for a plain pixel coordinate'),
    'LV_COORD_IS_PCT': ('x', 'True for an LV_PCT value'), 'LV_COORD_IS_SPEC': ('x', 'True for a special coordinate (LV_PCT, LV_SIZE_CONTENT)'),
    'LV_COORD_GET_PCT': ('x', 'The percentage of an LV_PCT value'), 'LV_COORD_PLAIN': ('x', 'Coordinate without the special bits'),
    'LV_COORD_SET_SPEC': ('x', 'Make a special coordinate'), 'LV_COORD_TYPE': ('x', 'Type bits of a coordinate'),
    'LV_VERSION_CHECK': ('x,y,z', 'True when LVGL is at least x.y.z'), 'LV_CANVAS_BUF_SIZE': ('w,h,bpp,stride_align', 'Bytes of a canvas buffer'),
    'LV_DRAW_BUF_SIZE': ('w,h,cf', 'Bytes of a draw buffer (LVGL 9)'), 'LV_DRAW_BUF_STRIDE': ('w,cf', 'Stride of a draw buffer (LVGL 9)'),
    'LV_COLOR_FORMAT_GET_BPP': ('cf', 'Bits per pixel of a colour format (LVGL 9)'), 'LV_COLOR_FORMAT_GET_SIZE': ('cf', 'Bytes per pixel of a colour format (LVGL 9)'),
    'LV_COLOR_FORMAT_IS_ALPHA_ONLY': ('cf', 'Alpha-only format? (LVGL 9)'), 'LV_COLOR_FORMAT_IS_INDEXED': ('cf', 'Indexed format? (LVGL 9)'),
    'LV_COLOR_INDEXED_PALETTE_SIZE': ('cf', 'Palette entries of an indexed format (LVGL 9)'),
}
ALIASES = {'lv_buttonmatrix_set_map': ['lv_btnmatrix_set_map'], 'lv_timer_delete': ['lv_timer_del'], 'lv_anim_delete': ['lv_anim_del'],
           'lv_buttonmatrix_set_ctrl_map': ['lv_btnmatrix_set_ctrl_map']}
INTS = {'int8_t', 'uint8_t', 'int16_t', 'uint16_t', 'int32_t', 'uint32_t', 'int64_t', 'uint64_t', 'size_t', 'ssize_t', 'intptr_t',
        'uintptr_t', 'char', 'short', 'long', 'unsigned', 'int', 'unsigned int', 'unsigned char', 'unsigned short', 'unsigned long',
        'signed char', 'lv_coord_t', 'lv_opa_t'}


def cs_type(t):
    """C type spelled in a header -> the type a script sees"""
    t = re.sub(r'\b(const|volatile|struct|enum|union)\b', '', t).replace('_Bool', 'bool')
    t = ' '.join(t.split())
    ptr = t.count('*')
    base = t.replace('*', '').strip()
    if base == 'char' and ptr == 1:
        return 'string'
    if base == 'void' and ptr:
        return 'object'
    if base in ('float', 'double'):
        base = 'float'
    if ptr == 0:
        return base
    if re.match(r'^_?lv_\w+_t$', base) and not base.endswith('_dsc_t') or base.startswith('lv_') and ptr == 1 and base not in INTS:
        return base.lstrip('_') if ptr == 1 else base + '[]' * (ptr - 1)
    return base + '[]'


def one_line(doc):
    doc = ' '.join((doc or '').replace('\n', ' ').split())
    m = re.match(r'^(.*?[.!?])(\s|$)', doc)
    doc = m.group(1) if m else doc
    doc = re.sub(r'\s*@\w+.*$', '', doc)
    return doc[:140]


def build(db):
    api = db['api']
    f, k, s, v = {}, [], [], {}
    for name, e in sorted(api.items()):
        kind = e['k']
        if kind == 'f':
            if name in OVERRIDES:
                continue
            ps = [[cs_type(t), (n or 'p%d' % i)] for i, (t, n) in enumerate(zip(e['sig'], e['p'] + [''] * len(e['sig'])))]
            f[name] = [cs_type(e['ret']), ps, one_line(e.get('doc', ''))]
        elif kind == 'k':
            k.append(name)
        elif kind == 's':
            s.append(name)
        elif kind == 'v':
            v[name] = e.get('type', 'object')
        elif kind == 'c':
            pass
    for name, (ret, ps, doc) in OVERRIDES.items():
        f[name] = [ret, [[t, n] for t, n in ps], doc]
        for al in ALIASES.get(name, []):
            f[al] = [ret, [[t, n] for t, n in ps], doc]
    for name, (ps, doc) in MACROS.items():
        f[name] = ['int', [['int', p] for p in ps.split(',')], doc]
    tags = db.get('tags', [])
    return {'versions': [tags[0], tags[-1]] if tags else [], 'f': f, 'k': k, 's': s, 'v': v, 'classes': sorted(set(c.lstrip('_') for c in db.get('classes', [])))}


def render(data):
    out = ['/* MicroCS Studio - LVGL API for completion / signature help / hover (generated by tools/lvgl_gen/studio_data.py from',
           ' * lvgl_api.json: LVGL %s ... %s; loaded on demand, see studio.js). Do not edit. */' % tuple(data['versions'] or ('?', '?')),
           'window.MCS_LVGL = {versions:%s,' % json.dumps(data['versions'])]
    out.append('classes:%s,' % json.dumps(data['classes'], separators=(',', ':')))
    out.append('k:%s,' % json.dumps(data['k'], separators=(',', ':')))
    out.append('s:%s,' % json.dumps(data['s'], separators=(',', ':')))
    out.append('v:%s,' % json.dumps(data['v'], separators=(',', ':'), sort_keys=True))
    out.append('f:{')
    for i, (n, e) in enumerate(sorted(data['f'].items())):
        out.append('%s:%s%s' % (json.dumps(n), json.dumps(e, separators=(',', ':'), ensure_ascii=False), ',' if i < len(data['f']) - 1 else ''))
    out.append('}};')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', default=os.path.join(HERE, 'lvgl_api.json'))
    ap.add_argument('--out', default=os.path.join(REPO, 'tools', 'studio', 'lvgl_api.js'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    text = render(build(json.load(open(a.json))))
    if a.check:
        if not os.path.exists(a.out) or open(a.out, encoding='utf-8').read() != text:
            sys.exit('studio_data: %s is out of date - run python3 tools/lvgl_gen/studio_data.py' % a.out)
        print('studio_data: up to date')
        return
    with open(a.out, 'w', encoding='utf-8') as fh:
        fh.write(text)
    print('wrote %s (%d KB)' % (a.out, len(text) // 1024))


if __name__ == '__main__':
    main()
