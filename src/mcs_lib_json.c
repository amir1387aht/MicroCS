/* MicroCS - JSON for scripts (MCS_ENABLE_JSON; needs List and Dictionary):
 *   Json.Parse(text)                 object -> Dictionary<string, object>, array -> List<object>,
 *                                    number -> int (integral, in range) or double, string, bool, null
 *   Json.Stringify(value[, indent[, includeHidden]])  Dictionary, List / arrays, HashSet, numbers, strings, bool, null,
 *                                    char, script fields + computed getters (underscore names opt-in)
 *   Json.Serialize = Stringify, Json.Deserialize = Parse, Json.Escape(string) -> quoted JSON string
 *   JsonSerializer.Serialize / Deserialize    the same, under the System.Text.Json name
 *   Json.TryParse(text, out value)    false + null for invalid text; allocation errors propagate
 * Parsing errors carry a byte offset; cycles and excessive nesting fail explicitly. */
#include "mcs_lib.h"
#include "mcs_native_internal.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

#if MCS_ENABLE_JSON && MCS_ENABLE_LIST && MCS_ENABLE_DICT

/* ------------------------------------------------------------ parser */
typedef struct { mcs_vm_t* vm; const char* s; size_t n, i; int depth; bool failed, quiet; mcs_buf_t scratch; mcs_value_t result; } jp_t;

