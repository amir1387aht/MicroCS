#!/usr/bin/env python3
"""Merge the per-tag LVGL API databases (tools/lvgl_gen/extract.py) into the
MicroCS binding: modules/lvgl/mcs_lvgl_api.inc (+ Studio / docs data).

    python3 tools/lvgl_gen/gen.py <db-dir> [--out modules/lvgl/mcs_lvgl_api.inc]

Every API entry is guarded by the range of LVGL versions it exists in (MCS_LV_VER)
and by the header's own #if feature conditions, so one generated file compiles
against any LVGL 8.x / 9.x with any lv_conf.h."""
import json, os, re, sys, glob, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

INT_BASE = {'char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'unsigned',
            'long', 'unsigned long', 'long long', 'unsigned long long', 'int8_t', 'uint8_t', 'int16_t',
            'uint16_t', 'int32_t', 'uint32_t', 'int64_t', 'uint64_t', 'size_t', 'intptr_t', 'uintptr_t',
            'signed', 'short int', 'long int', 'unsigned short int', 'unsigned long int', 'ptrdiff_t', 'wchar_t'}
FLOAT_BASE = {'float', 'double'}
COLOR = {'lv_color_t'}
# porting, memory, OS, internal or raw-pointer APIs that are never bound (docs/LVGL.md)
SKIP_PREFIX = ('_lv_', 'lv_mem_', 'lv_ll_', 'lv_rb_', 'lv_cache_', 'lv_os_', 'lv_thread_', 'lv_mutex_',
               'lv_draw_sw_', 'lv_draw_buf_', 'lv_draw_unit', 'lv_draw_task', 'lv_draw_dispatch', 'lv_lru_',
               'lv_malloc', 'lv_free', 'lv_realloc', 'lv_calloc', 'lv_zalloc', 'lv_memcpy', 'lv_memset',
               'lv_memmove', 'lv_memcmp', 'lv_memzero', 'lv_strcpy', 'lv_strncpy', 'lv_strlen', 'lv_strcmp',
               'lv_strncmp', 'lv_strdup', 'lv_strndup', 'lv_strcat', 'lv_strncat', 'lv_strchr', 'lv_snprintf',
               'lv_vsnprintf', 'lv_sprintf', 'lv_array_', 'lv_iter_', 'lv_circle_buf_', 'lv_profiler_',
               'lv_tlsf_', 'lv_gpu_', 'lv_sdl_', 'lv_x11_', 'lv_wayland_', 'lv_linux_', 'lv_windows_',
               'lv_nuttx_', 'lv_evdev_', 'lv_libinput_', 'lv_st7', 'lv_ili9', 'lv_lcd_', 'lv_tft_espi',
               'lv_opengles_', 'lv_glfw_', 'lv_qnx_', 'lv_uefi_', 'lv_vg_lite_', 'lv_nema_', 'lv_ppa_',
               'lv_pxp_', 'lv_g2d_', 'lv_dave2d_', 'lv_test_', 'lv_demo_', 'lv_example_', 'lv_ft81x_',
               'lv_renesas_', 'lv_st_ltdc_', 'lv_nxp_', 'lv_ime_pinyin_set_dict', 'lv_gltf_', 'lv_3d',
               'lv_fs_stdio_', 'lv_fs_posix_', 'lv_fs_win32_', 'lv_fs_fatfs_', 'lv_fs_memfs_', 'lv_fs_littlefs_',
               'lv_fs_arduino_', 'lv_fs_uefi_', 'lv_anim_core_', 'lv_event_push', 'lv_event_pop', 'lv_txt_',
               'lv_bin_decoder_', 'lv_draw_layer', 'lv_layer_', 'lv_indev_read_cb', 'lv_obj_style_create_transition',
               'lv_image_decoder_', 'lv_img_decoder_', 'lv_tick_set_cb', 'lv_delay_set_cb', 'lv_log_register',
               'lv_freetype_init', 'lv_freetype_uninit', 'lv_tiny_ttf_', 'lv_libpng_', 'lv_lodepng_', 'lv_bmp_',
               'lv_gif_', 'lv_tjpgd_', 'lv_libjpeg_', 'lv_split_jpeg_', 'lv_ffmpeg_init', 'lv_rle_',
               'lv_lz4_', 'lv_qrcode_', 'lv_barcode_')
KEEP_PREFIX = ('lv_qrcode_create', 'lv_qrcode_update', 'lv_qrcode_set_', 'lv_barcode_create', 'lv_barcode_set_',
               'lv_barcode_update', 'lv_gif_create', 'lv_gif_set_src', 'lv_gif_restart', 'lv_gif_pause',
               'lv_gif_resume', 'lv_tiny_ttf_create_file', 'lv_tiny_ttf_destroy', 'lv_tiny_ttf_set_size')
