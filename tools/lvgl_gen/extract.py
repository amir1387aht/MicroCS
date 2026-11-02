"""LVGL header extraction for tools/lvgl.py.

For one LVGL source tree this module
  * writes an "everything enabled" lv_conf.h (widgets, themes, layouts,
    image/font decoders, extra components; no drivers / GPUs / OS),
  * preprocesses lvgl.h against a tiny fake libc and parses it with pycparser,
  * records every public function (prototypes and static inline functions),
    enum constant, extern object, typedef and complete struct/union, together
    with the #if conditions of the header lines that declare them,
  * classifies the object-like LV_* macros (integer / string constants and
    aliases of other names) with the real compiler.
The result is a JSON-friendly dict; see tools/lvgl.py for the code generator.
"""
import json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# --------------------------------------------------------------------- config
ENABLE_EXACT = {
    # layouts, themes, extra components
    'LV_USE_FLEX', 'LV_USE_GRID', 'LV_USE_SNAPSHOT', 'LV_USE_MONKEY', 'LV_USE_GRIDNAV',
    'LV_USE_FRAGMENT', 'LV_USE_IMGFONT', 'LV_USE_MSG', 'LV_USE_OBSERVER', 'LV_USE_IME_PINYIN',
    'LV_USE_FILE_EXPLORER', 'LV_USE_SYSMON', 'LV_USE_PERF_MONITOR', 'LV_USE_MEM_MONITOR',
    'LV_USE_LOG', 'LV_USE_BIDI', 'LV_USE_ARABIC_PERSIAN_CHARS', 'LV_USE_FONT_COMPRESSED',
    'LV_USE_FONT_PLACEHOLDER', 'LV_USE_FONT_MANAGER', 'LV_USE_FLOAT', 'LV_USE_MATRIX',
    'LV_USE_VECTOR_GRAPHIC', 'LV_USE_THORVG_INTERNAL', 'LV_USE_OBJ_ID', 'LV_USE_OBJ_ID_BUILTIN',
    'LV_USE_OBJ_PROPERTY', 'LV_USE_OBJ_PROPERTY_NAME', 'LV_USE_OBJ_NAME', 'LV_USE_TRANSLATION',
    'LV_USE_COLOR_FILTER', 'LV_USE_GESTURE_RECOGNITION', 'LV_USE_XML', 'LV_USE_LARGE_COORD',
    # image / font decoders and other libraries
    'LV_USE_PNG', 'LV_USE_LODEPNG', 'LV_USE_LIBPNG', 'LV_USE_BMP', 'LV_USE_SJPG', 'LV_USE_TJPGD',
    'LV_USE_LIBJPEG_TURBO', 'LV_USE_GIF', 'LV_USE_QRCODE', 'LV_USE_BARCODE', 'LV_USE_FREETYPE',
    'LV_USE_TINY_TTF', 'LV_USE_RLOTTIE', 'LV_USE_LOTTIE', 'LV_USE_FFMPEG', 'LV_USE_RLE',
    'LV_USE_LZ4', 'LV_USE_LZ4_INTERNAL', 'LV_USE_SVG', 'LV_USE_LIBWEBP',
    'LV_USE_CALENDAR_HEADER_ARROW', 'LV_USE_CALENDAR_HEADER_DROPDOWN', 'LV_USE_CALENDAR_CHINESE',
    'LV_IMG_CACHE_DEF_SIZE_DUMMY',
}
# LV_USE_<widget>: every widget we can find a header for is enabled
NEVER = re.compile(r'LV_USE_(OS|STDLIB|DRAW_|GPU|PXP|VG_LITE|NEMA|PPA|SIFLI|G2D|DAVE|ROTATE|'
                   r'NATIVE_HELIUM|DEMO|FS_|PROFILER|TEST|ASSERT|CHECK|PRIVATE_API|SDL|X11|WAYLAND|'
                   r'WINDOWS|LINUX|EVDEV|LIBINPUT|NUTTX|OPENGLES|GLFW|QNX|UEFI|ST7|ILI|NV3|GENERIC_MIPI|'
                   r'NXP|RENESAS|ST_LTDC|TFT_ESPI|LOVYAN|FT81|NANOVG|REFR_DEBUG|LAYER_DEBUG|'
                   r'PARALLEL|GSTREAMER|GLTF|3DTEXTURE|FREERTOS|THORVG$|EXT_DATA|MEM_MONITOR_POS|'
                   r'PERF_MONITOR_POS|PERF_MONITOR_LOG|SVG|LOTTIE|RLOTTIE|VECTOR_GRAPHIC|USER_DATA)')