static bool jp_fail(jp_t* p, const char* what) {
    if (!p->failed && !p->quiet) mcs_throw(p->vm, EXC_FORMAT, "invalid JSON at offset %u: %s", (unsigned)p->i, what);
    p->failed = true;
    return false;
}
static void jp_ws(jp_t* p) {
    while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')) p->i++;
}
static int jp_hex4(jp_t* p) {
    if (p->i + 4 > p->n) return -1;
    int v = 0;
    for (int k = 0; k < 4; k++) {
        char c = p->s[p->i++];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}
/* Validate Unicode scalars, rejecting overlong UTF-8, surrogates and > U+10FFFF. */
static size_t json_utf8(const char* text, size_t n) {
    if (!n) return 0;
    const unsigned char* s = (const unsigned char*)text;
    if (s[0] < 0x80) return 1;
    unsigned count = s[0] >= 0xC2 && s[0] <= 0xDF ? 2 : s[0] >= 0xE0 && s[0] <= 0xEF ? 3 : s[0] >= 0xF0 && s[0] <= 0xF4 ? 4 : 0;
    if (!count || n < count) return 0;
    for (unsigned i = 1; i < count; i++) if ((s[i] & 0xC0) != 0x80) return 0;
    if ((s[0] == 0xE0 && s[1] < 0xA0) || (s[0] == 0xED && s[1] >= 0xA0) ||
        (s[0] == 0xF0 && s[1] < 0x90) || (s[0] == 0xF4 && s[1] >= 0x90)) return 0;
    return count;
}
static bool jp_string(jp_t* p, mcs_value_t* out) {
    p->i++;                                       /* opening quote */
    mcs_buf_t* b = &p->scratch; b->len = 0;
    for (;;) {
        if (p->i >= p->n) { return jp_fail(p, "unterminated string"); }
        char c = p->s[p->i];
        if (c == '"') { p->i++; break; }
        if ((unsigned char)c < 0x20) { return jp_fail(p, "control character in string"); }
        if (c != '\\') {
            size_t st = p->i;
            while (p->i < p->n && p->s[p->i] != '"' && p->s[p->i] != '\\' && (unsigned char)p->s[p->i] >= 0x20) {
                size_t width = json_utf8(p->s + p->i, p->n - p->i);
                if (!width) return jp_fail(p, "invalid UTF-8");
                p->i += width;
            }
            mcs_buf_putn(b, p->s + st, p->i - st);
            continue;
        }
        if (++p->i >= p->n) { return jp_fail(p, "unterminated string"); }
        c = p->s[p->i++];
        switch (c) {
        case '"': case '\\': case '/': mcs_buf_putc(b, c); break;
        case 'b': mcs_buf_putc(b, '\b'); break;
        case 'f': mcs_buf_putc(b, '\f'); break;
        case 'n': mcs_buf_putc(b, '\n'); break;
        case 'r': mcs_buf_putc(b, '\r'); break;
        case 't': mcs_buf_putc(b, '\t'); break;
        case 'u': {
            int cp = jp_hex4(p);
            if (cp < 0) { return jp_fail(p, "bad \\u escape"); }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (p->i + 6 > p->n || p->s[p->i] != '\\' || p->s[p->i + 1] != 'u') return jp_fail(p, "unpaired high surrogate");
                p->i += 2;
                int lo = jp_hex4(p);
                if (lo < 0xDC00 || lo > 0xDFFF) return jp_fail(p, "unpaired high surrogate");
                cp = 0x10000 + ((cp - 0xD800) << 10) + lo - 0xDC00;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) return jp_fail(p, "unpaired low surrogate");
            mcs_buf_utf8(b, (uint32_t)cp);
            break;
        }
        default: p->i--; return jp_fail(p, "bad escape");
        }
    }
    *out = OBJ_VAL(mcs_buf_to_string(b));
    return true;
}
static bool jp_number(jp_t* p, mcs_value_t* out) {
    size_t st = p->i;
    bool frac = false;
    if (p->s[p->i] == '-') p->i++;
    if (p->i >= p->n || p->s[p->i] < '0' || p->s[p->i] > '9') return jp_fail(p, "bad number");
    if (p->s[p->i] == '0') {
        p->i++;
        if (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') return jp_fail(p, "leading zero");
    } else while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
    if (p->i < p->n && p->s[p->i] == '.') {
        frac = true; p->i++;
        if (p->i >= p->n || p->s[p->i] < '0' || p->s[p->i] > '9') return jp_fail(p, "bad number");
        while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
    }
    if (p->i < p->n && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
        frac = true; p->i++;
        if (p->i < p->n && (p->s[p->i] == '+' || p->s[p->i] == '-')) p->i++;
        if (p->i >= p->n || p->s[p->i] < '0' || p->s[p->i] > '9') return jp_fail(p, "bad number");
        while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
    }
    if (!frac) {
        mcs_int_t value; bool overflow;
        if (lib_parse_int(p->s + st, p->i - st, 10, &value, &overflow)) {
#if MCS_ENABLE_FLOAT
            if (value == 0 && p->s[st] == '-') { *out = mcs_float((mcs_float_t)-0.0); return true; }
#endif
            *out = mcs_int(value); return true;
        }
    }
#if MCS_ENABLE_FLOAT
    /* The source is NUL-terminated. strtod can consume the full lexeme without
     * a truncated fixed-size copy; the strict lexer already determined its end. */
    char* end; errno = 0;
    double number = strtod(p->s + st, &end);
    mcs_float_t value = (mcs_float_t)number;
    if (end != p->s + p->i || !isfinite(number) || !isfinite((double)value) ||
        (errno == ERANGE && number == 0) || (number != 0 && value == 0)) return jp_fail(p, "number out of range");
    *out = mcs_float(value); return true;
#else
    return jp_fail(p, "number out of the integer range (no float support)");
#endif
}
static bool jp_lit(jp_t* p, const char* w, mcs_value_t v, mcs_value_t* out) {
    size_t l = strlen(w);
    if (p->i + l > p->n || memcmp(p->s + p->i, w, l)) return jp_fail(p, "unexpected token");
    p->i += l; *out = v;
    return true;
}
static bool jp_value(jp_t* p, mcs_value_t* out) {
    jp_ws(p);
    if (p->i >= p->n) return jp_fail(p, "unexpected end");
    mcs_vm_t* vm = p->vm;
    char c = p->s[p->i];
    if (c == '{' || c == '[') {
        if (++p->depth > MCS_JSON_MAX_DEPTH) return jp_fail(p, "nested too deep");
        bool obj = c == '{';
        mcs_value_t box = obj ? OBJ_VAL(mcs_new_dict(vm)) : mcs_new_list(vm);
        p->i++;
        jp_ws(p);
        char close = obj ? '}' : ']';
        if (p->i < p->n && p->s[p->i] == close) p->i++;
        else for (;;) {
            mcs_value_t key = mcs_null(), v;
            if (obj) {
                jp_ws(p);
                if (p->i >= p->n || p->s[p->i] != '"') { jp_fail(p, "expected a \"key\""); break; }
                if (!jp_string(p, &key)) break;
                jp_ws(p);
                if (p->i >= p->n || p->s[p->i] != ':') { jp_fail(p, "expected ':'"); break; }
                p->i++;
            }
            bool ok = jp_value(p, &v);
            if (ok) {
                if (obj) mcs_dict_set(vm, AS_DICT(box), key, v);
                else mcs_list_add(vm, box, v);
            }
            if (!ok) break;
            jp_ws(p);
            if (p->i < p->n && p->s[p->i] == ',') { p->i++; continue; }
            if (p->i < p->n && p->s[p->i] == close) { p->i++; break; }
            jp_fail(p, obj ? "expected ',' or '}'" : "expected ',' or ']'");
            break;
        }
        p->depth--;
        *out = box;
        return !p->failed;
    }
    if (c == '"') return jp_string(p, out);
    if (c == '-' || (c >= '0' && c <= '9')) return jp_number(p, out);
    if (c == 't') return jp_lit(p, "true", mcs_bool(true), out);
    if (c == 'f') return jp_lit(p, "false", mcs_bool(false), out);
    if (c == 'n') return jp_lit(p, "null", mcs_null(), out);
    return jp_fail(p, "unexpected character");
}
static void jp_parse_body(void* data) {
    jp_t* p = (jp_t*)data;
    if (!jp_value(p, &p->result)) return;
    jp_ws(p);
    if (p->i != p->n) jp_fail(p, "unexpected data after the value");
}
static mcs_value_t json_parse_str(mcs_vm_t* vm, mcs_value_t text, bool quiet, bool* success) {
    *success = false;
    if (!IS_STRING(text)) {
        if (!quiet) mcs_throw(vm, mcs_is_null(text) ? EXC_ARGNULL : EXC_ARGUMENT, "Json.Parse expects JSON text");
        return mcs_null();
    }
    jp_t p; memset(&p, 0, sizeof p); p.vm = vm; p.s = AS_CSTR(text); p.n = AS_STRING(text)->len; p.quiet = quiet;
    mcs_buf_init(&p.scratch, vm);
    if (p.n >= 3 && (unsigned char)p.s[0] == 0xEF && (unsigned char)p.s[1] == 0xBB && (unsigned char)p.s[2] == 0xBF) p.i = 3;
    int pause = vm->gc_pause; vm->gc_pause++;
    int code = mcs_protect_native(vm, jp_parse_body, &p);
    vm->gc_pause = pause;
    mcs_buf_free(&p.scratch);  /* also on malformed input, abort and fatal OOM */
    mcs_rethrow_native(vm, code);
    *success = !p.failed && !vm->has_exc;
    return *success ? p.result : mcs_null();
}

/* ------------------------------------------------------------ writer */
static void js_quote(mcs_buf_t* b, const char* s, size_t n) {
    for (size_t i = 0; i < n;) {
        size_t width = json_utf8(s + i, n - i);
        if (!width) { mcs_throw(b->vm, EXC_FORMAT, "Json: invalid UTF-8 string"); return; }
        i += width;
    }
    static const char hex[] = "0123456789abcdef";
    mcs_buf_putc(b, '"');
    size_t st = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') continue;
        mcs_buf_putn(b, s + st, i - st);
        st = i + 1;
        mcs_buf_putc(b, '\\');
        switch (c) {
        case '"': mcs_buf_putc(b, '"'); break;
        case '\\': mcs_buf_putc(b, '\\'); break;
        case '\n': mcs_buf_putc(b, 'n'); break;
        case '\r': mcs_buf_putc(b, 'r'); break;
        case '\t': mcs_buf_putc(b, 't'); break;
        case '\b': mcs_buf_putc(b, 'b'); break;
        case '\f': mcs_buf_putc(b, 'f'); break;
        default: mcs_buf_puts(b, "u00"); mcs_buf_putc(b, hex[c >> 4]); mcs_buf_putc(b, hex[c & 15]); break;
        }
    }
    mcs_buf_putn(b, s + st, n - st);
    mcs_buf_putc(b, '"');
}
static void js_nl(mcs_buf_t* b, int indent, int depth) {
    if (!indent) return;
    mcs_buf_putc(b, '\n');
    for (int i = 0; i < depth * indent; i++) mcs_buf_putc(b, ' ');
}
static bool js_char(mcs_vm_t* vm, mcs_buf_t* b, mcs_int_t value) {
    uint32_t cp = (uint32_t)value;
    if (value < 0 || (uint64_t)value > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        mcs_throw(vm, EXC_FORMAT, "Json: invalid Unicode character"); return false;
    }
    char text[5]; mcs_buf_t c = { text, 0, sizeof text, vm };
    mcs_buf_utf8(&c, cp); js_quote(b, text, c.len); return !vm->has_exc;
}
typedef struct js_path { mcs_obj_t* object; const struct js_path* parent; } js_path_t;
static bool js_value(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, int indent, int depth, const js_path_t* path, bool hidden);
static bool js_key_value(mcs_vm_t* vm, mcs_buf_t* b, bool* first, mcs_value_t key, const char* name, mcs_value_t value, int indent, int depth, const js_path_t* path, bool hidden) {
    if (!*first) mcs_buf_putc(b, ',');
    *first = false; js_nl(b, indent, depth + 1);
    if (name) js_quote(b, name, strlen(name));
    else if (IS_STRING(key)) js_quote(b, AS_CSTR(key), AS_STRING(key)->len);
    else if (key.type == MCS_T_CHAR) { if (!js_char(vm, b, key.as.i)) return false; }
    else {
        char text[64];
        if (key.type == MCS_T_INT) mcs_format_int(text, key.as.i);
#if MCS_ENABLE_FLOAT
        else if (key.type == MCS_T_FLOAT && isfinite((double)key.as.f)) mcs_format_float(text, key.as.f);
#endif
        else if (key.type == MCS_T_BOOL) strcpy(text, key.as.b ? "True" : "False");
        else { mcs_throw(vm, EXC_NOTSUPPORTED, "Json: dictionary keys must be strings or scalar values"); return false; }
        js_quote(b, text, strlen(text));
    }
    mcs_buf_putc(b, ':'); if (indent) mcs_buf_putc(b, ' ');
    return !vm->has_exc && js_value(vm, b, value, indent, depth + 1, path, hidden);
}
static bool js_visible(const char* name, bool hidden) {
    return name[0] != '$' && name[0] != '<' && (hidden || name[0] != '_');
}
static bool js_value(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, int indent, int depth, const js_path_t* path, bool hidden) {
    if (vm->has_exc) return false;
    if (depth > MCS_JSON_MAX_DEPTH) { mcs_throw(vm, EXC_INVOP, "Json.Stringify: nested deeper than %d", MCS_JSON_MAX_DEPTH); return false; }
    switch (v.type) {
    case MCS_T_NULL: mcs_buf_puts(b, "null"); return true;
    case MCS_T_BOOL: mcs_buf_puts(b, v.as.b ? "true" : "false"); return true;
    case MCS_T_INT: return mcs_value_to_buf(vm, b, v);
#if MCS_ENABLE_FLOAT
    case MCS_T_FLOAT:
        if (!isfinite((double)v.as.f)) { mcs_buf_puts(b, "null"); return true; } /* retained compatibility */
        return mcs_value_to_buf(vm, b, v);
#endif
    case MCS_T_CHAR: return js_char(vm, b, v.as.i);
    default: break;
    }
    if (IS_STRING(v)) { js_quote(b, AS_CSTR(v), AS_STRING(v)->len); return !vm->has_exc; }
    if (!IS_OBJ(v)) { mcs_throw(vm, EXC_NOTSUPPORTED, "Json: unsupported value"); return false; }
    for (const js_path_t* p = path; p; p = p->parent) if (p->object == AS_OBJ(v)) {
        mcs_throw(vm, EXC_INVOP, "Json.Stringify: reference cycle"); return false;
    }
    js_path_t node = { AS_OBJ(v), path }; path = &node;
    if (IS_KIND(v, MCS_O_ARRAY) || IS_KIND(v, MCS_O_LIST)) {
        mcs_list_t* l = AS_LIST(v); mcs_buf_putc(b, '[');
        for (uint32_t i = 0; i < l->count; i++) {
            if (i) mcs_buf_putc(b, ',');
            js_nl(b, indent, depth + 1);
            if (!js_value(vm, b, l->items[i], indent, depth + 1, path, hidden)) return false;
        }
        if (l->count) js_nl(b, indent, depth);
        mcs_buf_putc(b, ']'); return true;
    }
    if (IS_KIND(v, MCS_O_DICT)) {
        mcs_dict_t* d = AS_DICT(v); bool set = (d->obj.aux & DICT_KEYS_ONLY) != 0, first = true;
        mcs_buf_putc(b, set ? '[' : '{');
        for (uint32_t i = 0; i < d->count; i++) {
            if (set) {
                if (!first) mcs_buf_putc(b, ',');
                first = false; js_nl(b, indent, depth + 1);
                if (!js_value(vm, b, d->keys[i], indent, depth + 1, path, hidden)) return false;
            } else if (!js_key_value(vm, b, &first, d->keys[i], NULL, d->vals[i], indent, depth, path, hidden)) return false;
        }
        if (!first) js_nl(b, indent, depth);
        mcs_buf_putc(b, set ? ']' : '}'); return true;
    }
    if (IS_KIND(v, MCS_O_INSTANCE)) {
        mcs_instance_t* in = AS_INSTANCE(v); mcs_table_t* t = &in->cls->fields;
        bool first = true; mcs_value_t items;
        if (mcs_table_get_s(t, mcs_intern_c(vm, "$items"), &items) && items.type == MCS_T_INT)
            return js_value(vm, b, in->fields[items.as.i], indent, depth, path, hidden);
        mcs_buf_putc(b, '{');
        for (uint32_t slot = 0; slot < in->cls->field_count; slot++) for (uint32_t e = 0; e < t->cap; e++) {
            mcs_entry_t* en = &t->entries[e];
            if (!en->key || en->value.type != MCS_T_INT || (uint32_t)en->value.as.i != slot) continue;
            if (js_visible(en->key->chars, hidden) && !js_key_value(vm, b, &first, mcs_null(), en->key->chars, in->fields[slot], indent, depth, path, hidden)) return false;
            break;
        }
        /* Auto-properties live in field slots. Computed/inherited getters are
         * invoked once; derived properties and fields override base properties. */
        for (mcs_class_t* c = in->cls; c; c = c->super) for (uint32_t e = 0; e < c->getters.cap; e++) {
            mcs_entry_t* en = &c->getters.entries[e]; mcs_value_t ignored, value;
            if (!en->key || !js_visible(en->key->chars, hidden) || mcs_table_get_s(t, en->key, &ignored)) continue;
            if (IS_KIND(en->value, MCS_O_CLOSURE) && (AS_CLOSURE(en->value)->fn->flags & FN_IS_STATIC)) continue;
            bool shadowed = false;
            for (mcs_class_t* sub = in->cls; sub != c; sub = sub->super) if (mcs_table_get_s(&sub->getters, en->key, &ignored)) { shadowed = true; break; }
            if (shadowed) continue;
            if (mcs_call_internal(vm, en->value, v, 0, NULL, &value) != MCS_OK) return false;
            if (!js_key_value(vm, b, &first, mcs_null(), en->key->chars, value, indent, depth, path, hidden)) return false;
        }
        if (!first) js_nl(b, indent, depth);
        mcs_buf_putc(b, '}'); return true;
    }
    if (IS_KIND(v, MCS_O_USERDATA)) {
        mcs_class_t* cls = mcs_class_of(vm, v); mcs_value_t method, text;
        if (cls && mcs_cls_get(vm, cls, MCS_TAB_METHODS, vm->s_tostring, &method) &&
            mcs_call_internal(vm, method, v, 0, NULL, &text) == MCS_OK && IS_STRING(text)) {
            js_quote(b, AS_CSTR(text), AS_STRING(text)->len); return !vm->has_exc;
        }
        if (vm->has_exc) return false;
    }
    mcs_throw(vm, EXC_NOTSUPPORTED, "Json: unsupported %s (use a data object)", mcs_type_name(vm, v)); return false;
}