SKIP = {'lv_init', 'lv_deinit', 'lv_is_initialized', 'lv_timer_handler_run_in_period'}
# hand-written in modules/lvgl/mcs_lvgl.c (callbacks, user data, kept-alive arrays)
OVERRIDE = {
    'lv_obj_add_event_cb', 'lv_obj_remove_event_cb', 'lv_obj_remove_event_cb_with_user_data',
    'lv_obj_remove_event_dsc', 'lv_obj_remove_event', 'lv_event_get_user_data', 'lv_obj_set_user_data',
    'lv_obj_get_user_data', 'lv_timer_create', 'lv_timer_set_cb', 'lv_timer_get_user_data',
    'lv_timer_set_user_data', 'lv_timer_delete', 'lv_timer_del', 'lv_async_call', 'lv_async_call_cancel',
    'lv_anim_set_exec_cb', 'lv_anim_set_custom_exec_cb', 'lv_anim_set_path_cb', 'lv_anim_set_ready_cb',
    'lv_anim_set_start_cb', 'lv_anim_set_deleted_cb', 'lv_anim_set_user_data', 'lv_anim_get_user_data',
    'lv_anim_set_var', 'lv_anim_start', 'lv_anim_delete', 'lv_anim_del', 'lv_event_get_target',
    'lv_event_get_current_target', 'lv_line_set_points', 'lv_line_set_points_mutable',
    'lv_btnmatrix_set_map', 'lv_buttonmatrix_set_map', 'lv_keyboard_set_map', 'lv_btnmatrix_set_ctrl_map',
    'lv_buttonmatrix_set_ctrl_map', 'lv_anim_set_get_value_cb', 'lv_anim_set_early_apply',
    'lv_dropdown_get_selected_str', 'lv_roller_get_selected_str', 'lv_subject_add_observer', 'lv_subject_add_observer_obj',
    'lv_obj_bind_flag_if_eq', 'lv_observer_get_user_data', 'lv_subject_init_string', 'lv_subject_init_pointer',
    'lv_subject_set_pointer', 'lv_subject_get_pointer', 'lv_subject_get_previous_pointer',
    'lv_chart_set_ext_y_array', 'lv_chart_set_ext_x_array', 'lv_calendar_set_highlighted_dates',
    'lv_canvas_set_buffer', 'lv_obj_set_grid_dsc_array', 'lv_style_set_grid_row_dsc_array',
    'lv_style_set_grid_column_dsc_array', 'lv_obj_set_style_grid_row_dsc_array',
    'lv_obj_set_style_grid_column_dsc_array', 'lv_style_transition_dsc_init', 'lv_log_register_print_cb',
    'lv_obj_tree_walk', 'lv_msg_subscribe', 'lv_msg_subscribe_obj', 'lv_msg_get_user_data',
}
# struct types C# may `new` (storage owned by the VM); all others only wrap LVGL's pointers
NO_CTOR = re.compile(r'^(lv_obj_t|_lv_obj_t|lv_disp_t|lv_display_t|lv_indev_t|lv_timer_t|lv_group_t|lv_event_t|'
                     r'lv_theme_t|lv_obj_class_t|lv_fs_drv_t|lv_ll_t|lv_rb_t|lv_cache.*|lv_draw_unit_t|lv_draw_task_t|'
                     r'lv_image_decoder.*|lv_img_decoder.*|lv_mutex_t|lv_thread.*|lv_disp_drv_t|lv_indev_drv_t|'
                     r'lv_event_dsc_t|lv_chart_series_t|lv_chart_cursor_t|lv_span_t|lv_fragment.*|lv_monkey_t|'
                     r'lv_anim_timeline_t|lv_observer_t|lv_global_t|lv_imgfont.*|lv_font_t|lv_obj_spec_attr_t|'
                     r'_lv_.*|lv_event_list_t|lv_display_rotation_t)$')
# freed by the GC; everything else that C# creates stays allocated until the VM closes,
# because LVGL keeps pointers to styles, points, dates, descriptors... (docs/LVGL.md)
TEMP_STRUCT = {'lv_area_t', 'lv_color32_t', 'lv_color16_t', 'lv_color_hsv_t', 'lv_mem_monitor_t',
               'lv_font_glyph_dsc_t', 'lv_color8_t', 'lv_color1_t', 'lv_sqrt_res_t', 'lv_point_precise_t_tmp'}
