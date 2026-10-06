/* MicroCS value semantics: equality, conversions, ToString and formatting. */
#include "mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#if MCS_ENABLE_FLOAT
#include <math.h>
#endif

/* ------------------------------------------------------------ buffers */
void mcs_buf_init(mcs_buf_t* b, mcs_vm_t* vm) { b->data = NULL; b->len = b->cap = 0; b->vm = vm; }
static void buf_reserve(mcs_buf_t* b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap < 32 ? 32 : b->cap;
    while (nc < b->len + extra + 1) nc *= 2;
    b->data = (char*)mcs_realloc(b->vm, b->data, b->cap, nc);
    b->cap = nc;
}
void mcs_buf_putn(mcs_buf_t* b, const char* s, size_t n) { buf_reserve(b, n); memcpy(b->data + b->len, s, n); b->len += n; b->data[b->len] = 0; }
void mcs_buf_puts(mcs_buf_t* b, const char* s) { mcs_buf_putn(b, s, strlen(s)); }
void mcs_buf_putc(mcs_buf_t* b, char c) { buf_reserve(b, 1); b->data[b->len++] = c; b->data[b->len] = 0; }
void mcs_buf_free(mcs_buf_t* b) { if (b->data) mcs_realloc(b->vm, b->data, b->cap, 0); b->data = NULL; b->len = b->cap = 0; }
mcs_string_t* mcs_buf_to_string(mcs_buf_t* b) {
    mcs_string_t* s = mcs_take_buffer(b->vm, b->data, b->len, b->cap);
    b->data = NULL; b->len = b->cap = 0;
    return s;
}
void mcs_buf_utf8(mcs_buf_t* b, uint32_t cp) {
    char o[4]; size_t n = 0;
    if (cp < 0x80) o[n++] = (char)cp;
    else if (cp < 0x800) { o[n++] = (char)(0xC0 | (cp >> 6)); o[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o[n++] = (char)(0xE0 | (cp >> 12)); o[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[n++] = (char)(0x80 | (cp & 0x3F)); }
    else { o[n++] = (char)(0xF0 | (cp >> 18)); o[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[n++] = (char)(0x80 | (cp & 0x3F)); }
    mcs_buf_putn(b, o, n);
}

/* ------------------------------------------------------------ numbers */
void mcs_format_int(char* out, mcs_int_t v) {
    char tmp[24]; int n = 0;
    mcs_uint_t u = v < 0 ? (mcs_uint_t)0 - (mcs_uint_t)v : (mcs_uint_t)v;
    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    int k = 0;
    if (v < 0) out[k++] = '-';
    while (n) out[k++] = tmp[--n];
    out[k] = 0;
}

#if MCS_ENABLE_FLOAT
/* shortest round-trip representation, .NET Core style */
void mcs_format_float(char* out, mcs_float_t f) {
    if (f != f) { strcpy(out, "NaN"); return; }
    if (f == (mcs_float_t)INFINITY) { strcpy(out, "\xE2\x88\x9E"); return; }
    if (f == -(mcs_float_t)INFINITY) { strcpy(out, "-\xE2\x88\x9E"); return; }
    if (f == 0) { strcpy(out, signbit(f) ? "-0" : "0"); return; }
    char buf[48];
    int maxp = MCS_FLOAT_DOUBLE ? 17 : 9;
    int p;
    for (p = 1; p <= maxp; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, (double)f);
        if ((mcs_float_t)strtod(buf, NULL) == f) break;
    }
    /* parse d.ddde±x */
    const char* s = buf; bool neg = false;
    if (*s == '-') { neg = true; s++; }
    char digits[24]; int nd = 0;
    while (*s && *s != 'e') { if (*s >= '0' && *s <= '9') digits[nd++] = *s; s++; }
    int e = atoi(s + 1);
    while (nd > 1 && digits[nd - 1] == '0') nd--;
    digits[nd] = 0;
    char* o = out;
    if (neg) *o++ = '-';
    if (e >= 15 || e < -5) {
        *o++ = digits[0];
        if (nd > 1) { *o++ = '.'; memcpy(o, digits + 1, (size_t)nd - 1); o += nd - 1; }
        sprintf(o, "E%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
        return;
    }
    if (e >= 0) {
        for (int i = 0; i <= e; i++) *o++ = i < nd ? digits[i] : '0';
        if (nd > e + 1) { *o++ = '.'; for (int i = e + 1; i < nd; i++) *o++ = digits[i]; }
    } else {
        *o++ = '0'; *o++ = '.';
        for (int i = 0; i < -e - 1; i++) *o++ = '0';
        for (int i = 0; i < nd; i++) *o++ = digits[i];
    }
    *o = 0;
}
#endif

/* ------------------------------------------------------------ type info */
mcs_class_t* mcs_class_of(mcs_vm_t* vm, mcs_value_t v) {
    switch (v.type) {
    case MCS_T_INT: return vm->cls_int;
    case MCS_T_FLOAT: return vm->cls_float;
    case MCS_T_BOOL: return vm->cls_bool;
    case MCS_T_CHAR: return vm->cls_char;
    case MCS_T_OBJ:
        switch (OBJ_KIND(v)) {
        case MCS_O_STRING: return vm->cls_string;
        case MCS_O_INSTANCE: return AS_INSTANCE(v)->cls;
        case MCS_O_USERDATA: return AS_UDATA(v)->cls;
        case MCS_O_ARRAY: return vm->cls_array;
        case MCS_O_LIST: return vm->cls_list;
        case MCS_O_DICT: return vm->cls_dict;
        case MCS_O_CLOSURE: case MCS_O_NATIVE: case MCS_O_BOUND: case MCS_O_OVERLOADS: return vm->cls_delegate;
        case MCS_O_CLASS: return NULL;
        default: return vm->cls_object;
        }
    default: return NULL;
    }
}

const char* mcs_type_name(mcs_vm_t* vm, mcs_value_t v) {
    switch (v.type) {
    case MCS_T_NULL: return "null";
    case MCS_T_INT: return "Int32";
    case MCS_T_FLOAT: return "Double";
    case MCS_T_BOOL: return "Boolean";
    case MCS_T_CHAR: return "Char";
    case MCS_T_UNDEF: return "undefined";
    default: break;
    }
    if (OBJ_KIND(v) == MCS_O_CLASS) return AS_CLASS(v)->name->chars;
    if (OBJ_KIND(v) == MCS_O_ARRAY) return "Object[]";
    mcs_class_t* c = mcs_class_of(vm, v);
    return c ? c->name->chars : "Object";
}

bool mcs_truthy(mcs_value_t v) {
    switch (v.type) {
    case MCS_T_NULL: case MCS_T_UNDEF: return false;
    case MCS_T_BOOL: return v.as.b;
    default: return true;
    }
}

static inline bool is_intlike(mcs_value_t v) { return v.type == MCS_T_INT || v.type == MCS_T_CHAR; }

bool mcs_values_equal(mcs_value_t a, mcs_value_t b) {
    if (a.type == b.type) {
        switch (a.type) {
        case MCS_T_NULL: return true;
        case MCS_T_BOOL: return a.as.b == b.as.b;
        case MCS_T_INT: case MCS_T_CHAR: return a.as.i == b.as.i;
#if MCS_ENABLE_FLOAT
        case MCS_T_FLOAT: return a.as.f == b.as.f;
#endif
        case MCS_T_OBJ: return a.as.o == b.as.o || (a.as.o->kind == MCS_O_INSTANCE && mcs_tuple_equal(a, b, false));
        default: return false;
        }
    }
    if (is_intlike(a) && is_intlike(b)) return a.as.i == b.as.i;
#if MCS_ENABLE_FLOAT
    if (a.type == MCS_T_FLOAT && is_intlike(b)) return a.as.f == (mcs_float_t)b.as.i;
    if (b.type == MCS_T_FLOAT && is_intlike(a)) return b.as.f == (mcs_float_t)a.as.i;
#endif
    return false;
}

/* ------------------------------------------------------------ ToString */
bool mcs_value_to_buf(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v) {
    char tmp[64];
    switch (v.type) {
    case MCS_T_NULL: case MCS_T_UNDEF: return true;
    case MCS_T_BOOL: mcs_buf_puts(b, v.as.b ? "True" : "False"); return true;
    case MCS_T_INT: mcs_format_int(tmp, v.as.i); mcs_buf_puts(b, tmp); return true;
#if MCS_ENABLE_FLOAT
    case MCS_T_FLOAT: mcs_format_float(tmp, v.as.f); mcs_buf_puts(b, tmp); return true;
#endif
    case MCS_T_CHAR: mcs_buf_utf8(b, (uint32_t)v.as.i); return true;
    default: break;
    }
    switch (OBJ_KIND(v)) {
    case MCS_O_STRING: mcs_buf_putn(b, AS_CSTR(v), AS_STRING(v)->len); return true;
    case MCS_O_CLASS: mcs_buf_puts(b, AS_CLASS(v)->name->chars); return true;
    case MCS_O_ARRAY: mcs_buf_puts(b, "System.Object[]"); return true;
    case MCS_O_LIST: mcs_buf_puts(b, "System.Collections.Generic.List`1[System.Object]"); return true;
    case MCS_O_DICT: mcs_buf_puts(b, "System.Collections.Generic.Dictionary`2[System.Object,System.Object]"); return true;
    case MCS_O_CLOSURE: case MCS_O_NATIVE: case MCS_O_BOUND: case MCS_O_OVERLOADS: mcs_buf_puts(b, "System.Delegate"); return true;
    case MCS_O_INSTANCE: case MCS_O_USERDATA: {
        mcs_class_t* cls = mcs_class_of(vm, v);
        mcs_value_t m;
        if (mcs_cls_get(vm, cls, MCS_TAB_METHODS, vm->s_tostring, &m)) {
            mcs_value_t r;
            if (mcs_call_internal(vm, m, v, 0, NULL, &r) != MCS_OK) return false;
            if (IS_STRING(r)) mcs_buf_putn(b, AS_CSTR(r), AS_STRING(r)->len);
            else if (!IS_KIND(r, MCS_O_INSTANCE)) return mcs_value_to_buf(vm, b, r);
            return true;
        }
        mcs_buf_puts(b, cls->name->chars);
        return true;
    }
    default: mcs_buf_puts(b, "Object"); return true;
    }
}

mcs_string_t* mcs_value_to_string(mcs_vm_t* vm, mcs_value_t v) {
    if (IS_STRING(v)) return AS_STRING(v);
    char tmp[64];
    if (v.type == MCS_T_INT) { mcs_format_int(tmp, v.as.i); return mcs_intern_c(vm, tmp); }
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (!mcs_value_to_buf(vm, &b, v)) { mcs_buf_free(&b); return NULL; }
    return mcs_buf_to_string(&b);
}

/* ------------------------------------------------------------ format specifiers */
static void group_digits(mcs_buf_t* b, const char* digits, size_t n) {
    for (size_t i = 0; i < n; i++) {
        mcs_buf_putc(b, digits[i]);
        size_t rem = n - i - 1;
        if (rem && rem % 3 == 0) mcs_buf_putc(b, ',');
    }
}

#if MCS_ENABLE_FLOAT
static double as_double(mcs_value_t v) { return v.type == MCS_T_FLOAT ? (double)v.as.f : (double)v.as.i; }
#endif

/* fixed-point with `dec` decimals, optional grouping, min integer digits */
static void fmt_fixed(mcs_buf_t* b, mcs_value_t v, int dec, bool group, int minint) {
    char tmp[400];
#if MCS_ENABLE_FLOAT
    if (v.type == MCS_T_FLOAT) snprintf(tmp, sizeof tmp, "%.*f", dec, as_double(v));
    else
#endif
    {
        char it[24]; mcs_format_int(it, v.as.i);
        snprintf(tmp, sizeof tmp, "%s", it);
        if (dec > 0) { size_t l = strlen(tmp); tmp[l++] = '.'; for (int i = 0; i < dec && l < sizeof tmp - 1; i++) tmp[l++] = '0'; tmp[l] = 0; }
    }
    const char* s = tmp;
    if (*s == '-') {
        /* avoid "-0.00" */
        bool allzero = true;
        for (const char* q = s + 1; *q; q++) if (*q >= '1' && *q <= '9') allzero = false;
        if (!allzero) mcs_buf_putc(b, '-');
        s++;
    }
    const char* dot = strchr(s, '.');
    size_t ilen = dot ? (size_t)(dot - s) : strlen(s);
    if (ilen == 1 && s[0] == '0' && minint == 0) { s++; ilen = 0; }
    for (int i = (int)ilen; i < minint; i++) mcs_buf_putc(b, '0');
    if (group) group_digits(b, s, ilen); else mcs_buf_putn(b, s, ilen);
    if (dot) mcs_buf_puts(b, dot);
}

static bool format_core(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, const char* f, size_t n) {
    if (n == 0 || !(v.type == MCS_T_INT || v.type == MCS_T_FLOAT || v.type == MCS_T_CHAR)) {
        if (v.type == MCS_T_OBJ && n > 0 && (OBJ_KIND(v) == MCS_O_INSTANCE)) {
            /* call ToString(format) if defined */
            mcs_class_t* cls = mcs_class_of(vm, v);
            mcs_value_t m;
            if (mcs_cls_get(vm, cls, MCS_TAB_METHODS, vm->s_tostring, &m)) {
                mcs_value_t arg = OBJ_VAL(mcs_intern(vm, f, n)), r;
                if (IS_KIND(m, MCS_O_CLOSURE) && AS_CLOSURE(m)->fn->arity == 1) {
                    if (mcs_call_internal(vm, m, v, 1, &arg, &r) != MCS_OK) return false;
                    return mcs_value_to_buf(vm, b, r);
                }
            }
        }
        return mcs_value_to_buf(vm, b, v);
    }
    char kind = f[0];
    int prec = -1;
    if (n > 1) {
        bool alldig = true;
        for (size_t i = 1; i < n; i++) if (f[i] < '0' || f[i] > '9') alldig = false;
        if (alldig) prec = atoi(f + 1); else kind = 0;
    }
    if (n == 1 || prec >= 0) {
        switch (kind) {
        case 'F': case 'f': fmt_fixed(b, v, prec < 0 ? 2 : prec, false, 1); return true;
        case 'N': case 'n': fmt_fixed(b, v, prec < 0 ? 2 : prec, true, 1); return true;
        case 'D': case 'd': {
            if (v.type == MCS_T_FLOAT) break;
            char it[24]; mcs_format_int(it, v.as.i);
            const char* s = it;
            if (*s == '-') { mcs_buf_putc(b, '-'); s++; }
            for (int i = (int)strlen(s); i < prec; i++) mcs_buf_putc(b, '0');
            mcs_buf_puts(b, s);
            return true;
        }
        case 'X': case 'x': {
            if (v.type == MCS_T_FLOAT) break;
            char hx[24];
            const char* digs = kind == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            mcs_uint_t u = (mcs_uint_t)v.as.i;
            int k = 0;
            do { hx[k++] = digs[u & 15]; u >>= 4; } while (u);
            for (int i = k; i < prec; i++) mcs_buf_putc(b, '0');
            while (k) mcs_buf_putc(b, hx[--k]);
            return true;
        }
#if MCS_ENABLE_FLOAT
        case 'E': case 'e': {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%.*e", prec < 0 ? 6 : prec, as_double(v));
            char* e = strchr(tmp, 'e');
            if (e) {
                *e = 0; mcs_buf_puts(b, tmp);
                int ex = atoi(e + 1);
                char eb[16]; snprintf(eb, sizeof eb, "%c%c%03d", kind, ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
                mcs_buf_puts(b, eb);
            } else mcs_buf_puts(b, tmp);
            return true;
        }
        case 'P': case 'p': {
            mcs_value_t x = mcs_float((mcs_float_t)(as_double(v) * 100.0));
            fmt_fixed(b, x, prec < 0 ? 2 : prec, true, 1);
            mcs_buf_putc(b, '%');
            return true;
        }
#endif
        case 'G': case 'g': case 'R': case 'r': return mcs_value_to_buf(vm, b, v);
        default: break;
        }
    }
    /* custom numeric format: 0, #, '.', ',' */
    int minint = 0, dec = 0, optdec = 0; bool group = false, seen_dot = false, ok = true;
    for (size_t i = 0; i < n; i++) {
        char c = f[i];
        if (c == '0') { if (seen_dot) dec++; else minint++; }
        else if (c == '#') { if (seen_dot) optdec++; }
        else if (c == '.') seen_dot = true;
        else if (c == ',') { if (!seen_dot) group = true; }
        else { ok = false; break; }
    }
    if (!ok) return mcs_value_to_buf(vm, b, v);
    mcs_buf_t t; mcs_buf_init(&t, vm);
    fmt_fixed(&t, v, dec + optdec, group, minint);
    if (optdec && t.data) {
        /* trim optional zeros */
        size_t len = t.len, keep = 0;
        char* dot = strchr(t.data, '.');
        if (dot) {
            keep = (size_t)(dot - t.data) + 1 + (size_t)dec;
            while (len > keep && t.data[len - 1] == '0') len--;
            if (len == (size_t)(dot - t.data) + 1) len--;
        }
        t.len = len;
    }
    if (t.data) mcs_buf_putn(b, t.data, t.len);
    mcs_buf_free(&t);
    return true;
}

/* spec: [,align][:format]  or a bare format string */
bool mcs_format_spec(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, const char* spec, size_t len) {
    int align = 0;
    const char* f = spec; size_t fn = len;
    if (len && spec[0] == ',') {
        size_t i = 1; bool neg = false;
        if (i < len && spec[i] == '-') { neg = true; i++; }
        while (i < len && spec[i] >= '0' && spec[i] <= '9') { align = align * 10 + (spec[i] - '0'); i++; }
        if (neg) align = -align;
        f = spec + i; fn = len - i;
    }
    if (fn && f[0] == ':') { f++; fn--; }
    if (align == 0) return format_core(vm, b, v, f, fn);
    mcs_buf_t t; mcs_buf_init(&t, vm);
    if (!format_core(vm, &t, v, f, fn)) { mcs_buf_free(&t); return false; }
    /* count UTF-8 characters */
    size_t chars = 0;
    for (size_t i = 0; i < t.len; i++) if (((uint8_t)t.data[i] & 0xC0) != 0x80) chars++;
    int pad = (align < 0 ? -align : align) - (int)chars;
    if (align > 0) for (int i = 0; i < pad; i++) mcs_buf_putc(b, ' ');
    if (t.data) mcs_buf_putn(b, t.data, t.len);
    if (align < 0) for (int i = 0; i < pad; i++) mcs_buf_putc(b, ' ');
    mcs_buf_free(&t);
    return true;
}

bool mcs_format_string(mcs_vm_t* vm, mcs_buf_t* b, const char* fmt, size_t flen, int argc, mcs_value_t* argv) {
    size_t i = 0;
    while (i < flen) {
        char c = fmt[i];
        if (c == '{' && i + 1 < flen && fmt[i + 1] == '{') { mcs_buf_putc(b, '{'); i += 2; continue; }
        if (c == '}' && i + 1 < flen && fmt[i + 1] == '}') { mcs_buf_putc(b, '}'); i += 2; continue; }
        if (c != '{') { mcs_buf_putc(b, c); i++; continue; }
        size_t j = i + 1;
        int idx = 0; bool any = false;
        while (j < flen && fmt[j] >= '0' && fmt[j] <= '9') { idx = idx * 10 + (fmt[j] - '0'); j++; any = true; }
        size_t spec = j;
        while (j < flen && fmt[j] != '}') j++;
        if (!any || j >= flen) { mcs_throw(vm, EXC_FORMAT, "Input string was not in a correct format."); return false; }
        if (idx >= argc) { mcs_throw(vm, EXC_FORMAT, "Index (zero based) must be greater than or equal to zero and less than the size of the argument list."); return false; }
        if (!mcs_format_spec(vm, b, argv[idx], fmt + spec, j - spec)) return false;
        i = j + 1;
    }
    return true;
}

/* ------------------------------------------------------------ public helpers */
bool mcs_is_string(mcs_value_t v) { return IS_STRING(v); }
int mcs_obj_kind(mcs_value_t v) { return IS_OBJ(v) ? (int)OBJ_KIND(v) : -1; }
mcs_value_t mcs_string(mcs_vm_t* vm, const char* s) { return OBJ_VAL(mcs_intern_c(vm, s)); }
mcs_value_t mcs_string_n(mcs_vm_t* vm, const char* s, size_t n) { return OBJ_VAL(mcs_intern(vm, s, n)); }
const char* mcs_cstr(mcs_value_t v) { return IS_STRING(v) ? AS_CSTR(v) : NULL; }
size_t mcs_strlen(mcs_value_t v) { return IS_STRING(v) ? AS_STRING(v)->len : 0; }

mcs_int_t mcs_to_int(mcs_vm_t* vm, mcs_value_t v) {
    if (v.type == MCS_T_INT || v.type == MCS_T_CHAR) return v.as.i;
#if MCS_ENABLE_FLOAT
    if (v.type == MCS_T_FLOAT) return (mcs_int_t)v.as.f;
#endif
    if (v.type == MCS_T_BOOL) return v.as.b ? 1 : 0;
    mcs_throw(vm, EXC_ARGUMENT, "Expected an integer but got %s", mcs_type_name(vm, v));
    return 0;
}
#if MCS_ENABLE_FLOAT
mcs_float_t mcs_to_float(mcs_vm_t* vm, mcs_value_t v) {
    if (v.type == MCS_T_FLOAT) return v.as.f;
    if (v.type == MCS_T_INT || v.type == MCS_T_CHAR) return (mcs_float_t)v.as.i;
    mcs_throw(vm, EXC_ARGUMENT, "Expected a number but got %s", mcs_type_name(vm, v));
    return 0;
}
#endif
bool mcs_to_bool(mcs_vm_t* vm, mcs_value_t v) {
    if (v.type == MCS_T_BOOL) return v.as.b;
    if (v.type == MCS_T_INT) return v.as.i != 0;
    mcs_throw(vm, EXC_ARGUMENT, "Expected a bool but got %s", mcs_type_name(vm, v));
    return false;
}
const char* mcs_to_cstr(mcs_vm_t* vm, mcs_value_t v) {
    if (IS_STRING(v)) return AS_CSTR(v);
    if (v.type == MCS_T_NULL) return "";
    mcs_throw(vm, EXC_ARGUMENT, "Expected a string but got %s", mcs_type_name(vm, v));
    return "";
}
mcs_value_t mcs_tostring(mcs_vm_t* vm, mcs_value_t v) {
    mcs_string_t* s = mcs_value_to_string(vm, v);
    return s ? OBJ_VAL(s) : mcs_null();
}

mcs_value_t mcs_new_array(mcs_vm_t* vm, uint32_t len) { return OBJ_VAL(mcs_new_listobj(vm, MCS_O_ARRAY, len)); }
mcs_value_t mcs_new_list(mcs_vm_t* vm) { return OBJ_VAL(mcs_new_listobj(vm, MCS_O_LIST, 0)); }
uint32_t mcs_len(mcs_value_t v) {
    if (IS_KIND(v, MCS_O_ARRAY) || IS_KIND(v, MCS_O_LIST)) return AS_LIST(v)->count;
    if (IS_STRING(v)) return AS_STRING(v)->len;
    if (IS_KIND(v, MCS_O_DICT)) return AS_DICT(v)->count;
    return 0;
}
mcs_value_t mcs_index(mcs_value_t v, uint32_t i) { return AS_LIST(v)->items[i]; }
void mcs_set_index(mcs_value_t v, uint32_t i, mcs_value_t x) { AS_LIST(v)->items[i] = x; }
void mcs_list_add(mcs_vm_t* vm, mcs_value_t list, mcs_value_t x) { mcs_listobj_push(vm, AS_LIST(list), x); }
void* mcs_userdata(mcs_value_t v) { return IS_KIND(v, MCS_O_USERDATA) ? (void*)AS_UDATA(v)->data : NULL; }
void* mcs_check_userdata(mcs_vm_t* vm, mcs_value_t v, const mcs_class_def_t* def) {
    if (IS_KIND(v, MCS_O_USERDATA)) {
        for (mcs_class_t* c = AS_UDATA(v)->cls; c; c = c->super) if (c->def == def) return AS_UDATA(v)->data;
    }
    mcs_throw(vm, EXC_INVCAST, "Expected %s but got %s", def->name, mcs_type_name(vm, v));
    return NULL;
}