def widget_names(root):
    names = set()
    for base in ('src/widgets', 'src/extra/widgets'):
        d = os.path.join(root, base)
        if not os.path.isdir(d):
            continue
        for dp, dn, fn in os.walk(d):
            for f in fn:
                if f.startswith('lv_') and f.endswith('.h'):
                    names.add('LV_USE_' + f[3:-2].upper())
    return names


def make_conf(root, out_dir):
    src = open(os.path.join(root, 'lv_conf_template.h'), encoding='utf-8').read()
    src = re.sub(r'^#if 0( */\*.*[Ss]et (it|this) to)', r'#if 1\1', src, count=1, flags=re.M)
    widgets = widget_names(root)
    enabled = []

    def repl(m):
        name, val = m.group(2), m.group(3)
        if val != '0':
            return m.group(0)
        on = False
        if name.startswith('LV_USE_') and not NEVER.match(name):
            on = name in ENABLE_EXACT or name in widgets or name.startswith('LV_USE_THEME_')
        if name.startswith('LV_FONT_') and name not in ('LV_FONT_FMT_TXT_LARGE', 'LV_FONT_USE_CUSTOM_INCLUDE',
                                                        'LV_FONT_CUSTOM'):
            on = True
        if not on:
            return m.group(0)
        enabled.append(name)
        return f'{m.group(1)}{name} 1'

    src = re.sub(r'^(\s*#\s*define\s+)(LV_[A-Z0-9_]+)\s+(\S+)', repl, src, flags=re.M)
    # v8: user data in every object / anim / timer is needed by the binding
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'lv_conf.h'), 'w') as f:
        f.write(src)
    return enabled


# --------------------------------------------------------------- fake libc
FAKE = {
    'stdint.h': '''typedef signed char int8_t; typedef unsigned char uint8_t; typedef short int16_t;
typedef unsigned short uint16_t; typedef int int32_t; typedef unsigned int uint32_t;
typedef long long int64_t; typedef unsigned long long uint64_t; typedef long intptr_t;
typedef unsigned long uintptr_t; typedef long long intmax_t; typedef unsigned long long uintmax_t;
typedef int8_t int_least8_t; typedef uint8_t uint_least8_t; typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t; typedef int32_t int_least32_t; typedef uint32_t uint_least32_t;
typedef int64_t int_least64_t; typedef uint64_t uint_least64_t; typedef int int_fast8_t;
typedef unsigned uint_fast8_t; typedef int int_fast16_t; typedef unsigned uint_fast16_t;
typedef int int_fast32_t; typedef unsigned uint_fast32_t; typedef int64_t int_fast64_t; typedef uint64_t uint_fast64_t;
#define INT8_MAX 127
#define INT8_MIN (-128)
#define UINT8_MAX 255
#define INT16_MAX 32767
#define INT16_MIN (-32768)
#define UINT16_MAX 65535
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647-1)
#define UINT32_MAX 4294967295U
#define INT64_MAX 9223372036854775807LL
#define INT64_MIN (-9223372036854775807LL-1)
#define UINT64_MAX 18446744073709551615ULL
#define SIZE_MAX 18446744073709551615UL
#define UINTPTR_MAX 18446744073709551615UL
#define INTPTR_MAX 9223372036854775807L
''',
    'stddef.h': 'typedef unsigned long size_t; typedef long ptrdiff_t; typedef int wchar_t;\n#define NULL ((void*)0)\n#define offsetof(t, m) ((size_t)&((t*)0)->m)\n',
    'stdbool.h': '#define bool _Bool\n#define true 1\n#define false 0\n',
    'stdarg.h': 'typedef int va_list;\n#define va_start(a, b)\n#define va_end(a)\n#define va_arg(a, t) (*(t*)0)\n#define va_copy(a, b)\n',
    'limits.h': '#define CHAR_BIT 8\n#define INT_MAX 2147483647\n#define INT_MIN (-INT_MAX-1)\n#define UINT_MAX 4294967295U\n#define LONG_MAX 9223372036854775807L\n#define SHRT_MAX 32767\n#define UCHAR_MAX 255\n#define LLONG_MAX 9223372036854775807LL\n',
    'inttypes.h': '#include <stdint.h>\n#define PRId32 "d"\n#define PRIu32 "u"\n#define PRIx32 "x"\n#define PRIX32 "X"\n#define PRIi32 "i"\n#define PRId64 "lld"\n#define PRIu64 "llu"\n#define PRIx64 "llx"\n#define PRIuPTR "lu"\n#define PRIxPTR "lx"\n#define PRId16 "d"\n#define PRIu16 "u"\n#define PRId8 "d"\n#define PRIu8 "u"\n',
    'string.h': '#include <stddef.h>\n',
    'stdlib.h': '#include <stddef.h>\n',
    'stdio.h': '#include <stddef.h>\ntypedef struct _FILE FILE;\n',
    'math.h': '', 'float.h': '#define FLT_MAX 3.40282347e+38F\n#define FLT_MIN 1.17549435e-38F\n#define DBL_MAX 1.79e308\n',
    'time.h': 'typedef long time_t;\n',
    'assert.h': '#define assert(x)\n',
    'ft2build.h': '#define FT_FREETYPE_H <ft2build.h>\n#define FT_CACHE_H <ft2build.h>\n#define FT_SIZES_H <ft2build.h>\n#define FT_IMAGE_H <ft2build.h>\n#define FT_OUTLINE_H <ft2build.h>\n#define FT_STROKER_H <ft2build.h>\n#define FT_GLYPH_H <ft2build.h>\n#define FT_BITMAP_H <ft2build.h>\n#define FT_SYNTHESIS_H <ft2build.h>\n#define FT_TRUETYPE_TABLES_H <ft2build.h>\n#ifndef MCS_FAKE_FT\n#define MCS_FAKE_FT\ntypedef struct FT_FaceRec_* FT_Face; typedef void* FT_Library; typedef void* FTC_Manager; typedef void* FTC_CMapCache; typedef void* FTC_SBitCache; typedef void* FTC_ImageCache; typedef void* FT_Size; typedef long FT_Long; typedef unsigned long FT_ULong; typedef int FT_Error; typedef void* FTC_FaceID; typedef long FT_Pos; typedef int FT_Int; typedef unsigned FT_UInt;\n#endif\n',
}