OBJ_TYPES = {'lv_obj_t', '_lv_obj_t'}
NULLABLE_PARAMS = {'parent', 'user_data', 'disp', 'display', 'indev', 'group', 'theme', 'base', 'par', 'th',
                   'copy', 'prev', 'scr', 'font', 'filter', 'src', 'dsc', 'cls', 'class_p', 'obj_p'}
VOIDP_RET_OBJ = {'lv_event_get_target', 'lv_event_get_current_target', 'lv_event_get_param'}
PATH_PARAMS = {'path', 'src', 'fn', 'fontName', 'font_name', 'file_name', 'filename', 'path_p', 'src_path'}
KEEP_STR = {'lv_label_set_text_static', 'lv_dropdown_set_text', 'lv_dropdown_set_options_static',
            'lv_checkbox_set_text_static', 'lv_spangroup_set_text_static', 'lv_span_set_text_static',
            'lv_imagebutton_set_src', 'lv_imgbtn_set_src', 'lv_image_set_src', 'lv_img_set_src',
            'lv_win_add_title', 'lv_list_add_text'}


def ver_num(tag):
    m = re.match(r'v(\d+)\.(\d+)(?:\.(\d+))?', tag)
    return int(m.group(1)) * 10000 + int(m.group(2)) * 100 + int(m.group(3) or 0)


class Tag:
    def __init__(self, path):
        self.db = json.load(open(path))
        self.tag = self.db['tag']
        self.ver = ver_num(self.tag)
        self.td = self.db['typedefs']
        self.structs = self.db['structs']

    def resolve(self, t, depth=0):
        """category of a type json: ('void'|'int'|'bool'|'float'|'color'|'str'|'ptr'|'voidp'|'struct'|None, extra)"""
        k = t['k']
        if depth > 20:
            return (None, 'deep')
        if k == 'name':
            n = t['name']
            if n in COLOR:
                return ('color', n)
            if n == 'void':
                return ('void', None)
            if n in ('_Bool', 'bool'):
                return ('bool', None)
            if n in INT_BASE:
                return ('int', n)
            if n in FLOAT_BASE:
                return ('float', n)
            if n in self.td:
                tt = self.td[n]['type']
                if tt['k'] == 'enum':
                    return ('int', n)
                if tt['k'] in ('struct', 'union'):
                    return ('struct', n)
                r = self.resolve(tt, depth + 1)
                if r[0] in ('struct', 'cb'):
                    return (r[0], n)
                if r[0] == 'fnptr':
                    return ('cb', n)
                return r
            return (None, 'unknown ' + n)
        if k == 'enum':
            return ('int', 'enum')
        if k in ('struct', 'union'):
            return ('struct', (k + ' ' + (t.get('name') or '')).strip())
        if k == 'ptr':
            to = t['to']
            if to['k'] == 'func':
                return ('fnptr', None)
            if to['k'] == 'name' and to['name'] == 'char':
                return ('str', None) if 'const' in to.get('quals', []) else ('charbuf', None)
            if to['k'] == 'name' and to['name'] == 'void':
                return ('voidp', None)
            if to['k'] == 'name':
                r = self.resolve(to, depth + 1)
                if r[0] == 'struct':
                    return ('ptr', to['name'])
                if r[0] is None and r[1].startswith('unknown'):
                    return ('ptr', to['name'])      # opaque (forward-declared) type
                if r[0] == 'color':
                    return ('ptr', to['name']) if to['name'] in self.structs else (None, 'color*')
                return (None, 'ptr to ' + str(r[0]))
            if to['k'] in ('struct', 'union') and to.get('name'):
                return ('ptr', to['k'] + ' ' + to['name'])
            return (None, 'ptr-ptr')
        if k == 'array':
            return (None, 'array')
        return (None, k)


def const_q(t):
    return 'const' in t.get('quals', []) or (t['k'] == 'ptr' and 'const' in t['to'].get('quals', []))


def cid(name):
    return 'LVB_C_' + re.sub(r'\W', '_', name)


def guard_expr(tags_present, all_tags):
    """version range(s) -> C expression on MCS_LV_VER"""
    idx = [i for i, t in enumerate(all_tags) if t in tags_present or getattr(t, 'tag', None) in tags_present]
    ranges = []
    for i in idx:
        if ranges and ranges[-1][1] == i - 1:
            ranges[-1][1] = i
        else:
            ranges.append([i, i])
    parts = []
    for a, b in ranges:
        lo = None if a == 0 else all_tags[a].ver
        hi = None if b == len(all_tags) - 1 else all_tags[b + 1].ver
        if lo is None and hi is None:
            return None
        if lo is None:
            parts.append('MCS_LV_VER < %d' % hi)
        elif hi is None:
            parts.append('MCS_LV_VER >= %d' % lo)
        else:
            parts.append('(MCS_LV_VER >= %d && MCS_LV_VER < %d)' % (lo, hi))
    return ' || '.join(parts) or '0'


