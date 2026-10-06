/* MicroCS - System.String and StringBuilder.
 * Strings are immutable, interned UTF-8; indices/lengths are byte based. */
#include "mcs_lib.h"

#define SELF_STR() if (!IS_STRING(self)) { mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object."); return mcs_null(); } \
    mcs_string_t* s = AS_STRING(self)

/* text of a char or string argument */
typedef struct { const char* p; size_t n; char tmp[4]; } piece_t;
static bool piece_of(mcs_vm_t* vm, mcs_value_t v, piece_t* pc, const char* param) {
    if (IS_STRING(v)) { pc->p = AS_CSTR(v); pc->n = AS_STRING(v)->len; return true; }
    if (v.type == MCS_T_CHAR) {
        uint32_t c = (uint32_t)v.as.i;
        mcs_buf_t b; mcs_buf_init(&b, vm);
        mcs_buf_utf8(&b, c);
        memcpy(pc->tmp, b.data, b.len); pc->n = b.len; pc->p = pc->tmp;
        mcs_buf_free(&b);
        return true;
    }
    if (v.type == MCS_T_NULL) mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter '%s')", param);
    else mcs_throw(vm, EXC_ARGUMENT, "Expected a string or char for '%s', got %s", param, mcs_type_name(vm, v));
    return false;
}

static long find_bytes(const char* h, size_t hn, const char* nd, size_t nn, size_t from) {
    if (nn == 0) return from <= hn ? (long)from : -1;
    if (nn > hn) return -1;
    for (size_t i = from; i + nn <= hn; i++)
        if (h[i] == nd[0] && memcmp(h + i, nd, nn) == 0) return (long)i;
    return -1;
}
static long rfind_bytes(const char* h, size_t hn, const char* nd, size_t nn) {
    if (nn > hn) return -1;
    for (size_t i = hn - nn + 1; i-- > 0;) if (memcmp(h + i, nd, nn) == 0) return (long)i;
    return -1;
}

static bool get_index(mcs_vm_t* vm, mcs_value_t v, size_t max, size_t* out, const char* param) {
    mcs_int_t i = mcs_to_int(vm, v);
    if (vm->has_exc) return false;
    if (i < 0 || (mcs_uint_t)i > max) { mcs_throw(vm, EXC_ARGRANGE, "Index and length must refer to a location within the string. (Parameter '%s')", param); return false; }
    *out = (size_t)i;
    return true;
}