def write_fake(dir_):
    os.makedirs(dir_, exist_ok=True)
    for k, v in FAKE.items():
        p = os.path.join(dir_, k)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w') as f:
            f.write(v)


CPP_DEFS = ['-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE', '-DPYCPARSER', '-D__attribute__(x)=',
            '-D__extension__=', '-D__restrict=', '-D__inline=inline', '-D__asm__(x)=', '-D_Static_assert(a,b)=',
            '-D__declspec(x)=', '-D__fp16=short', '-D_Float16=short']


def run_cpp(root, conf_dir, fake_dir, extra=()):
    """preprocess lvgl.h; missing third-party headers become empty stubs"""
    stub = os.path.join(conf_dir, 'stubs')
    os.makedirs(stub, exist_ok=True)
    for _ in range(60):
        cmd = ['gcc', '-E', '-std=gnu99', '-nostdinc', '-I', fake_dir, '-I', stub, '-I', conf_dir,
               '-I', root, '-I', os.path.dirname(root)] + CPP_DEFS + list(extra) + [os.path.join(root, 'lvgl.h')]
        p = subprocess.run(cmd, capture_output=True, text=True)
        missing = re.findall(r'fatal error: ([^:]+): No such file', p.stderr)
        if p.returncode == 0:
            return p.stdout
        if not missing:
            raise RuntimeError('cpp failed for %s:\n%s' % (root, p.stderr[-4000:]))
        for m in missing:
            path = os.path.join(stub, m.strip())
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, 'a').close()
    raise RuntimeError('too many missing headers')


# ---------------------------------------------------- conditional scanning
_cond_cache = {}


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def norm_cond(c):
    c = re.sub(r'\s+', ' ', c).strip()
    return c