typedef struct { mcs_vm_t* vm; mcs_buf_t buffer; mcs_value_t input, result; int indent; bool hidden, escape; } js_call_t;
static void js_write_body(void* data) {
    js_call_t* c = (js_call_t*)data;
    if (c->escape) {
        if (IS_STRING(c->input)) js_quote(&c->buffer, AS_CSTR(c->input), AS_STRING(c->input)->len);
        else if (!mcs_is_null(c->input)) { mcs_throw(c->vm, EXC_ARGUMENT, "Json.Escape expects a string"); return; }
        else mcs_buf_puts(&c->buffer, "null");
    } else if (!js_value(c->vm, &c->buffer, c->input, c->indent, 0, NULL, c->hidden)) return;
    if (!c->vm->has_exc) c->result = OBJ_VAL(mcs_buf_to_string(&c->buffer));
}
static mcs_value_t js_write(mcs_vm_t* vm, mcs_value_t value, int indent, bool hidden, bool escape) {
    js_call_t c; memset(&c, 0, sizeof c); c.vm = vm; c.input = value; c.indent = indent; c.hidden = hidden; c.escape = escape;
    mcs_buf_init(&c.buffer, vm);
    int pause = vm->gc_pause; vm->gc_pause++;
    int code = mcs_protect_native(vm, js_write_body, &c);
    vm->gc_pause = pause; mcs_buf_free(&c.buffer); mcs_rethrow_native(vm, code);
    return vm->has_exc ? mcs_null() : c.result;
}

