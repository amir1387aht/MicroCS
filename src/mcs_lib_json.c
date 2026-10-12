/* MicroCS - JSON for scripts (MCS_ENABLE_JSON; needs List and Dictionary):
 *   Json.Parse(text)                 object -> Dictionary<string, object>, array -> List<object>,
 *                                    number -> int (integral, in range) or double, string, bool, null
 *   Json.Stringify(value[, indent])  Dictionary, List / arrays, HashSet, numbers, strings, bool, null,
 *                                    char, script objects (their fields, except names starting with '_')
 *   Json.Serialize = Stringify, Json.Deserialize = Parse, Json.Escape(string) -> quoted JSON string
 *   JsonSerializer.Serialize / Deserialize    the same, under the System.Text.Json name
 * Errors throw FormatException with the byte offset; nesting is limited to MCS_JSON_MAX_DEPTH. */
#include "mcs_lib.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#if MCS_ENABLE_JSON && MCS_ENABLE_LIST && MCS_ENABLE_DICT

#ifndef MCS_JSON_MAX_DEPTH
#define MCS_JSON_MAX_DEPTH 32
#endif

/* ------------------------------------------------------------ parser */
typedef struct { mcs_vm_t* vm; const char* s; size_t n, i; int depth; bool failed; } jp_t;