def file_conditions(path):
    """list indexed by 1-based line number: tuple of active conditions"""
    if path in _cond_cache:
        return _cond_cache[path]
    try:
        raw = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        _cond_cache[path] = None
        return None
    lines = strip_comments(raw).split('\n')
    res = [()] * (len(lines) + 2)
    stack = []   # frames: dict(cond=current, prev=[...], ignore=bool)
    first_directive = True
    guard_name = None
    i = 0
    while i < len(lines):
        ln = i + 1
        line = lines[i]
        full = line
        while full.endswith('\\') and i + 1 < len(lines):
            i += 1
            full = full[:-1] + ' ' + lines[i]
        active = tuple(f['cur'] for f in stack if not f['ignore'] and f['cur'])
        for k in range(ln, i + 2):
            if k < len(res):
                res[k] = active
        m = re.match(r'\s*#\s*(\w+)\s*(.*)$', full)
        if m:
            d, rest = m.group(1), norm_cond(m.group(2))
            if d in ('if', 'ifdef', 'ifndef'):
                if d == 'ifdef':
                    cond = 'defined(%s)' % rest.split()[0]
                elif d == 'ifndef':
                    cond = '!defined(%s)' % rest.split()[0]
                else:
                    cond = rest
                ignore = False
                if first_directive and d == 'ifndef':
                    guard_name = rest.split()[0]
                    ignore = True
                if '__cplusplus' in cond or 'LV_CONF_H' in cond:
                    ignore = True
                stack.append({'cur': '(%s)' % cond, 'prev': ['(%s)' % cond], 'ignore': ignore})
            elif d == 'elif' and stack:
                f = stack[-1]
                f['cur'] = '(%s && (%s))' % (' && '.join('!%s' % p for p in f['prev']), rest)
                f['prev'].append('(%s)' % rest)
            elif d == 'else' and stack:
                f = stack[-1]
                f['cur'] = '(%s)' % ' && '.join('!%s' % p for p in f['prev'])
            elif d == 'endif' and stack:
                stack.pop()
            if d not in ('ifndef',) or not first_directive:
                pass
            first_directive = False if d not in ('define',) or guard_name is None else first_directive
        i += 1
    _cond_cache[path] = res
    return res


# --------------------------------------------------------------- includes
def include_sites(pp_text):
    """map file -> (parent_file, parent_line) of its first inclusion, from linemarkers"""
    sites = {}
    stack = []
    cur_file, cur_line = None, 0
    for line in pp_text.split('\n'):
        if line.startswith('# '):
            m = re.match(r'# (\d+) "([^"]*)"((?: \d)*)', line)
            if m:
                num, f, flags = int(m.group(1)), m.group(2), m.group(3).split()
                if '1' in flags:
                    if f not in sites and cur_file is not None:
                        sites[f] = (cur_file, cur_line - 1)
                    stack.append(cur_file)
                elif '2' in flags and stack:
                    stack.pop()
                cur_file, cur_line = f, num
                continue
        cur_line += 1
    return sites


def chain_conditions(file, line, sites, depth=0):
    conds = []
    fc = file_conditions(file)
    if fc is not None and 0 < line < len(fc):
        conds.extend(fc[line])
    # Only lvgl.h's own #if blocks guard the headers it includes. Conditions of
    # other parents are artifacts of the first include path (lv_label.h is first
    # reached through lv_bar.h etc.), so they are not part of the API's guard.
    if depth < 40 and file in sites and os.path.basename(sites[file][0]) == 'lvgl.h':
        pf, pl = sites[file]
        conds = chain_conditions(pf, pl, sites, depth + 1) + conds
    out = []
    for c in conds:
        if c not in out:
            out.append(c)
    return out


# --------------------------------------------------------------- AST walk
def type_to_json(node, gen):
    from pycparser import c_ast
    if isinstance(node, c_ast.TypeDecl):
        t = type_to_json(node.type, gen)
        if node.quals:
            t = dict(t, quals=sorted(set(t.get('quals', []) + node.quals)))
        return t
    if isinstance(node, c_ast.PtrDecl):
        return {'k': 'ptr', 'to': type_to_json(node.type, gen), 'quals': node.quals}
    if isinstance(node, c_ast.ArrayDecl):
        dim = gen.visit(node.dim) if node.dim is not None else None
        return {'k': 'array', 'of': type_to_json(node.type, gen), 'dim': dim}
    if isinstance(node, c_ast.FuncDecl):
        params = []
        variadic = False
        if node.args:
            for p in node.args.params:
                if isinstance(p, c_ast.EllipsisParam):
                    variadic = True
                    continue
                pt = type_to_json(p.type, gen)
                if pt.get('k') == 'name' and pt['name'] == 'void' and not getattr(p, 'name', None):
                    continue
                params.append({'name': getattr(p, 'name', None), 'type': pt})
        return {'k': 'func', 'ret': type_to_json(node.type, gen), 'params': params, 'variadic': variadic}
    if isinstance(node, c_ast.IdentifierType):
        return {'k': 'name', 'name': ' '.join(node.names)}
    if isinstance(node, c_ast.Struct):
        return {'k': 'struct', 'name': node.name}
    if isinstance(node, c_ast.Union):
        return {'k': 'union', 'name': node.name}
    if isinstance(node, c_ast.Enum):
        return {'k': 'enum', 'name': node.name}
    if isinstance(node, c_ast.Typename):
        return type_to_json(node.type, gen)
    return {'k': 'unknown', 'what': type(node).__name__}