/* ------------------------------------------------------------ natives */
NATIVE(json_parse) { bool ok; return json_parse_str(vm, argv[0], false, &ok); }
NATIVE(json_tryparse) {
    bool ok; mcs_value_t value = json_parse_str(vm, argv[0], true, &ok);
    lib_out_set(argv[1], value); return mcs_bool(ok);
}
NATIVE(json_stringify) {
    if (argc < 1 || argc > 3) { mcs_throw(vm, EXC_ARGUMENT, "Json.Stringify(value[, indent[, includeHidden]])"); return mcs_null(); }
    int indent = 0;
    if (argc > 1) {
        mcs_int_t width = mcs_is_bool(argv[1]) ? (argv[1].as.b ? 2 : 0) : mcs_to_int(vm, argv[1]); CHECK();
        indent = width < 0 ? 0 : width > 8 ? 8 : (int)width;
    }
    return js_write(vm, argv[0], indent, argc > 2 && mcs_to_bool(vm, argv[2]), false);
}
NATIVE(json_escape) { return js_write(vm, argv[0], 0, false, true); }
static const mcs_reg_t json_fns[] = {
    MCS_FN("Parse", json_parse, 1), MCS_FN("TryParse", json_tryparse, 2), MCS_FN("Deserialize", json_parse, 1),
    MCS_FN("Stringify", json_stringify, -1), MCS_FN("Serialize", json_stringify, -1),
    MCS_FN("Escape", json_escape, 1), MCS_REG_END
};

enum { LZ_JSON, LZ_JSONSER };
const mcs_lib_entry_t mcs_lib_json_entries[] = {
    { "Json", LZ_JSON, MCS_LIB_COLLECTIONS }, { "JsonSerializer", LZ_JSONSER, MCS_LIB_COLLECTIONS },
    { NULL, 0, 0 }
};
void mcs_lib_json_make(mcs_vm_t* vm, int id) {
    mcs_class_t* c = mcs_define_builtin_class(vm, id == LZ_JSON ? "Json" : "JsonSerializer", NULL, CLS_STATIC);
    mcs_add_regs(vm, c, json_fns, true);
}
#else
const mcs_lib_entry_t mcs_lib_json_entries[] = { { NULL, 0, 0 } };
void mcs_lib_json_make(mcs_vm_t* vm, int id) { (void)vm; (void)id; }
#endif /* MCS_ENABLE_JSON */