/* ---------------------------------------------------------------- statics */
NATIVE(str_new) { /* new string(char, count) / new string(char[]) */
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (argc == 2 && argv[0].type == MCS_T_CHAR) {
        mcs_int_t n = mcs_to_int(vm, argv[1]);
        if (vm->has_exc) { mcs_buf_free(&b); return mcs_null(); }
        if (n < 0) { mcs_buf_free(&b); mcs_throw(vm, EXC_ARGRANGE, "Count cannot be less than zero. (Parameter 'count')"); return mcs_null(); }
        for (mcs_int_t i = 0; i < n; i++) mcs_buf_utf8(&b, (uint32_t)argv[0].as.i);
    } else if (argc >= 1 && lib_is_seq(argv[0])) {
        mcs_list_t* l = AS_LIST(argv[0]);
        size_t start = 0, len = l->count;
        if (argc == 3) { start = (size_t)mcs_to_int(vm, argv[1]); len = (size_t)mcs_to_int(vm, argv[2]); }
        if (start > l->count || len > l->count - start) { mcs_buf_free(&b); mcs_throw(vm, EXC_ARGRANGE, "Index was out of range."); return mcs_null(); }
        for (size_t i = start; i < start + len; i++) {
            if (!mcs_value_to_buf(vm, &b, l->items[i])) { mcs_buf_free(&b); return mcs_null(); }
        }
    } else if (argc == 0) {
    } else { mcs_buf_free(&b); mcs_throw(vm, EXC_ARGUMENT, "No matching String constructor"); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}

NATIVE(str_isnullorempty) { return mcs_bool(!IS_STRING(argv[0]) || AS_STRING(argv[0])->len == 0); }
NATIVE(str_isnullorwhite) {
    if (!IS_STRING(argv[0])) return mcs_bool(true);
    mcs_string_t* s = AS_STRING(argv[0]);
    for (uint32_t i = 0; i < s->len; i++) { char c = s->chars[i]; if (!(c == ' ' || (c >= 9 && c <= 13))) return mcs_bool(false); }
    return mcs_bool(true);
}

static bool append_seq(mcs_vm_t* vm, mcs_buf_t* b, const char* sep, size_t sn, mcs_value_t* items, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (i && sn) mcs_buf_putn(b, sep, sn);
        if (!mcs_value_to_buf(vm, b, items[i])) return false;
    }
    return true;
}
NATIVE(str_join) {
    ARGN(1);
    piece_t sep = { "", 0, {0} };
    if (argv[0].type != MCS_T_NULL && !piece_of(vm, argv[0], &sep, "separator")) return mcs_null();
    if (argc == 2) lib_seq_arg(vm, &argv[1], false);
    mcs_buf_t b; mcs_buf_init(&b, vm);
    bool ok;
    if (argc == 2 && lib_is_seq(argv[1])) ok = append_seq(vm, &b, sep.p, sep.n, AS_LIST(argv[1])->items, AS_LIST(argv[1])->count);
    else if (argc == 2 && IS_KIND(argv[1], MCS_O_DICT)) {
        mcs_dict_t* d = AS_DICT(argv[1]);
        ok = true;
        for (uint32_t i = 0; i < d->count && ok; i++) {
            if (i) mcs_buf_putn(&b, sep.p, sep.n);
            mcs_buf_putc(&b, '[');
            ok = mcs_value_to_buf(vm, &b, d->keys[i]);
            mcs_buf_puts(&b, ", ");
            ok = ok && mcs_value_to_buf(vm, &b, DICT_VAL(d, i));
            mcs_buf_putc(&b, ']');
        }
    } else ok = append_seq(vm, &b, sep.p, sep.n, argv + 1, (uint32_t)(argc - 1));
    if (!ok) { mcs_buf_free(&b); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(str_concat) {
    if (argc == 1) lib_seq_arg(vm, &argv[0], false);
    mcs_buf_t b; mcs_buf_init(&b, vm);
    bool ok = (argc == 1 && lib_is_seq(argv[0])) ? append_seq(vm, &b, "", 0, AS_LIST(argv[0])->items, AS_LIST(argv[0])->count)
                                                 : append_seq(vm, &b, "", 0, argv, (uint32_t)argc);
    if (!ok) { mcs_buf_free(&b); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(str_format) {
    mcs_string_t* f = lib_need_str(vm, argv[0], "format"); CHECK();
    int n = argc - 1; mcs_value_t* args = argv + 1;
    if (n == 1 && IS_KIND(args[0], MCS_O_ARRAY)) { n = (int)AS_LIST(args[0])->count; args = AS_LIST(args[0])->items; }
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (!mcs_format_string(vm, &b, f->chars, f->len, n, args)) { mcs_buf_free(&b); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
static int lower_c(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int cmp_str(mcs_value_t a, mcs_value_t b, bool ic) {
    if (!IS_STRING(a)) return IS_STRING(b) ? -1 : 0;
    if (!IS_STRING(b)) return 1;
    const unsigned char* x = (const unsigned char*)AS_CSTR(a);
    const unsigned char* y = (const unsigned char*)AS_CSTR(b);
    for (;; x++, y++) {
        int cx = ic ? lower_c(*x) : *x, cy = ic ? lower_c(*y) : *y;
        if (cx != cy || !cx) return cx < cy ? -1 : cx > cy;
    }
}
NATIVE(str_compare) { ARGN(2); return mcs_int(cmp_str(argv[0], argv[1], argc > 2 && mcs_truthy(argv[2]))); }
NATIVE(str_equals_static) { ARGN(2); return mcs_bool(mcs_values_same(argv[0], argv[1])); }

/* --------------------------------------------------------------- instance */
NATIVE(str_length) { SELF_STR(); return mcs_int((mcs_int_t)s->len); }
NATIVE(str_substring) {
    SELF_STR();
    size_t start, len;
    if (!get_index(vm, argv[0], s->len, &start, "startIndex")) return mcs_null();
    if (argc > 1) { if (!get_index(vm, argv[1], s->len - start, &len, "length")) return mcs_null(); }
    else len = s->len - start;
    return lib_str(vm, s->chars + start, len);
}
NATIVE(str_indexof) {
    SELF_STR(); ARGN(1);
    piece_t p; if (!piece_of(vm, argv[0], &p, "value")) return mcs_null();
    size_t from = 0;
    if (argc > 1 && !get_index(vm, argv[1], s->len, &from, "startIndex")) return mcs_null();
    return mcs_int(find_bytes(s->chars, s->len, p.p, p.n, from));
}
NATIVE(str_lastindexof) {
    SELF_STR(); ARGN(1);
    piece_t p; if (!piece_of(vm, argv[0], &p, "value")) return mcs_null();
    return mcs_int(rfind_bytes(s->chars, s->len, p.p, p.n));
}
NATIVE(str_indexofany) {
    SELF_STR();
    if (!lib_is_seq(argv[0])) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'anyOf')"); return mcs_null(); }
    mcs_list_t* l = AS_LIST(argv[0]);
    for (uint32_t i = 0; i < s->len; i++)
        for (uint32_t k = 0; k < l->count; k++)
            if (is_intlike_v(l->items[k]) && (uint8_t)s->chars[i] == (uint8_t)l->items[k].as.i) return mcs_int((mcs_int_t)i);
    return mcs_int(-1);
}
NATIVE(str_contains) {
    SELF_STR();
    piece_t p; if (!piece_of(vm, argv[0], &p, "value")) return mcs_null();
    return mcs_bool(find_bytes(s->chars, s->len, p.p, p.n, 0) >= 0);
}
NATIVE(str_startswith) {
    SELF_STR();
    piece_t p; if (!piece_of(vm, argv[0], &p, "value")) return mcs_null();
    return mcs_bool(p.n <= s->len && memcmp(s->chars, p.p, p.n) == 0);
}
NATIVE(str_endswith) {
    SELF_STR();
    piece_t p; if (!piece_of(vm, argv[0], &p, "value")) return mcs_null();
    return mcs_bool(p.n <= s->len && memcmp(s->chars + s->len - p.n, p.p, p.n) == 0);
}
NATIVE(str_replace) {
    SELF_STR(); ARGN(2);
    piece_t a, b;
    if (!piece_of(vm, argv[0], &a, "oldValue")) return mcs_null();
    if (argv[1].type == MCS_T_NULL) { b.p = ""; b.n = 0; }
    else if (!piece_of(vm, argv[1], &b, "newValue")) return mcs_null();
    if (a.n == 0) { mcs_throw(vm, EXC_ARGUMENT, "String cannot be of zero length. (Parameter 'oldValue')"); return mcs_null(); }
    mcs_buf_t o; mcs_buf_init(&o, vm);
    size_t i = 0;
    for (;;) {
        long k = find_bytes(s->chars, s->len, a.p, a.n, i);
        if (k < 0) break;
        mcs_buf_putn(&o, s->chars + i, (size_t)k - i);
        mcs_buf_putn(&o, b.p, b.n);
        i = (size_t)k + a.n;
    }
    mcs_buf_putn(&o, s->chars + i, s->len - i);
    return OBJ_VAL(mcs_buf_to_string(&o));
}
static bool trim_char(int argc, mcs_value_t* argv, unsigned char c) {
    if (argc == 0) return c == ' ' || (c >= 9 && c <= 13);
    for (int i = 0; i < argc; i++) {
        if (is_intlike_v(argv[i]) && (unsigned char)argv[i].as.i == c) return true;
        if (lib_is_seq(argv[i])) {
            mcs_list_t* l = AS_LIST(argv[i]);
            for (uint32_t k = 0; k < l->count; k++) if (is_intlike_v(l->items[k]) && (unsigned char)l->items[k].as.i == c) return true;
        }
    }
    return false;
}
static mcs_value_t do_trim(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, bool left, bool right) {
    SELF_STR();
    size_t a = 0, b = s->len;
    if (left) while (a < b && trim_char(argc, argv, (unsigned char)s->chars[a])) a++;
    if (right) while (b > a && trim_char(argc, argv, (unsigned char)s->chars[b - 1])) b--;
    if (a == 0 && b == s->len) return self;
    return lib_str(vm, s->chars + a, b - a);
}
NATIVE(str_trim) { return do_trim(vm, self, argc, argv, true, true); }
NATIVE(str_trimstart) { return do_trim(vm, self, argc, argv, true, false); }
NATIVE(str_trimend) { return do_trim(vm, self, argc, argv, false, true); }
static mcs_value_t map_case(mcs_vm_t* vm, mcs_value_t self, bool upper) {
    SELF_STR();
    mcs_buf_t b; mcs_buf_init(&b, vm);
    mcs_buf_putn(&b, s->chars, s->len);
    for (size_t i = 0; i < b.len; i++) {
        char c = b.data[i];
        if (upper && c >= 'a' && c <= 'z') b.data[i] = (char)(c - 32);
        else if (!upper && c >= 'A' && c <= 'Z') b.data[i] = (char)(c + 32);
    }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(str_toupper) { return map_case(vm, self, true); }
NATIVE(str_tolower) { return map_case(vm, self, false); }
static mcs_value_t do_pad(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, bool left) {
    SELF_STR(); ARGN(1);
    mcs_int_t w = mcs_to_int(vm, argv[0]); CHECK();
    if (w < 0) { mcs_throw(vm, EXC_ARGRANGE, "Non-negative number required. (Parameter 'totalWidth')"); return mcs_null(); }
    uint32_t pc = argc > 1 && is_intlike_v(argv[1]) ? (uint32_t)argv[1].as.i : ' ';
    if ((mcs_uint_t)w <= s->len) return self;
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (!left) mcs_buf_putn(&b, s->chars, s->len);
    for (mcs_int_t i = (mcs_int_t)s->len; i < w; i++) mcs_buf_utf8(&b, pc);
    if (left) mcs_buf_putn(&b, s->chars, s->len);
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(str_padleft) { return do_pad(vm, self, argc, argv, true); }
NATIVE(str_padright) { return do_pad(vm, self, argc, argv, false); }

NATIVE(str_split) {
    SELF_STR();
    /* separators: chars, strings, arrays of either; trailing int = options/count */
    piece_t seps[16]; int ns = 0;
    int options = 0; mcs_int_t max_count = -1;
    for (int i = 0; i < argc; i++) {
        mcs_value_t v = argv[i];
        if (v.type == MCS_T_INT) { if (i == argc - 1) options = (int)v.as.i; else max_count = v.as.i; continue; }
        if (lib_is_seq(v)) {
            mcs_list_t* l = AS_LIST(v);
            for (uint32_t k = 0; k < l->count && ns < 16; k++) if (!piece_of(vm, l->items[k], &seps[ns++], "separator")) return mcs_null();
        } else if (v.type != MCS_T_NULL && ns < 16) { if (!piece_of(vm, v, &seps[ns++], "separator")) return mcs_null(); }
    }
    vm->gc_pause++;
    mcs_list_t* out = mcs_new_listobj(vm, MCS_O_ARRAY, 0);
    size_t start = 0, i = 0;
    while (i <= s->len) {
        int hit = -1;
        if (i < s->len && (max_count < 0 || (mcs_int_t)out->count < max_count - 1)) {
            if (ns == 0) { char c = s->chars[i]; if (c == ' ' || (c >= 9 && c <= 13)) hit = 0; }
            else for (int k = 0; k < ns; k++) if (seps[k].n && i + seps[k].n <= s->len && memcmp(s->chars + i, seps[k].p, seps[k].n) == 0) { hit = k; break; }
        }
        if (hit >= 0 || i == s->len) {
            size_t a = start, b = i;
            if (options & 2) { while (a < b && (s->chars[a] == ' ' || (s->chars[a] >= 9 && s->chars[a] <= 13))) a++; while (b > a && (s->chars[b - 1] == ' ' || (s->chars[b - 1] >= 9 && s->chars[b - 1] <= 13))) b--; }
            if (!((options & 1) && a == b)) mcs_listobj_push(vm, out, lib_str(vm, s->chars + a, b - a));
            if (i == s->len) break;
            size_t adv = ns == 0 ? 1 : seps[hit].n;
            i += adv; start = i;
        } else i++;
    }
    vm->gc_pause--;
    return OBJ_VAL(out);
}
NATIVE(str_tochararray) {
    SELF_STR();
    vm->gc_pause++;
    mcs_list_t* out = mcs_new_listobj(vm, MCS_O_ARRAY, 0);
    for (uint32_t i = 0; i < s->len;) {
        uint8_t c0 = (uint8_t)s->chars[i]; uint32_t cp = c0; int extra = 0;
        if (c0 >= 0xF0) { cp = c0 & 7; extra = 3; } else if (c0 >= 0xE0) { cp = c0 & 15; extra = 2; } else if (c0 >= 0xC0) { cp = c0 & 31; extra = 1; }
        for (int k = 1; k <= extra && i + k < s->len; k++) cp = (cp << 6) | ((uint8_t)s->chars[i + k] & 0x3F);
        mcs_listobj_push(vm, out, mcs_char(cp));
        i += 1 + extra;
    }
    vm->gc_pause--;
    return OBJ_VAL(out);
}
NATIVE(str_insert) {
    SELF_STR(); ARGN(2);
    size_t at; if (!get_index(vm, argv[0], s->len, &at, "startIndex")) return mcs_null();
    piece_t p; if (!piece_of(vm, argv[1], &p, "value")) return mcs_null();
    mcs_buf_t b; mcs_buf_init(&b, vm);
    mcs_buf_putn(&b, s->chars, at); mcs_buf_putn(&b, p.p, p.n); mcs_buf_putn(&b, s->chars + at, s->len - at);
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(str_remove) {
    SELF_STR(); ARGN(1);
    size_t at, n;
    if (!get_index(vm, argv[0], s->len, &at, "startIndex")) return mcs_null();
    if (argc > 1) { if (!get_index(vm, argv[1], s->len - at, &n, "count")) return mcs_null(); }
    else n = s->len - at;
    mcs_buf_t b; mcs_buf_init(&b, vm);
    mcs_buf_putn(&b, s->chars, at); mcs_buf_putn(&b, s->chars + at + n, s->len - at - n);
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(str_compareto) { SELF_STR(); (void)s; return mcs_int(cmp_str(self, argv[0], false)); }
NATIVE(str_equals) {
    SELF_STR(); (void)s;
    if (argc > 1 && mcs_truthy(argv[1]) && argv[1].type == MCS_T_INT && (argv[1].as.i == 5 || argv[1].as.i == 3 || argv[1].as.i == 1))
        return mcs_bool(cmp_str(self, argv[0], true) == 0); /* StringComparison.*IgnoreCase */
    return mcs_bool(mcs_values_same(self, argv[0]));
}
NATIVE(str_getenum) { return self; }
NATIVE(str_clone) { return self; }

static const mcs_reg_t string_methods[] = {
    MCS_GET("Length", str_length),
    MCS_FN("Substring", str_substring, -1), MCS_FN("IndexOf", str_indexof, -1), MCS_FN("LastIndexOf", str_lastindexof, -1),
    MCS_FN("IndexOfAny", str_indexofany, 1), MCS_FN("Contains", str_contains, 1), MCS_FN("StartsWith", str_startswith, -1),
    MCS_FN("EndsWith", str_endswith, -1), MCS_FN("Replace", str_replace, 2), MCS_FN("Trim", str_trim, -1),
    MCS_FN("TrimStart", str_trimstart, -1), MCS_FN("TrimEnd", str_trimend, -1), MCS_FN("ToUpper", str_toupper, 0),
    MCS_FN("ToLower", str_tolower, 0), MCS_FN("ToUpperInvariant", str_toupper, 0), MCS_FN("ToLowerInvariant", str_tolower, 0),
    MCS_FN("PadLeft", str_padleft, -1), MCS_FN("PadRight", str_padright, -1), MCS_FN("Split", str_split, -1),
    MCS_FN("ToCharArray", str_tochararray, 0), MCS_FN("Insert", str_insert, 2), MCS_FN("Remove", str_remove, -1),
    MCS_FN("CompareTo", str_compareto, 1), MCS_FN("Equals", str_equals, 1), MCS_FN("Equals", str_equals, 2),
    MCS_FN("GetEnumerator", str_getenum, 0), MCS_FN("Clone", str_clone, 0),
    MCS_REG_END
};
static const mcs_reg_t string_statics[] = {
    MCS_FN("IsNullOrEmpty", str_isnullorempty, 1), MCS_FN("IsNullOrWhiteSpace", str_isnullorwhite, 1),
    MCS_FN("Join", str_join, -1), MCS_FN("Concat", str_concat, -1), MCS_FN("Format", str_format, -1),
    MCS_FN("Compare", str_compare, -1), MCS_FN("CompareOrdinal", str_compare, 2), MCS_FN("Equals", str_equals_static, 2),
    MCS_REG_END
};

/* ------------------------------------------------------------ StringBuilder */
#if MCS_ENABLE_STRINGBUILDER
static const mcs_class_def_t sb_def;
static void sb_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    mcs_buf_t* b = (mcs_buf_t*)mcs_userdata(self);
    mcs_buf_init(b, vm);
    if (argc >= 1 && IS_STRING(argv[0])) mcs_buf_putn(b, AS_CSTR(argv[0]), AS_STRING(argv[0])->len);
}
static void sb_final(mcs_vm_t* vm, void* data) { MCS_UNUSED(vm); mcs_buf_free((mcs_buf_t*)data); }
#define SB() mcs_buf_t* b = (mcs_buf_t*)mcs_check_userdata(vm, self, &sb_def); CHECK(); if (!b->vm) mcs_buf_init(b, vm)
NATIVE(sb_append) {
    SB();
    if (argc == 2 && argv[0].type == MCS_T_CHAR && argv[1].type == MCS_T_INT) {
        for (mcs_int_t i = 0; i < argv[1].as.i; i++) mcs_buf_utf8(b, (uint32_t)argv[0].as.i);
        return self;
    }
    for (int i = 0; i < argc; i++) if (!mcs_value_to_buf(vm, b, argv[i])) return mcs_null();
    return self;
}
NATIVE(sb_appendline) {
    SB();
    if (argc && !mcs_value_to_buf(vm, b, argv[0])) return mcs_null();
    mcs_buf_putc(b, '\n');
    return self;
}
NATIVE(sb_appendformat) {
    SB();
    mcs_string_t* f = lib_need_str(vm, argv[0], "format"); CHECK();
    int n = argc - 1; mcs_value_t* args = argv + 1;
    if (n == 1 && IS_KIND(args[0], MCS_O_ARRAY)) { n = (int)AS_LIST(args[0])->count; args = AS_LIST(args[0])->items; }
    if (!mcs_format_string(vm, b, f->chars, f->len, n, args)) return mcs_null();
    return self;
}
NATIVE(sb_tostring) {
    SB();
    if (argc == 2) {
        mcs_int_t a = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
        if (a < 0 || n < 0 || (size_t)(a + n) > b->len) { mcs_throw(vm, EXC_ARGRANGE, "Index was out of range."); return mcs_null(); }
        return lib_str(vm, b->data + a, (size_t)n);
    }
    return lib_str(vm, b->data ? b->data : "", b->len);
}
NATIVE(sb_clear) { SB(); b->len = 0; return self; }
NATIVE(sb_length) { SB(); return mcs_int((mcs_int_t)b->len); }
NATIVE(sb_setlength) {
    SB();
    mcs_int_t n = mcs_to_int(vm, argv[0]); CHECK();
    if (n < 0) { mcs_throw(vm, EXC_ARGRANGE, "Length cannot be less than zero."); return mcs_null(); }
    while (b->len < (size_t)n) mcs_buf_putc(b, 0);
    b->len = (size_t)n;
    return mcs_null();
}
NATIVE(sb_capacity) { SB(); return mcs_int((mcs_int_t)b->cap); }
NATIVE(sb_insert) {
    SB();
    mcs_int_t at = mcs_to_int(vm, argv[0]); CHECK();
    if (at < 0 || (size_t)at > b->len) { mcs_throw(vm, EXC_ARGRANGE, "Index was out of range."); return mcs_null(); }
    mcs_buf_t t; mcs_buf_init(&t, vm);
    if (!mcs_value_to_buf(vm, &t, argv[1])) { mcs_buf_free(&t); return mcs_null(); }
    size_t old = b->len;
    for (size_t i = 0; i < t.len; i++) mcs_buf_putc(b, 0);
    memmove(b->data + at + t.len, b->data + at, old - (size_t)at);
    memcpy(b->data + at, t.data, t.len);
    mcs_buf_free(&t);
    return self;
}
NATIVE(sb_remove) {
    SB();
    mcs_int_t at = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
    if (at < 0 || n < 0 || (size_t)(at + n) > b->len) { mcs_throw(vm, EXC_ARGRANGE, "Index was out of range."); return mcs_null(); }
    memmove(b->data + at, b->data + at + n, b->len - (size_t)(at + n));
    b->len -= (size_t)n;
    return self;
}
NATIVE(sb_replace) {
    SB();
    piece_t a, r;
    if (!piece_of(vm, argv[0], &a, "oldValue")) return mcs_null();
    if (argv[1].type == MCS_T_NULL) { r.p = ""; r.n = 0; } else if (!piece_of(vm, argv[1], &r, "newValue")) return mcs_null();
    if (!a.n) return self;
    mcs_buf_t o; mcs_buf_init(&o, vm);
    size_t i = 0;
    for (;;) {
        long k = find_bytes(b->data ? b->data : "", b->len, a.p, a.n, i);
        if (k < 0) break;
        mcs_buf_putn(&o, b->data + i, (size_t)k - i);
        mcs_buf_putn(&o, r.p, r.n);
        i = (size_t)k + a.n;
    }
    mcs_buf_putn(&o, b->data + i, b->len - i);
    mcs_buf_free(b);
    *b = o;
    return self;
}
NATIVE(sb_getitem) {
    SB();
    mcs_int_t i = mcs_to_int(vm, argv[0]); CHECK();
    if (i < 0 || (size_t)i >= b->len) { mcs_throw(vm, EXC_INDEX, "Index was outside the bounds of the array."); return mcs_null(); }
    return mcs_char((uint8_t)b->data[i]);
}
NATIVE(sb_setitem) {
    SB();
    mcs_int_t i = mcs_to_int(vm, argv[0]); CHECK();
    if (i < 0 || (size_t)i >= b->len) { mcs_throw(vm, EXC_INDEX, "Index was outside the bounds of the array."); return mcs_null(); }
    b->data[i] = (char)mcs_to_int(vm, argv[1]);
    return argv[1];
}
static const mcs_reg_t sb_members[] = {
    MCS_FN("Append", sb_append, -1), MCS_FN("AppendLine", sb_appendline, -1), MCS_FN("AppendFormat", sb_appendformat, -1),
    MCS_FN("ToString", sb_tostring, 0), MCS_FN("ToString", sb_tostring, 2), MCS_FN("Clear", sb_clear, 0),
    MCS_GET("Length", sb_length), MCS_SET("Length", sb_setlength), MCS_GET("Capacity", sb_capacity),
    MCS_FN("Insert", sb_insert, 2), MCS_FN("Remove", sb_remove, 2), MCS_FN("Replace", sb_replace, 2),
    MCS_FN("get_Item", sb_getitem, 1), MCS_FN("set_Item", sb_setitem, 2),
    MCS_REG_END
};
static const mcs_class_def_t sb_def = { "StringBuilder", sizeof(mcs_buf_t), sb_ctor, sb_final, sb_members, NULL };
#endif

void mcs_lib_open_string(mcs_vm_t* vm, uint8_t mask) {
    vm->cls_string = mcs_define_builtin_class(vm, "String", vm->cls_object, CLS_BUILTIN);
    vm->cls_string->native_ctor = str_new;
    mcs_add_regs(vm, vm->cls_string, string_methods, false);
    mcs_add_regs(vm, vm->cls_string, string_statics, true);
    vm->gc_pause++;
    mcs_table_set(vm, &vm->cls_string->statics, lib_cstr(vm, "Empty"), lib_cstr(vm, ""));
    vm->gc_pause--;
    mcs_module_set(vm, "StringSplitOptions", "None", mcs_int(0));
    mcs_module_set(vm, "StringSplitOptions", "RemoveEmptyEntries", mcs_int(1));
    mcs_module_set(vm, "StringSplitOptions", "TrimEntries", mcs_int(2));
    mcs_module_set(vm, "StringComparison", "Ordinal", mcs_int(4));
    mcs_module_set(vm, "StringComparison", "OrdinalIgnoreCase", mcs_int(5));
    mcs_module_set(vm, "StringComparison", "CurrentCultureIgnoreCase", mcs_int(1));
    mcs_module_set(vm, "StringComparison", "InvariantCultureIgnoreCase", mcs_int(3));
#if MCS_ENABLE_STRINGBUILDER
    if (mask & MCS_LIB_TEXT) mcs_register_class(vm, &sb_def);
#else
    MCS_UNUSED(mask);
#endif
}