def c_spelling(node, gen, name=''):
    """C declaration text of a type (as spelled in the headers)"""
    from pycparser import c_ast
    import copy
    n = copy.deepcopy(node)
    # drop struct bodies
    for sub in iter_nodes(n):
        if isinstance(sub, (c_ast.Struct, c_ast.Union, c_ast.Enum)):
            if isinstance(sub, c_ast.Enum):
                sub.values = None
            else:
                sub.decls = None
    _set_declname(n, name)
    return gen.visit(n)


def _set_declname(n, name):
    from pycparser import c_ast
    while not isinstance(n, c_ast.TypeDecl):
        if isinstance(n, c_ast.FuncDecl):
            n = n.type
        elif hasattr(n, 'type'):
            n = n.type
        else:
            return
    n.declname = name


def iter_nodes(n):
    yield n
    for _, ch in n.children():
        yield from iter_nodes(ch)


def extract(root, work, tag):
    """returns the per-tag database"""
    from pycparser import c_parser, c_ast, c_generator
    conf_dir = os.path.join(work, 'conf', tag)
    fake_dir = os.path.join(work, 'fake')
    write_fake(fake_dir)
    enabled = make_conf(root, conf_dir)
    pp = run_cpp(root, conf_dir, fake_dir)
    sites = include_sites(pp)
    # pycparser does not understand a few GNU-isms that survive preprocessing
    text = pp
    parser = c_parser.CParser()
    ast = parser.parse(text, filename='lvgl.h')
    gen = c_generator.CGenerator()
    db = {'tag': tag, 'functions': {}, 'enums': {}, 'externs': {}, 'typedefs': {}, 'structs': {},
          'enabled': enabled}
    rootabs = os.path.abspath(root)

    def rel(f):
        f = os.path.abspath(f)
        return os.path.relpath(f, rootabs) if f.startswith(rootabs) else None

    def conds_of(coord):
        if coord is None:
            return []
        return chain_conditions(coord.file, coord.line, sites)

    def record_struct(su, tdname=None):
        if su.decls is None:
            return
        key = ('struct ' if isinstance(su, c_ast.Struct) else 'union ') + (su.name or '')
        fields = []
        for d in su.decls:
            if d.name is None:
                # anonymous member struct/union: flatten
                t = d.type
                inner = t.type if isinstance(t, c_ast.TypeDecl) else t
                if isinstance(inner, (c_ast.Struct, c_ast.Union)) and inner.decls:
                    for dd in inner.decls:
                        if dd.name:
                            fields.append({'name': dd.name, 'type': type_to_json(dd.type, gen),
                                           'c': c_spelling(dd.type, gen), 'bits': gen.visit(dd.bitsize) if dd.bitsize else None,
                                           'anon': True})
                continue
            fields.append({'name': d.name, 'type': type_to_json(d.type, gen), 'c': c_spelling(d.type, gen),
                           'bits': gen.visit(d.bitsize) if d.bitsize else None})
        info = {'kind': 'struct' if isinstance(su, c_ast.Struct) else 'union', 'tag': su.name,
                'typedef': tdname, 'fields': fields, 'file': rel(su.coord.file) if su.coord else None,
                'conds': conds_of(su.coord)}
        db['structs'][tdname or key] = info

    def scan_nested(t, tdname=None):
        for sub in iter_nodes(t):
            if isinstance(sub, c_ast.Enum) and sub.values:
                for e in sub.values.enumerators:
                    if e.name not in db['enums']:
                        db['enums'][e.name] = {'enum': sub.name, 'typedef': tdname, 'file': rel(e.coord.file) if e.coord else None,
                                               'conds': conds_of(e.coord), 'value': gen.visit(e.value) if e.value else None}
            if isinstance(sub, (c_ast.Struct, c_ast.Union)) and sub.decls is not None:
                record_struct(sub, tdname if sub is _base(t) else None)

    for ext in ast.ext:
        coord = ext.coord
        f = rel(coord.file) if coord else None
        if isinstance(ext, c_ast.FuncDef):
            decl = ext.decl
            if f is None:
                continue
            add_func(db, decl, gen, f, conds_of(decl.coord), inline=True)
            continue
        if not isinstance(ext, c_ast.Decl) and not isinstance(ext, c_ast.Typedef):
            continue
        if f is None:
            continue
        if isinstance(ext, c_ast.Typedef):
            db['typedefs'][ext.name] = {'type': type_to_json(ext.type, gen), 'c': c_spelling(ext.type, gen),
                                        'file': f, 'conds': conds_of(coord)}
            scan_nested(ext.type, ext.name)
            continue
        # Decl
        if isinstance(ext.type, c_ast.FuncDecl):
            add_func(db, ext, gen, f, conds_of(coord), inline=False)
            continue
        if ext.name and 'extern' in (ext.storage or []):
            db['externs'][ext.name] = {'type': type_to_json(ext.type, gen), 'c': c_spelling(ext.type, gen),
                                       'file': f, 'conds': conds_of(coord)}
            continue
        scan_nested(ext.type)
    # docs
    for name, fn in db['functions'].items():
        fn['doc'] = doc_comment(os.path.join(root, fn['file']), fn['line'])
    db['macros'] = classify_macros(root, conf_dir, set(db['functions']) | set(db['enums']) | set(db['externs']))
    db['version'] = read_version(root)
    return db