def open_guards(out, vg, conds):
    n = 0
    if vg:
        out.append('#if %s' % vg); n += 1
    for c in conds:
        out.append('#if %s' % c); n += 1
    return n


def close_guards(out, n):
    out.extend(['#endif'] * n)


class Gen:
    def __init__(self, tags):
        self.tags = sorted(tags, key=lambda t: t.ver)
        self.classes = {}      # name -> {'tags': set(tag), 'complete': {tag: struct}}
        self.unsupported = {}  # name -> reason
        self.wrappers = []     # C code chunks
        self.fn_entries = []   # (name, wrapper, arity, kind, vg, conds)
        self.const_entries = []
        self.api = {}          # name -> doc info for IntelliSense

    def note_class(self, name, tag):
        name = name.replace('struct ', '').replace('union ', '')
        c = self.classes.setdefault(name, {'tags': set()})
        c['tags'].add(tag.tag)
        return name

    # ---- functions
    def param_code(self, tag, fname, i, p, lines):
        t = p['type']
        cat, extra = tag.resolve(t)
        pname = p.get('name') or 'arg%d' % i
        cty = None
        if cat == 'int':
            lines.append('    %s a%d = (%s)mcs_to_int(vm, argv[%d]);' % ('%s', i, '%s', i))
            return 'int'
        if cat == 'bool':
            lines.append('    bool a%d = mcs_to_bool(vm, argv[%d]);' % (i, i))
            return 'bool'
        if cat == 'float':
            lines.append('    %s a%d = (%s)lvb_arg_float(vm, argv[%d]);' % ('%s', i, '%s', i))
            return 'float'
        if cat == 'color':
            lines.append('    lv_color_t a%d = lvb_arg_color(vm, argv[%d]);' % (i, i))
            return 'color'
        if cat == 'str':
            conv = 'lvb_arg_path' if pname in PATH_PARAMS else 'lvb_arg_str'
            lines.append('    const char* a%d = %s(vm, argv[%d]);' % (i, conv, i))
            return 'str'
        if cat == 'ptr':
            cls = self.note_class(extra, tag)
            nullable = pname in NULLABLE_PARAMS or (i == 0 and fname.endswith('_create'))
            lines.append('    %%s a%d = (%%s)lvb_arg_ptr(vm, argv[%d], %s, %d);' % (i, i, cid(cls), 1 if nullable else 0))
            return 'ptr:' + cls
        if cat == 'voidp':
            conv = 'lvb_arg_src' if pname in PATH_PARAMS else 'lvb_arg_voidp'
            lines.append('    %%s a%d = (%%s)%s(vm, argv[%d]);' % (i, conv, i))
            return 'voidp'
        if cat == 'struct':
            cls = self.note_class(extra, tag)
            lines.append('    %%s a%d; lvb_arg_struct(vm, argv[%d], %s, &a%d, sizeof a%d);' % (i, i, cid(cls), i, i))
            return 'struct:' + cls
        return None

    def build_wrapper(self, tag, name, fn):
        ft = fn['type']
        if ft.get('variadic'):
            return None, 'variadic'
        params = [p for p in ft['params'] if not (p['type']['k'] == 'name' and p['type']['name'] == 'void')]
        lines, sig = [], []
        ctypes = fn['params_c'] if fn['params_c'] != ['void'] else []
        for i, p in enumerate(params):
            kind = self.param_code(tag, name, i, p, lines)
            if kind is None:
                return None, 'param %d (%s): %s' % (i, p.get('name'), tag.resolve(p['type'])[1])
            ct = ctypes[i]
            if '%s' in lines[-1]:
                lines[-1] = lines[-1].replace('%s', re.sub(r'^const\s+', '', ct) if kind.startswith('struct:') else ct)
            sig.append((kind, ct))
        rcat, rextra = tag.resolve(ft['ret'])
        rc = fn['ret_c']
        call = '%s(%s)' % (name, ', '.join('a%d' % i for i in range(len(params))))
        keep = ''
        if name in KEEP_STR and len(params) >= 2 and sig[0][0].startswith('ptr:'):
            keep = '    lvb_keep_for(vm, a0, "%s", argv[1]);\n' % name
        if rcat == 'void':
            body = '    %s;\n%s    return mcs_null();' % (call, keep)
            rk = 'void'
        elif rcat == 'int':
            body = '    mcs_value_t r_ = mcs_int((mcs_int_t)%s);\n%s    return r_;' % (call, keep); rk = 'int'
        elif rcat == 'bool':
            body = '    mcs_value_t r_ = mcs_bool(%s);\n%s    return r_;' % (call, keep); rk = 'bool'
        elif rcat == 'float':
            body = '    return lvb_ret_float((double)%s);' % call; rk = 'float'
        elif rcat == 'color':
            body = '    return lvb_ret_color(%s);' % call; rk = 'color'
        elif rcat == 'charbuf':
            body = '    return lvb_ret_str(vm, (const char*)%s);' % call; rk = 'str'
        elif rcat == 'str':
            body = '    return lvb_ret_str(vm, %s);' % call; rk = 'str'
        elif rcat == 'ptr':
            cls = self.note_class(rextra, tag)
            if cls in OBJ_TYPES and name.endswith('_create'):
                body = '    return lvb_ret_created(vm, (void*)%s);' % call
            else:
                body = '    return lvb_ret_ptr(vm, (void*)%s, %s);' % (call, cid(cls))
            rk = 'ptr:' + cls
        elif rcat == 'voidp' and name in VOIDP_RET_OBJ:
            body = '    return lvb_ret_ptr(vm, (void*)%s, LVB_C_lv_obj_t);' % call; rk = 'ptr:lv_obj_t'
            self.note_class('lv_obj_t', tag)
        elif rcat == 'struct':
            cls = self.note_class(rextra, tag)
            body = '    %s r_ = %s;\n    return lvb_ret_struct(vm, &r_, sizeof r_, %s);' % (rc, call, cid(cls))
            rk = 'struct:' + cls
        else:
            return None, 'returns %s' % rextra
        head = '    (void)vm; (void)self; (void)argc; (void)argv;'
        if lines:
            lines.append('    if (mcs_has_exception(vm)) return mcs_null();')
        code = '%s\n%s\n%s' % (head, '\n'.join(lines), body) if lines else '%s\n%s' % (head, body)
        return {'code': code, 'arity': len(params), 'sig': sig, 'ret': rk,
                'pnames': [p.get('name') or 'arg%d' % i for i, p in enumerate(params)]}, None

    def skip_name(self, name):
        if name in SKIP or name in OVERRIDE:
            return True
        if name.startswith(KEEP_PREFIX):
            return False
        return name.startswith(SKIP_PREFIX) or not name.startswith('lv_')

    def gen_functions(self):
        names = sorted({n for t in self.tags for n in t.db['functions']})
        self.wrapper_of = {}
        for name in names:
            if self.skip_name(name):
                continue
            groups = {}   # (code, conds) -> [tags]
            reasons = set()
            info = None
            for t in self.tags:
                fn = t.db['functions'].get(name)
                if not fn:
                    continue
                w, why = self.build_wrapper(t, name, fn)
                if w is None:
                    reasons.add(why)
                    continue
                key = (w['code'], tuple(fn['conds']), w['arity'])
                groups.setdefault(key, []).append(t)
                info = (w, fn)
            if not groups:
                self.unsupported[name] = sorted(reasons)[0]
                continue
            for gi, ((code, conds, arity), gtags) in enumerate(sorted(groups.items(), key=lambda kv: kv[1][0].ver)):
                wname = 'lvw_%s%s' % (name, '' if len(groups) == 1 else '_%d' % gi)
                vg = guard_expr({t.tag for t in gtags}, self.tags)
                out = []
                n = open_guards(out, vg, list(conds))
                out.append('static mcs_value_t %s(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {' % wname)
                out.append(code)
                out.append('}')
                close_guards(out, n)
                self.wrappers.append('\n'.join(out))
                self.fn_entries.append((name, wname, arity, 'f', vg, list(conds)))
                self.wrapper_of.setdefault(name, []).append((wname, arity, vg, list(conds)))
            w, fn = info
            self.api[name] = {'k': 'f', 'sig': [s[1] for s in w['sig']], 'p': w['pnames'], 'ret': fn['ret_c'],
                              'doc': (fn.get('doc') or {}).get('brief', ''), 'file': fn['file'],
                              'since': min(t.ver for g in groups.values() for t in g)}

    def gen_aliases(self):
        """v8 names on v9 (lv_api_map_v8.h) and other #define old new aliases"""
        for t in self.tags:
            for old, new in t.db['macros']['alias'].items():
                if not old.startswith('lv_') or new not in self.wrapper_of:
                    continue
                if t.db['functions'].get(old):
                    continue
                self.alias_tags = getattr(self, 'alias_tags', {})
                self.alias_tags.setdefault((old, new), set()).add(t.tag)
        for (old, new), tags in sorted(getattr(self, 'alias_tags', {}).items()):
            vg_alias = guard_expr(tags, self.tags)
            for wname, arity, vg, conds in self.wrapper_of[new]:
                g = ' && '.join('(%s)' % x for x in (vg_alias, vg) if x) or None
                self.fn_entries.append((old, wname, arity, 'f', g, conds + ['defined(%s)' % old]))
            self.api.setdefault(old, dict(self.api.get(new, {}), alias=new))

        # v9 names on v8: lv_screen_load -> lv_scr_load, wherever the v9 api map says old == new
        rev = {}
        for t in self.tags:
            for old, new in t.db['macros']['alias'].items():
                if old.startswith('lv_') and new.startswith('lv_') and old in self.wrapper_of:
                    rev.setdefault(new, set()).add(old)
        for new, olds in sorted(rev.items()):
            import difflib
            ar = {w[1] for w in self.wrapper_of.get(new, [])}
            cand = [o for o in olds if not ar or {w[1] for w in self.wrapper_of[o]} <= ar]
            if not cand:
                continue
            old = max(sorted(cand), key=lambda o: difflib.SequenceMatcher(None, new, o).ratio())
            for wname, arity, vg, conds in self.wrapper_of[old]:
                self.fn_entries.append((new, wname, arity, 'f', vg, conds))
            self.api.setdefault(new, dict(self.api.get(old, {}), alias=old))

    # ---- constants, strings, externs
    def gen_consts(self):
        ents = {}
        for t in self.tags:
            for n, e in t.db['enums'].items():
                if n.startswith('_') or not n.startswith('LV_'):
                    continue
                ents.setdefault(n, {}).setdefault(('enum', tuple(e['conds'])), set()).add(t.tag)
            for n in t.db['macros']['int']:
                if n.startswith('LV_USE_') or n.startswith('LV_CONF') or n.startswith('LV_ATTRIBUTE') or n.startswith('LV_BUILD'):
                    continue
                ents.setdefault(n, {}).setdefault(('macro', ()), set()).add(t.tag)
            for n, target in t.db['macros']['alias'].items():
                if n.startswith('LV_') and not n.startswith('LV_USE_') and (target in t.db['enums'] or target in t.db['macros']['int']):
                    ents.setdefault(n, {}).setdefault(('macro', ()), set()).add(t.tag)
        self.const_entries = []
        for n in sorted(ents):
            for (kind, conds), tags in ents[n].items():
                vg = guard_expr(tags, self.tags)
                c = list(conds) + (['defined(%s)' % n] if kind == 'macro' else [])
                self.const_entries.append((n, vg, c))
            self.api.setdefault(n, {'k': 'k'})
        strs = {}
        for t in self.tags:
            for n in t.db['macros']['str']:
                if re.search(r'_INCLUDE$|_PATH$|_HEADER$|^LV_CONF', n):
                    continue
                strs.setdefault(n, set()).add(t.tag)
        for n, tags in sorted(strs.items()):
            fname = 'lvg_' + n
            self.wrappers.append('#ifdef %s\nstatic mcs_value_t %s(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {\n'
                                 '    (void)self; (void)argc; (void)argv; return mcs_string(vm, %s);\n}\n#endif' % (n, fname, n))
            self.fn_entries.append((n, fname, 0, 'g', None, ['defined(%s)' % n]))
            self.api[n] = {'k': 's'}
        exts = {}
        for t in self.tags:
            for n, e in t.db['externs'].items():
                if not n.startswith('lv_') or e['type']['k'] != 'name':
                    continue
                cat, extra = t.resolve(e['type'])
                if cat != 'struct':
                    continue
                exts.setdefault((n, extra, tuple(e['conds'])), set()).add(t)
        for (n, cls, conds), tags in sorted(exts.items(), key=lambda kv: kv[0][0]):
            for t in tags:
                self.note_class(cls, t)
            vg = guard_expr({t.tag for t in tags}, self.tags)
            fname = 'lvg_%s_%d' % (n, len(self.wrappers))
            out = []
            k = open_guards(out, vg, list(conds))
            out.append('static mcs_value_t %s(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {\n'
                       '    (void)self; (void)argc; (void)argv; return lvb_ret_ptr(vm, (void*)&%s, %s);\n}' % (fname, n, cid(cls)))
            close_guards(out, k)
            self.wrappers.append('\n'.join(out))
            self.fn_entries.append((n, fname, 0, 'g', vg, list(conds)))
            self.api[n] = {'k': 'v', 'type': cls}

    # ---- struct classes
    def gen_classes(self, out):
        names = sorted(self.classes)
        out.append('/* ---- classes: one C# class per LVGL C type, named exactly like it */')
        out.append('enum {')
        for n in names:
            out.append('    %s,' % cid(n))
        out.append('    LVB_C__COUNT\n};')
        out.append('static const char* const lvb_class_names[LVB_C__COUNT] = {')
        out.extend('    "%s",' % n for n in names)
        out.append('};')
        # canonical ids: typedef aliases of the same struct share one (lv_disp_t / lv_display_t on v9)
        canon_variants = {}
        for n in names:
            for t in self.tags:
                if n not in t.td:
                    continue
                tt = t.td[n]['type']
                if tt['k'] == 'name' and tt['name'] in self.classes:
                    canon_variants.setdefault(n, {}).setdefault(tt['name'], set()).add(t.tag)
                elif tt['k'] in ('struct', 'union') and tt.get('name') and tt['name'] != n and tt['name'] in self.classes:
                    # typedef struct _lv_obj_t lv_obj_t: both spellings are the same C# class
                    canon_variants.setdefault(tt['name'], {}).setdefault(n, set()).add(t.tag)
        out.append('static uint16_t lvb_canon(uint16_t c) {\n    switch (c) {')
        for n, targets in sorted(canon_variants.items()):
            for tgt, tags in targets.items():
                vg = guard_expr(tags, self.tags)
                if vg:
                    out.append('#if %s' % vg)
                out.append('    case %s: return lvb_canon(%s);' % (cid(n), cid(tgt)))
                if vg:
                    out.append('#endif')
        out.append('    default: return c;\n    }\n}')
        # field accessors, ctors
        defs = []
        for n in names:
            members = []
            complete = {}
            for t in self.tags:
                s = t.structs.get(n)
                if s is None and n in t.td and t.td[n]['type']['k'] in ('struct', 'union') and t.td[n]['type'].get('name'):
                    tagname = t.td[n]['type']['k'] + ' ' + t.td[n]['type']['name']
                    s = t.structs.get(tagname)
                if s is None and n.startswith('_') and ('struct ' + n) in t.structs:
                    s = t.structs['struct ' + n]
                if s:
                    complete[t.tag] = (t, s)
            ctor = 'NULL'
            sconds = []
            for tg in sorted(complete, reverse=True):
                sconds = complete[tg][1].get('conds') or []
                break
            ctor_tags = {tg for tg in complete} if not NO_CTOR.match(n) and not n.startswith('struct ') else set()
            if ctor_tags:
                vg = guard_expr(ctor_tags, self.tags)
                retain = 0 if n in TEMP_STRUCT else 1
                ng = open_guards(out, vg, sconds)
                out.append('static void lvc_new_%s(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {\n'
                           '    (void)argc; (void)argv; lvb_ctor(vm, self, sizeof(%s), %s, %d);\n}' % (n, n, cid(n), retain))
                out.append('#define LVC_NEW_%s lvc_new_%s' % (n, n))
                close_guards(out, ng)
                out.append('#ifndef LVC_NEW_%s\n#define LVC_NEW_%s NULL\n#endif' % (n, n))
                ctor = 'LVC_NEW_%s' % n
                # fields
                fields = {}
                for tg, (t, s) in complete.items():
                    if NO_CTOR.match(n):
                        continue
                    for f in s['fields']:
                        cat, extra = t.resolve(f['type'])
                        if cat not in ('int', 'bool', 'float', 'color'):
                            continue
                        fields.setdefault((f['name'], cat, f['c']), set()).add(tg)
                for (fname, cat, fc), ftags in sorted(fields.items()):
                    vg = guard_expr(ftags, self.tags)
                    ng = open_guards(out, vg, sconds)
                    get = {'int': 'mcs_int((mcs_int_t)p_->%s)', 'bool': 'mcs_bool(p_->%s)',
                           'float': 'lvb_ret_float((double)p_->%s)', 'color': 'lvb_ret_color(p_->%s)'}[cat] % fname
                    sv = {'int': '(%s)mcs_to_int(vm, argv[0])', 'bool': 'mcs_to_bool(vm, argv[0])',
                          'float': '(%s)lvb_arg_float(vm, argv[0])', 'color': 'lvb_arg_color(vm, argv[0])'}[cat]
                    if '%s' in sv:
                        sv = sv % re.sub(r'\s*:\s*\d+$', '', fc)
                    fid = '%s__%s_%d' % (n, fname, len(members))
                    out.append('static mcs_value_t lvf_get_%s(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {\n'
                               '    (void)argc; (void)argv; %s* p_ = (%s*)lvb_self(vm, self, %s); if (!p_) return mcs_null(); return %s;\n}'
                               % (fid, n, n, cid(n), get))
                    out.append('static mcs_value_t lvf_set_%s(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {\n'
                               '    (void)argc; %s* p_ = (%s*)lvb_self(vm, self, %s); if (!p_) return mcs_null(); p_->%s = %s; return mcs_null();\n}'
                               % (fid, n, n, cid(n), fname, sv))
                    close_guards(out, ng)
                    members.append((fname, fid, vg, sconds))
            out.append('static const mcs_reg_t lvm_%s[] = {' % re.sub(r'\W', '_', n))
            for fname, fid, vg, sc in sorted(members, key=lambda m: m[:2]):
                ng = open_guards(out, vg, sc)
                out.append('    MCS_GET("%s", lvf_get_%s), MCS_SET("%s", lvf_set_%s),' % (fname, fid, fname, fid))
                close_guards(out, ng)
            out.append('    LVB_MEMBERS_COMMON\n};')
            defs.append('    { "%s", sizeof(lvb_ud_t), %s, lvb_finalize, lvm_%s, lvb_statics },' % (n, ctor, re.sub(r'\W', '_', n)))
        out.append('static const mcs_class_def_t lvb_class_defs[LVB_C__COUNT] = {')
        out.extend(defs)
        out.append('};')

    def write(self, path):
        self.gen_functions()
        self.gen_aliases()
        self.gen_consts()
        out = ['/* Generated by tools/lvgl_gen/gen.py from LVGL %s - do not edit.' % ', '.join(t.tag for t in self.tags),
               ' * %d functions, %d constants, %d classes; %d APIs are not bound (see docs/LVGL_API.md). */'
               % (len({e[0] for e in self.fn_entries if e[3] == 'f'}), len({e[0] for e in self.const_entries}),
                  len(self.classes), len(self.unsupported))]
        self.gen_classes(out)
        out.append('/* ---- function wrappers */')
        out.extend(self.wrappers)
        # path callbacks: native wrapper -> C function, for lv_anim_set_path_cb(a, lv_anim_path_ease_out)
        out.append('static const struct { mcs_native_fn w; lv_anim_path_cb_t c; } lvb_anim_paths[] = {')
        for name, wl in sorted(self.wrapper_of.items()):
            if name.startswith('lv_anim_path_'):
                for wname, arity, vg, conds in wl:
                    n = open_guards(out, vg, conds)
                    out.append('    { %s, %s },' % (wname, name))
                    close_guards(out, n)
        out.append('    { NULL, NULL }\n};')
        # ROM tables (sorted; #if only removes entries)
        out.append('static const mcs_reg_t lvb_rom_fns[] = {')
        fe = sorted(self.fn_entries, key=lambda e: e[0].encode())
        for name, wname, arity, kind, vg, conds in fe:
            n = open_guards(out, vg, conds)
            out.append('    { "%s", %s, %d, \'%s\' },' % (name, wname, arity, kind))
            close_guards(out, n)
        out.append('    MCS_REG_END\n};')
        out.append('static const mcs_const_t lvb_rom_consts[] = {')
        for name, vg, conds in sorted(self.const_entries, key=lambda e: e[0].encode()):
            n = open_guards(out, vg, conds)
            out.append('    { "%s", (int32_t)(%s) },' % (name, name))
            close_guards(out, n)
        out.append('    MCS_CONST_END\n};')
        out.append('static const mcs_class_def_t* const lvb_rom_classes[] = {')
        for n in sorted(self.classes, key=lambda s: s.encode()):
            out.append('    &lvb_class_defs[%s],' % cid(n))
        out.append('};')
        with open(path, 'w') as f:
            f.write('\n'.join(out) + '\n')
        return self


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dbdir')
    ap.add_argument('--out', default=os.path.join(REPO, 'modules', 'lvgl', 'mcs_lvgl_api.inc'))
    ap.add_argument('--api-json', default=os.path.join(HERE, 'lvgl_api.json'))
    a = ap.parse_args()
    tags = [Tag(p) for p in glob.glob(os.path.join(a.dbdir, 'v*.json'))]
    g = Gen(tags).write(a.out)
    with open(a.api_json, 'w') as f:
        json.dump({'tags': [t.tag for t in g.tags], 'api': g.api, 'unsupported': g.unsupported,
                   'classes': sorted(g.classes)}, f, indent=0, sort_keys=True)
    print('wrote', a.out, len(g.fn_entries), 'fn entries', len(g.const_entries), 'consts', len(g.classes), 'classes',
          len(g.unsupported), 'unsupported')


if __name__ == '__main__':
    main()