static bool jp_fail(jp_t* p, const char* what) {
    if (!p->failed) mcs_throw(p->vm, EXC_FORMAT, "invalid JSON at offset %u: %s", (unsigned)p->i, what);
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
static bool jp_string(jp_t* p, mcs_value_t* out) {
    p->i++;                                       /* opening quote */
    mcs_buf_t b; mcs_buf_init(&b, p->vm);
    for (;;) {
        if (p->i >= p->n) { mcs_buf_free(&b); return jp_fail(p, "unterminated string"); }
        char c = p->s[p->i];
        if (c == '"') { p->i++; break; }
        if ((unsigned char)c < 0x20) { mcs_buf_free(&b); return jp_fail(p, "control character in string"); }
        if (c != '\\') {
            size_t st = p->i;
            while (p->i < p->n && p->s[p->i] != '"' && p->s[p->i] != '\\' && (unsigned char)p->s[p->i] >= 0x20) p->i++;
            mcs_buf_putn(&b, p->s + st, p->i - st);
            continue;
        }
        if (++p->i >= p->n) { mcs_buf_free(&b); return jp_fail(p, "unterminated string"); }
        c = p->s[p->i++];
        switch (c) {
        case '"': case '\\': case '/': mcs_buf_putc(&b, c); break;
        case 'b': mcs_buf_putc(&b, '\b'); break;
        case 'f': mcs_buf_putc(&b, '\f'); break;
        case 'n': mcs_buf_putc(&b, '\n'); break;
        case 'r': mcs_buf_putc(&b, '\r'); break;
        case 't': mcs_buf_putc(&b, '\t'); break;
        case 'u': {
            int cp = jp_hex4(p);
            if (cp < 0) { mcs_buf_free(&b); return jp_fail(p, "bad \\u escape"); }
            if (cp >= 0xD800 && cp <= 0xDBFF && p->i + 6 <= p->n && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
                size_t save = p->i; p->i += 2;
                int lo = jp_hex4(p);
                if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                else p->i = save;
            }
            mcs_buf_utf8(&b, (uint32_t)cp);
            break;
        }
        default: mcs_buf_free(&b); p->i--; return jp_fail(p, "bad escape");
        }
    }
    *out = OBJ_VAL(mcs_buf_to_string(&b));
    return true;
}
static bool jp_number(jp_t* p, mcs_value_t* out) {
    size_t st = p->i;
    bool frac = false;
    if (p->s[p->i] == '-') p->i++;
    if (p->i >= p->n || p->s[p->i] < '0' || p->s[p->i] > '9') return jp_fail(p, "bad number");
    while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') p->i++;
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
    char tmp[48];
    size_t len = p->i - st;
    if (len >= sizeof tmp) len = sizeof tmp - 1;
    memcpy(tmp, p->s + st, len); tmp[len] = 0;
    if (!frac) {
        char* end; long long v = strtoll(tmp, &end, 10);
        mcs_int_t iv = (mcs_int_t)v;
        if ((long long)iv == v && v != LLONG_MAX && v != LLONG_MIN) { *out = mcs_int(iv); return true; }
    }
#if MCS_ENABLE_FLOAT
    *out = mcs_float((mcs_float_t)strtod(tmp, NULL));
    return true;
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
static mcs_value_t json_parse_str(mcs_vm_t* vm, mcs_value_t text) {
    if (mcs_is_null(text)) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'json')"); return mcs_null(); }
    if (!IS_STRING(text)) { mcs_throw(vm, EXC_ARGUMENT, "Json.Parse expects a string, got %s", mcs_type_name(vm, text)); return mcs_null(); }
    jp_t p = { vm, AS_CSTR(text), AS_STRING(text)->len, 0, 0, false };
    if (p.n >= 3 && (unsigned char)p.s[0] == 0xEF && (unsigned char)p.s[1] == 0xBB && (unsigned char)p.s[2] == 0xBF) p.i = 3;
    mcs_value_t v = mcs_null();
    /* nothing runs a script while parsing, so the GC is simply held off instead of rooting
     * every open container and key (two roots per nesting level overflowed MCS_MAX_ROOTS
     * at 16 nested objects, a fatal error) */
    vm->gc_pause++;
    bool ok = jp_value(&p, &v);
    vm->gc_pause--;
    if (!ok) return mcs_null();
    jp_ws(&p);
    if (p.i != p.n) { jp_fail(&p, "unexpected data after the value"); return mcs_null(); }
    return v;
}

/* ------------------------------------------------------------ writer */
static void js_quote(mcs_buf_t* b, const char* s, size_t n) {
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
static bool js_value(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, int indent, int depth);
static bool js_key_value(mcs_vm_t* vm, mcs_buf_t* b, bool* first, mcs_value_t key, const char* kname, mcs_value_t v, int indent, int depth) {
    if (!*first) mcs_buf_putc(b, ',');
    *first = false;
    js_nl(b, indent, depth + 1);
    if (kname) js_quote(b, kname, strlen(kname));
    else if (IS_STRING(key)) js_quote(b, AS_CSTR(key), AS_STRING(key)->len);
    else {
        mcs_buf_t k; mcs_buf_init(&k, vm);
        if (!mcs_value_to_buf(vm, &k, key)) { mcs_buf_free(&k); return false; }
        js_quote(b, k.data ? k.data : "", k.len);
        mcs_buf_free(&k);
    }
    mcs_buf_putc(b, ':');
    if (indent) mcs_buf_putc(b, ' ');
    return js_value(vm, b, v, indent, depth + 1);
}
static bool js_value(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, int indent, int depth) {
    if (depth > MCS_JSON_MAX_DEPTH) { mcs_throw(vm, EXC_INVOP, "Json.Stringify: nested deeper than %d (a cycle?)", MCS_JSON_MAX_DEPTH); return false; }
    switch (v.type) {
    case MCS_T_NULL: mcs_buf_puts(b, "null"); return true;
    case MCS_T_BOOL: mcs_buf_puts(b, v.as.b ? "true" : "false"); return true;
    case MCS_T_INT: return mcs_value_to_buf(vm, b, v);
#if MCS_ENABLE_FLOAT
    case MCS_T_FLOAT:
        if (isnan((double)v.as.f) || isinf((double)v.as.f)) { mcs_buf_puts(b, "null"); return true; }
        return mcs_value_to_buf(vm, b, v);
#endif
    case MCS_T_CHAR: {
        mcs_buf_t c; mcs_buf_init(&c, vm);
        mcs_buf_utf8(&c, (uint32_t)v.as.i);
        js_quote(b, c.data ? c.data : "", c.len);
        mcs_buf_free(&c);
        return true;
    }
    default: break;
    }
    if (IS_STRING(v)) { js_quote(b, AS_CSTR(v), AS_STRING(v)->len); return true; }
    if (IS_KIND(v, MCS_O_ARRAY) || IS_KIND(v, MCS_O_LIST)) {
        mcs_list_t* l = AS_LIST(v);
        mcs_buf_putc(b, '[');
        for (uint32_t i = 0; i < l->count; i++) {
            if (i) mcs_buf_putc(b, ',');
            js_nl(b, indent, depth + 1);
            if (!js_value(vm, b, l->items[i], indent, depth + 1)) return false;
        }
        if (l->count) js_nl(b, indent, depth);
        mcs_buf_putc(b, ']');
        return true;
    }
    if (IS_KIND(v, MCS_O_DICT)) {
        mcs_dict_t* d = AS_DICT(v);
        bool set = (d->obj.aux & DICT_KEYS_ONLY) != 0, first = true;
        mcs_buf_putc(b, set ? '[' : '{');
        for (uint32_t i = 0; i < d->count; i++) {
            if (set) {
                if (!first) mcs_buf_putc(b, ',');
                first = false;
                js_nl(b, indent, depth + 1);
                if (!js_value(vm, b, d->keys[i], indent, depth + 1)) return false;
            } else if (!js_key_value(vm, b, &first, d->keys[i], NULL, d->vals[i], indent, depth)) return false;
        }
        if (!first) js_nl(b, indent, depth);
        mcs_buf_putc(b, set ? ']' : '}');
        return true;
    }
    if (IS_KIND(v, MCS_O_INSTANCE)) {
        mcs_instance_t* in = (mcs_instance_t*)AS_OBJ(v);
        mcs_table_t* t = &in->cls->fields;
        bool first = true;
        mcs_value_t items;          /* HashSet / Stack / Queue wrappers keep their elements in $items */
        if (mcs_table_get_s(t, mcs_intern_c(vm, "$items"), &items) && items.type == MCS_T_INT)
            return js_value(vm, b, in->fields[items.as.i], indent, depth);
        mcs_buf_putc(b, '{');
        /* slots in declaration order */
        for (uint32_t slot = 0; slot < in->cls->field_count; slot++) {
            for (uint32_t e = 0; e < t->cap; e++) {
                mcs_entry_t* en = &t->entries[e];
                if (!IS_STRING(en->key) || en->value.type != MCS_T_INT || (uint32_t)en->value.as.i != slot) continue;
                const char* name = AS_CSTR(en->key);
                if (name[0] == '_' || name[0] == '<' || name[0] == '$') break;
                if (!js_key_value(vm, b, &first, mcs_null(), name, in->fields[slot], indent, depth)) return false;
                break;
            }
        }
        if (!first) js_nl(b, indent, depth);
        mcs_buf_putc(b, '}');
        return true;
    }
    /* anything else (userdata, delegates): its ToString() as a string */
    mcs_buf_t s; mcs_buf_init(&s, vm);
    if (!mcs_value_to_buf(vm, &s, v)) { mcs_buf_free(&s); return false; }
    js_quote(b, s.data ? s.data : "", s.len);
    mcs_buf_free(&s);
    return true;
}

/* ------------------------------------------------------------ natives */
NATIVE(json_parse) { return json_parse_str(vm, argv[0]); }
NATIVE(json_stringify) {
    if (argc < 1 || argc > 2) { mcs_throw(vm, EXC_ARGUMENT, "Json.Stringify(value[, indent])"); return mcs_null(); }
    int indent = 0;
    if (argc > 1) {
        if (mcs_is_bool(argv[1])) indent = argv[1].as.b ? 2 : 0;
        else { indent = (int)mcs_to_int(vm, argv[1]); CHECK(); if (indent < 0) indent = 0; if (indent > 8) indent = 8; }
    }
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (!js_value(vm, &b, argv[0], indent, 0)) { mcs_buf_free(&b); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(json_escape) {
    if (mcs_is_null(argv[0])) return mcs_string(vm, "null");
    const char* s = mcs_to_cstr(vm, argv[0]); CHECK();
    mcs_buf_t b; mcs_buf_init(&b, vm);
    js_quote(&b, s, strlen(s));
    return OBJ_VAL(mcs_buf_to_string(&b));
}
static const mcs_reg_t json_fns[] = {
    MCS_FN("Parse", json_parse, 1), MCS_FN("Deserialize", json_parse, 1),
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