def _base(t):
    from pycparser import c_ast
    while isinstance(t, (c_ast.TypeDecl,)):
        t = t.type
    return t


def add_func(db, decl, gen, f, conds, inline):
    name = decl.name
    if not name or not name.startswith('lv_'):
        return
    ft = decl.type
    db['functions'][name] = {
        'type': type_to_json(ft, gen),
        'ret_c': c_spelling(ft.type, gen),
        'params_c': [c_spelling(p.type, gen) if hasattr(p, 'type') else '...' for p in (ft.args.params if ft.args else [])],
        'file': f, 'line': decl.coord.line, 'conds': conds, 'inline': inline,
        'storage': decl.storage, 'funcspec': decl.funcspec,
    }


def doc_comment(path, line):
    try:
        lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    except OSError:
        return None
    i = line - 2
    # skip attribute lines / blank
    while i >= 0 and lines[i].strip() == '':
        i -= 1
    if i < 0 or not lines[i].strip().endswith('*/'):
        return None
    end = i
    while i >= 0 and '/*' not in lines[i]:
        i -= 1
    if i < 0:
        return None
    body = '\n'.join(lines[i:end + 1])
    body = re.sub(r'^\s*/\*\*?<?', '', body)
    body = re.sub(r'\*/\s*$', '', body)
    body = '\n'.join(re.sub(r'^\s*\* ?', '', l) for l in body.split('\n'))
    brief, params, ret = [], {}, None
    cur = brief
    for l in body.split('\n'):
        s = l.strip()
        m = re.match(r'[@\\]param(?:\[[a-z,]+\])?\s+(\w+)\s*(.*)', s)
        if m:
            params[m.group(1)] = [m.group(2)]
            cur = params[m.group(1)]
            continue
        m = re.match(r'[@\\]returns?\s*(.*)', s)
        if m:
            ret = [m.group(1)]
            cur = ret
            continue
        if s.startswith('@') or s.startswith('\\'):
            cur = []
            continue
        if s:
            cur.append(s)
    j = lambda x: re.sub(r'\s+', ' ', ' '.join(x)).strip()
    return {'brief': j(brief), 'params': {k: j(v) for k, v in params.items()}, 'ret': j(ret) if ret else ''}


def read_version(root):
    txt = open(os.path.join(root, 'lv_version.h')).read() if os.path.exists(os.path.join(root, 'lv_version.h')) \
        else open(os.path.join(root, 'lvgl.h')).read()
    g = lambda k: int(re.search(r'#define\s+%s\s+(\d+)' % k, txt).group(1))
    return [g('LVGL_VERSION_MAJOR'), g('LVGL_VERSION_MINOR'), g('LVGL_VERSION_PATCH')]


# ------------------------------------------------------------- macros
def classify_macros(root, conf_dir, known_names):
    """object-like LV_* macros -> {'int': [...], 'str': [...], 'alias': {name: target}} and function-like"""
    stub = os.path.join(conf_dir, 'stubs')
    base = ['gcc', '-std=gnu99', '-I', stub, '-I', conf_dir, '-I', root, '-I', os.path.dirname(root),
            '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE']
    p = subprocess.run(base + ['-E', '-dM', os.path.join(root, 'lvgl.h')], capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(p.stderr[-3000:])
    objs, funcs = {}, {}
    for line in p.stdout.split('\n'):
        m = re.match(r'#define (\w+)(\([^)]*\))? ?(.*)$', line)
        if not m:
            continue
        name, args, body = m.group(1), m.group(2), m.group(3).strip()
        if not (name.startswith('LV_') or name.startswith('lv_') or name.startswith('LVGL_')):
            continue
        if args is not None:
            funcs[name] = {'args': [a.strip() for a in args[1:-1].split(',') if a.strip()], 'body': body}
        else:
            objs[name] = body
    aliases = {}
    cands = []
    for name, body in objs.items():
        if re.fullmatch(r'[A-Za-z_]\w*', body or '') and (body in known_names or body in objs or body in funcs) \
                and body != name:
            aliases[name] = body
            continue
        if not body or name.endswith('_H') or name.startswith('LV_ATTRIBUTE') or name.startswith('lv_'):
            continue
        cands.append(name)
    # function-like aliases: #define lv_old(a, b) lv_new(a, b)
    for name, info in funcs.items():
        m = re.fullmatch(r'(\w+)\s*\(([^()]*)\)', info['body'])
        if m and m.group(1) != name and [a.strip() for a in m.group(2).split(',') if a.strip()] == info['args']:
            aliases[name] = m.group(1)
    ints, strs = probe(base, root, cands, 'int')
    return {'int': sorted(ints), 'str': sorted(strs), 'alias': aliases,
            'func': {k: v for k, v in funcs.items() if k not in aliases}}


def probe(base, root, names, _):
    tmp = tempfile.mkdtemp()
    src = os.path.join(tmp, 'p.c')

    def attempt(names, mode):
        names = list(names)
        while True:
            lines = ['#include "lvgl.h"']
            index = {}
            for n in names:
                index[len(lines) + 1] = n
                if mode == 'int':
                    lines.append('static const long long p_%s = (long long)(%s) + 0 * sizeof(char[(%s) * 0 + 1]);' % (n, n, n))
                else:
                    lines.append('static const char* const p_%s = "" %s "";' % (n, n))
            with open(src, 'w') as f:
                f.write('\n'.join(lines) + '\n')
            r = subprocess.run(base + ['-fsyntax-only', '-fmax-errors=0', '-w', src], capture_output=True, text=True)
            bad = set()
            for m in re.finditer(r'p\.c:(\d+):\d+: error', r.stderr):
                ln = int(m.group(1))
                if ln in index:
                    bad.add(index[ln])
            if r.returncode == 0:
                return names
            if not bad:
                # errors inside headers / macro expansion notes: fall back to per-line
                more = set()
                for m in re.finditer(r'p\.c:(\d+):', r.stderr):
                    ln = int(m.group(1))
                    if ln in index:
                        more.add(index[ln])
                if not more:
                    # the error is reported only inside a header: bisect
                    if len(names) <= 1:
                        return []
                    half = len(names) // 2
                    return attempt(names[:half], mode) + attempt(names[half:], mode)
                bad = more
            names = [n for n in names if n not in bad]

    ints = attempt(names, 'int')
    rest = [n for n in names if n not in set(ints)]
    strs = attempt(rest, 'str') if rest else []
    return ints, strs


if __name__ == '__main__':
    root, work, tag, out = sys.argv[1:5]
    db = extract(root, work, tag)
    with open(out, 'w') as f:
        json.dump(db, f, indent=0)
    print(tag, len(db['functions']), 'functions', len(db['enums']), 'enums', len(db['macros']['int']), 'int macros',
          len(db['macros']['str']), 'str macros', len(db['macros']['alias']), 'aliases', len(db['structs']), 'structs')
