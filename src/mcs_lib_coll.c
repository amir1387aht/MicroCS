/* MicroCS - arrays, List<T>, Dictionary<K,V>, HashSet<T>, Stack<T>,
 * Queue<T> and LINQ-style sequence operators (Where, Select, OrderBy...). */
#include "mcs_lib.h"
#if (!MCS_ENABLE_LIST || !MCS_ENABLE_DICT) && defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#endif

#define SELF_SEQ() if (!lib_is_seq(self)) { mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object."); return mcs_null(); } \
    mcs_list_t* l = AS_LIST(self)
#define FAIL_POP() do { mcs_pop_root(vm, 1); return mcs_null(); } while (0)

static mcs_list_t* new_rooted(mcs_vm_t* vm, uint8_t kind, uint32_t n) {
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, kind, n);
    mcs_push_root(vm, OBJ_VAL(o));
    vm->gc_pause--;
    return o;
}
static mcs_value_t done(mcs_vm_t* vm, mcs_list_t* o) { mcs_pop_root(vm, 1); return OBJ_VAL(o); }

static bool is_fn(mcs_value_t v) {
    return IS_OBJ(v) && (OBJ_KIND(v) == MCS_O_CLOSURE || OBJ_KIND(v) == MCS_O_NATIVE || OBJ_KIND(v) == MCS_O_BOUND || OBJ_KIND(v) == MCS_O_OVERLOADS);
}
static bool need_fn(mcs_vm_t* vm, int argc, mcs_value_t* argv, int i, const char* what) {
    if (argc <= i || !is_fn(argv[i])) {
        mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter '%s')", what);
        return false;
    }
    return true;
}
/* invoke a selector/predicate; passes (item, index) to 2-parameter lambdas */
static bool cb(mcs_vm_t* vm, mcs_value_t fn, mcs_value_t item, uint32_t idx, mcs_value_t* out) {
    mcs_value_t args[2] = { item, mcs_int((mcs_int_t)idx) };
    int n = (IS_KIND(fn, MCS_O_CLOSURE) && AS_CLOSURE(fn)->fn->arity == 2) ? 2 : 1;
    return lib_call(vm, fn, n, args, out);
}
static bool check_range(mcs_vm_t* vm, mcs_int_t start, mcs_int_t count, uint32_t len) {
    if (start < 0 || count < 0 || (mcs_uint_t)start > len || (mcs_uint_t)count > len - (mcs_uint_t)start) {
        mcs_throw(vm, EXC_ARGRANGE, "Offset and length were out of bounds for the array or count is greater than the number of elements from index to the end of the source collection.");
        return false;
    }
    return true;
}
static void no_elements(mcs_vm_t* vm) { mcs_throw(vm, EXC_INVOP, "Sequence contains no elements"); }

/* ------------------------------------------------------------ sorting */
static int cmp2(mcs_vm_t* vm, mcs_value_t cmp, mcs_value_t a, mcs_value_t b, bool* ok) {
    if (cmp.type != MCS_T_NULL) {
        mcs_value_t args[2] = { a, b }, r;
        if (!lib_call(vm, cmp, 2, args, &r)) { *ok = false; return 0; }
        *ok = true;
        return r.type == MCS_T_INT ? (r.as.i < 0 ? -1 : r.as.i > 0) : 0;
    }
    return mcs_compare_values(vm, a, b, ok);
}
/* composite keys (ThenBy): each key is an array; spec holds [sel, desc] pairs */
static int cmp_multi(mcs_vm_t* vm, mcs_list_t* spec, mcs_value_t a, mcs_value_t b, bool* ok) {
    mcs_list_t *ka = AS_LIST(a), *kb = AS_LIST(b);
    for (uint32_t i = 0; i < ka->count; i++) {
        int c = mcs_compare_values(vm, ka->items[i], kb->items[i], ok);
        if (!*ok) return 0;
        if (mcs_truthy(spec->items[2 * i + 1])) c = -c;
        if (c) return c;
    }
    return 0;
}
static mcs_list_t* g_multi_spec; /* set only during a ThenBy sort */
/* stable bottom-up merge sort of index array by keys[] */
static bool sort_indices(mcs_vm_t* vm, const mcs_value_t* keys, uint32_t n, mcs_value_t cmp, bool desc, uint32_t* idx) {
    for (uint32_t i = 0; i < n; i++) idx[i] = i;
    if (n < 2) return true;
    uint32_t* tmp = MCS_ALLOC(vm, uint32_t, n);
    uint32_t *src = idx, *dst = tmp;
    bool ok = true;
    for (uint32_t w = 1; w < n && ok; w *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * w) {
            uint32_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
            uint32_t a = lo, b = mid, k = lo;
            while (a < mid && b < hi) {
                int c = 0;
                if (ok) { c = g_multi_spec ? cmp_multi(vm, g_multi_spec, keys[src[a]], keys[src[b]], &ok) : cmp2(vm, cmp, keys[src[a]], keys[src[b]], &ok); if (desc) c = -c; }
                dst[k++] = (c <= 0) ? src[a++] : src[b++];
            }
            while (a < mid) dst[k++] = src[a++];
            while (b < hi) dst[k++] = src[b++];
        }
        uint32_t* t = src; src = dst; dst = t;
    }
    if (src != idx) memcpy(idx, src, sizeof(uint32_t) * n);
    MCS_FREE(vm, uint32_t, tmp, n);
    return ok;
}
/* sort items[start..start+n) in place (values stay reachable via `keys`) */
static bool sort_values(mcs_vm_t* vm, mcs_list_t* l, uint32_t start, uint32_t n, mcs_value_t cmp, bool desc) {
    if (n < 2) return true;
    mcs_list_t* keys = new_rooted(vm, MCS_O_ARRAY, n);
    memcpy(keys->items, l->items + start, sizeof(mcs_value_t) * n);
    uint32_t* idx = MCS_ALLOC(vm, uint32_t, n);
    bool ok = sort_indices(vm, keys->items, n, cmp, desc, idx);
    if (ok && start + n <= l->count)
        for (uint32_t i = 0; i < n; i++) l->items[start + i] = keys->items[idx[i]];
    MCS_FREE(vm, uint32_t, idx, n);
    mcs_pop_root(vm, 1);
    return ok;
}

/* ------------------------------------------------------------ sequence ops */
NATIVE(seq_getenum) { return self; }
NATIVE(seq_contains) {
    SELF_SEQ();
    for (uint32_t i = 0; i < l->count; i++) { if (lib_equals(vm, l->items[i], argv[0])) return mcs_bool(true); CHECK(); }
    return mcs_bool(false);
}
NATIVE(seq_indexof) {
    SELF_SEQ(); ARGN(1);
    uint32_t from = 0;
    if (argc > 1) { mcs_int_t f = mcs_to_int(vm, argv[1]); CHECK(); if (f < 0 || (mcs_uint_t)f > l->count) { mcs_throw(vm, EXC_ARGRANGE, "Index was out of range."); return mcs_null(); } from = (uint32_t)f; }
    for (uint32_t i = from; i < l->count; i++) { if (lib_equals(vm, l->items[i], argv[0])) return mcs_int((mcs_int_t)i); CHECK(); }
    return mcs_int(-1);
}
NATIVE(seq_lastindexof) {
    SELF_SEQ();
    for (uint32_t i = l->count; i-- > 0;) { if (lib_equals(vm, l->items[i], argv[0])) return mcs_int((mcs_int_t)i); CHECK(); }
    return mcs_int(-1);
}
NATIVE(seq_where) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "predicate")) return mcs_null();
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, 0);
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t it = l->items[i], r;
        if (!cb(vm, argv[0], it, i, &r)) FAIL_POP();
        if (mcs_truthy(r)) mcs_listobj_push(vm, o, it);
    }
    return done(vm, o);
}
NATIVE(seq_select) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "selector")) return mcs_null();
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, 0);
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t r;
        if (!cb(vm, argv[0], l->items[i], i, &r)) FAIL_POP();
        mcs_listobj_push(vm, o, r);
    }
    return done(vm, o);
}
NATIVE(seq_selectmany) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "selector")) return mcs_null();
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, 0);
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t r;
        if (!cb(vm, argv[0], l->items[i], i, &r)) FAIL_POP();
        if (lib_is_seq(r)) for (uint32_t k = 0; k < AS_LIST(r)->count; k++) mcs_listobj_push(vm, o, AS_LIST(r)->items[k]);
    }
    return done(vm, o);
}
NATIVE(seq_any) {
    SELF_SEQ();
    if (argc == 0) return mcs_bool(l->count > 0);
    if (!need_fn(vm, argc, argv, 0, "predicate")) return mcs_null();
    for (uint32_t i = 0; i < l->count; i++) { mcs_value_t r; if (!cb(vm, argv[0], l->items[i], i, &r)) return mcs_null(); if (mcs_truthy(r)) return mcs_bool(true); }
    return mcs_bool(false);
}
NATIVE(seq_all) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "predicate")) return mcs_null();
    for (uint32_t i = 0; i < l->count; i++) { mcs_value_t r; if (!cb(vm, argv[0], l->items[i], i, &r)) return mcs_null(); if (!mcs_truthy(r)) return mcs_bool(false); }
    return mcs_bool(true);
}
NATIVE(seq_count) {
    SELF_SEQ();
    if (argc == 0) return mcs_int((mcs_int_t)l->count);
    if (!need_fn(vm, argc, argv, 0, "predicate")) return mcs_null();
    mcs_int_t n = 0;
    for (uint32_t i = 0; i < l->count; i++) { mcs_value_t r; if (!cb(vm, argv[0], l->items[i], i, &r)) return mcs_null(); if (mcs_truthy(r)) n++; }
    return mcs_int(n);
}
/* value i of the sequence, optionally projected through argv[0] */
static bool item_at(mcs_vm_t* vm, mcs_list_t* l, uint32_t i, int argc, mcs_value_t* argv, mcs_value_t* out) {
    if (argc > 0) return cb(vm, argv[0], l->items[i], i, out);
    *out = l->items[i];
    return true;
}
NATIVE(seq_sum) {
    SELF_SEQ();
    mcs_int_t si = 0;
#if MCS_ENABLE_FLOAT
    mcs_float_t sf = 0; bool fl = false;
#endif
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t v;
        if (!item_at(vm, l, i, argc, argv, &v)) return mcs_null();
        if (is_intlike_v(v)) si = (mcs_int_t)((mcs_uint_t)si + (mcs_uint_t)v.as.i);
#if MCS_ENABLE_FLOAT
        else if (v.type == MCS_T_FLOAT) { sf += v.as.f; fl = true; }
#endif
        else if (v.type != MCS_T_NULL) { mcs_throw(vm, EXC_INVOP, "Sum: element is not a number"); return mcs_null(); }
    }
#if MCS_ENABLE_FLOAT
    if (fl) return mcs_float(sf + (mcs_float_t)si);
#endif
    return mcs_int(si);
}
#if MCS_ENABLE_FLOAT
NATIVE(seq_average) {
    SELF_SEQ();
    if (l->count == 0) { no_elements(vm); return mcs_null(); }
    mcs_float_t s = 0;
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t v;
        if (!item_at(vm, l, i, argc, argv, &v)) return mcs_null();
        s += mcs_to_float(vm, v); CHECK();
    }
    return mcs_float(s / (mcs_float_t)l->count);
}
#endif
static mcs_value_t minmax(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, int sign, bool by) {
    SELF_SEQ();
    if (l->count == 0) { no_elements(vm); return mcs_null(); }
    if (by && !need_fn(vm, argc, argv, 0, "keySelector")) return mcs_null();
    mcs_value_t best, bestv = l->items[0];
    if (!item_at(vm, l, 0, argc, argv, &best)) return mcs_null();
    mcs_push_root(vm, best);
    for (uint32_t i = 1; i < l->count; i++) {
        mcs_value_t v; bool ok;
        if (!item_at(vm, l, i, argc, argv, &v)) FAIL_POP();
        int c = mcs_compare_values(vm, v, best, &ok);
        if (!ok) FAIL_POP();
        if (c * sign > 0) { best = v; bestv = l->items[i]; vm->roots[vm->root_count - 1] = best; }
    }
    mcs_pop_root(vm, 1);
    return by ? bestv : best;
}
NATIVE(seq_max) { return minmax(vm, self, argc, argv, 1, false); }
NATIVE(seq_min) { return minmax(vm, self, argc, argv, -1, false); }
NATIVE(seq_maxby) { return minmax(vm, self, argc, argv, 1, true); }
NATIVE(seq_minby) { return minmax(vm, self, argc, argv, -1, true); }

static mcs_value_t find_impl(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, bool last, bool want_index, int on_missing) {
    SELF_SEQ();
    /* on_missing: 0 -> default(null / -1), 1 -> throw */
    if (argc == 0) {
        if (l->count) return want_index ? mcs_int(last ? (mcs_int_t)l->count - 1 : 0) : l->items[last ? l->count - 1 : 0];
        if (on_missing) { no_elements(vm); return mcs_null(); }
        return want_index ? mcs_int(-1) : mcs_null();
    }
    mcs_value_t deflt = mcs_null();
    /* default(T) heuristic: infer T from the first element (generics are erased) */
    if (l->count && !want_index) {
        mcs_value_t f0 = l->items[0];
        if (f0.type == MCS_T_INT) deflt = mcs_int(0);
#if MCS_ENABLE_FLOAT
        else if (f0.type == MCS_T_FLOAT) deflt = mcs_float(0);
#endif
        else if (f0.type == MCS_T_BOOL) deflt = mcs_bool(false);
        else if (f0.type == MCS_T_CHAR) { deflt = f0; deflt.as.i = 0; }
    }
    if (argc == 1 && !on_missing && !want_index && !is_fn(argv[0])) /* FirstOrDefault(defaultValue) */
        return l->count ? l->items[last ? l->count - 1 : 0] : argv[0];
    if (!need_fn(vm, argc, argv, 0, "predicate")) return mcs_null();
    if (argc > 1) deflt = argv[1];
    for (uint32_t k = 0; k < l->count; k++) {
        uint32_t i = last ? l->count - 1 - k : k;
        mcs_value_t r;
        if (!lib_call(vm, argv[0], 1, &l->items[i], &r)) return mcs_null();
        if (mcs_truthy(r)) return want_index ? mcs_int((mcs_int_t)i) : l->items[i];
    }
    if (on_missing) { mcs_throw(vm, EXC_INVOP, "Sequence contains no matching element"); return mcs_null(); }
    return want_index ? mcs_int(-1) : deflt;
}
NATIVE(seq_first) { return find_impl(vm, self, argc, argv, false, false, 1); }
NATIVE(seq_firstordefault) { return find_impl(vm, self, argc, argv, false, false, 0); }
NATIVE(seq_last) { return find_impl(vm, self, argc, argv, true, false, 1); }
NATIVE(seq_lastordefault) { return find_impl(vm, self, argc, argv, true, false, 0); }
NATIVE(seq_findindex) { return find_impl(vm, self, argc, argv, false, true, 0); }
NATIVE(seq_findlastindex) { return find_impl(vm, self, argc, argv, true, true, 0); }
NATIVE(seq_single) {
    SELF_SEQ();
    if (argc == 0) {
        if (l->count != 1) { mcs_throw(vm, EXC_INVOP, l->count ? "Sequence contains more than one element" : "Sequence contains no elements"); return mcs_null(); }
        return l->items[0];
    }
    mcs_value_t found = mcs_null(); int n = 0;
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t r;
        if (!lib_call(vm, argv[0], 1, &l->items[i], &r)) return mcs_null();
        if (mcs_truthy(r)) { found = l->items[i]; n++; }
    }
    if (n != 1) { mcs_throw(vm, EXC_INVOP, n ? "Sequence contains more than one matching element" : "Sequence contains no matching element"); return mcs_null(); }
    return found;
}
NATIVE(seq_elementat) {
    SELF_SEQ();
    mcs_int_t i = mcs_to_int(vm, argv[0]); CHECK();
    if (i < 0 || (mcs_uint_t)i >= l->count) {
        mcs_throw(vm, EXC_ARGRANGE, "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
        return mcs_null();
    }
    return l->items[i];
}
NATIVE(seq_elementatordefault) {
    SELF_SEQ();
    mcs_int_t i = mcs_to_int(vm, argv[0]); CHECK();
    return (i < 0 || (mcs_uint_t)i >= l->count) ? mcs_null() : l->items[i];
}
static mcs_value_t order_impl(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, bool desc) {
    SELF_SEQ();
    uint32_t n = l->count;
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, n);
    memcpy(o->items, l->items, sizeof(mcs_value_t) * n);
    mcs_list_t* keys = o;
    bool rooted_keys = false;
    if (argc > 0 && argv[0].type != MCS_T_NULL) {
        if (!need_fn(vm, argc, argv, 0, "keySelector")) FAIL_POP();
        keys = new_rooted(vm, MCS_O_ARRAY, n);
        rooted_keys = true;
        for (uint32_t i = 0; i < n; i++) {
            mcs_value_t k;
            if (!lib_call(vm, argv[0], 1, &o->items[i], &k)) { mcs_pop_root(vm, 2); return mcs_null(); }
            keys->items[i] = k;
        }
    }
    mcs_value_t cmp = argc > 1 ? argv[1] : mcs_null();
    uint32_t* idx = MCS_ALLOC(vm, uint32_t, n ? n : 1);
    bool ok = sort_indices(vm, keys->items, n, cmp, desc, idx);
    mcs_list_t* res = NULL;
    if (ok) {
        vm->gc_pause++;
        res = mcs_new_listobj(vm, MCS_O_LIST, n);
        for (uint32_t i = 0; i < n; i++) res->items[i] = o->items[idx[i]];
        vm->gc_pause--;
    }
    MCS_FREE(vm, uint32_t, idx, n ? n : 1);
    mcs_pop_root(vm, rooted_keys ? 2 : 1);
    if (ok) {
        vm->order_last = OBJ_VAL(res);
        vm->gc_pause++;
        mcs_list_t* spec = mcs_new_listobj(vm, MCS_O_LIST, 2);
        spec->items[0] = argc > 0 ? argv[0] : mcs_null();
        spec->items[1] = mcs_bool(desc);
        vm->order_spec = OBJ_VAL(spec);
        vm->gc_pause--;
    }
    return ok ? OBJ_VAL(res) : mcs_null();
}
/* ThenBy / ThenByDescending: re-sort the result of the preceding OrderBy using
 * composite keys (the VM remembers the key selectors of the last ordering). */
static mcs_value_t thenby_impl(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, bool desc) {
    SELF_SEQ();
    if (!need_fn(vm, argc, argv, 0, "keySelector")) return mcs_null();
    if (!(IS_OBJ(vm->order_last) && AS_OBJ(vm->order_last) == AS_OBJ(self) && IS_OBJ(vm->order_spec)))
        return order_impl(vm, self, argc, argv, desc);
    mcs_list_t* old = AS_LIST(vm->order_spec);
    uint32_t nk = old->count / 2 + 1, n = l->count;
    mcs_list_t* spec = new_rooted(vm, MCS_O_LIST, nk * 2);
    memcpy(spec->items, old->items, sizeof(mcs_value_t) * old->count);
    spec->items[old->count] = argv[0]; spec->items[old->count + 1] = mcs_bool(desc);
    mcs_list_t* keys = new_rooted(vm, MCS_O_ARRAY, n);
    for (uint32_t i = 0; i < n; i++) {
        vm->gc_pause++;
        mcs_list_t* k = mcs_new_listobj(vm, MCS_O_ARRAY, nk);
        vm->gc_pause--;
        keys->items[i] = OBJ_VAL(k);
        for (uint32_t j = 0; j < nk; j++) {
            mcs_value_t sel = spec->items[2 * j], kv = l->items[i];
            if (sel.type != MCS_T_NULL && !lib_call(vm, sel, 1, &l->items[i], &kv)) { mcs_pop_root(vm, 2); return mcs_null(); }
            k->items[j] = kv;
        }
    }
    uint32_t* idx = MCS_ALLOC(vm, uint32_t, n ? n : 1);
    g_multi_spec = spec;
    bool ok = sort_indices(vm, keys->items, n, mcs_null(), false, idx);
    g_multi_spec = NULL;
    mcs_list_t* res = NULL;
    if (ok) {
        vm->gc_pause++;
        res = mcs_new_listobj(vm, MCS_O_LIST, n);
        for (uint32_t i = 0; i < n; i++) res->items[i] = l->items[idx[i]];
        vm->gc_pause--;
        vm->order_last = OBJ_VAL(res); vm->order_spec = OBJ_VAL(spec);
    }
    MCS_FREE(vm, uint32_t, idx, n ? n : 1);
    mcs_pop_root(vm, 2);
    return ok ? OBJ_VAL(res) : mcs_null();
}
NATIVE(seq_thenby) { return thenby_impl(vm, self, argc, argv, false); }
NATIVE(seq_thenbydesc) { return thenby_impl(vm, self, argc, argv, true); }
NATIVE(seq_orderby) { return order_impl(vm, self, argc, argv, false); }
NATIVE(seq_orderbydesc) { return order_impl(vm, self, argc, argv, true); }
NATIVE(seq_order) { return order_impl(vm, self, 0, argv, false); }
NATIVE(seq_orderdesc) { return order_impl(vm, self, 0, argv, true); }

static mcs_value_t copy_seq(mcs_vm_t* vm, mcs_list_t* l, uint8_t kind, uint32_t from, uint32_t n) {
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, kind, n);
    if (n) memcpy(o->items, l->items + from, sizeof(mcs_value_t) * n);
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(seq_tolist) { SELF_SEQ(); return copy_seq(vm, l, MCS_O_LIST, 0, l->count); }
NATIVE(seq_toarray) { SELF_SEQ(); return copy_seq(vm, l, MCS_O_ARRAY, 0, l->count); }
NATIVE(seq_skip) {
    SELF_SEQ();
    mcs_int_t n = mcs_to_int(vm, argv[0]); CHECK();
    if (n < 0) n = 0;
    if ((mcs_uint_t)n > l->count) n = (mcs_int_t)l->count;
    return copy_seq(vm, l, MCS_O_LIST, (uint32_t)n, l->count - (uint32_t)n);
}
NATIVE(seq_take) {
    SELF_SEQ();
    mcs_int_t n = mcs_to_int(vm, argv[0]); CHECK();
    if (n < 0) n = 0;
    if ((mcs_uint_t)n > l->count) n = (mcs_int_t)l->count;
    return copy_seq(vm, l, MCS_O_LIST, 0, (uint32_t)n);
}
static mcs_value_t while_impl(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, bool take) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "predicate")) return mcs_null();
    uint32_t i = 0;
    for (; i < l->count; i++) { mcs_value_t r; if (!cb(vm, argv[0], l->items[i], i, &r)) return mcs_null(); if (!mcs_truthy(r)) break; }
    return take ? copy_seq(vm, l, MCS_O_LIST, 0, i) : copy_seq(vm, l, MCS_O_LIST, i, l->count - i);
}
NATIVE(seq_takewhile) { return while_impl(vm, self, argc, argv, true); }
NATIVE(seq_skipwhile) { return while_impl(vm, self, argc, argv, false); }
NATIVE(seq_distinct) {
    SELF_SEQ();
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, 0);
    vm->gc_pause++;
    mcs_dict_t* seen = mcs_new_dict(vm);
    mcs_push_root(vm, OBJ_VAL(seen));
    vm->gc_pause--;
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t v = l->items[i], key = v;
        if (argc > 0 && !lib_call(vm, argv[0], 1, &v, &key)) { mcs_pop_root(vm, 2); return mcs_null(); }
#if MCS_ENABLE_FLOAT
        if (key.type == MCS_T_FLOAT && key.as.f == (mcs_float_t)(mcs_int_t)key.as.f) key = mcs_int((mcs_int_t)key.as.f);
#endif
        if (key.type == MCS_T_CHAR) key = mcs_int(key.as.i);
        if (mcs_dict_get(seen, key, NULL)) continue;
        mcs_dict_set(vm, seen, key, mcs_bool(true));
        mcs_listobj_push(vm, o, v);
    }
    mcs_pop_root(vm, 1);
    return done(vm, o);
}
NATIVE(seq_aggregate) {
    SELF_SEQ();
    mcs_value_t acc, fn;
    uint32_t start = 0;
    if (argc >= 2) { acc = argv[0]; fn = argv[1]; }
    else {
        if (!need_fn(vm, argc, argv, 0, "func")) return mcs_null();
        if (!l->count) { no_elements(vm); return mcs_null(); }
        acc = l->items[0]; fn = argv[0]; start = 1;
    }
    mcs_push_root(vm, acc);
    for (uint32_t i = start; i < l->count; i++) {
        mcs_value_t args[2] = { acc, l->items[i] };
        if (!lib_call(vm, fn, 2, args, &acc)) FAIL_POP();
        vm->roots[vm->root_count - 1] = acc;
    }
    if (argc >= 3) { mcs_value_t r; if (!lib_call(vm, argv[2], 1, &acc, &r)) FAIL_POP(); acc = r; }
    mcs_pop_root(vm, 1);
    return acc;
}
NATIVE(seq_sequenceequal) {
    SELF_SEQ();
    if (!lib_is_seq(argv[0])) return mcs_bool(false);
    mcs_list_t* o = AS_LIST(argv[0]);
    if (o->count != l->count) return mcs_bool(false);
    for (uint32_t i = 0; i < l->count; i++) { if (!lib_equals(vm, l->items[i], o->items[i])) return mcs_bool(false); CHECK(); }
    return mcs_bool(true);
}
NATIVE(seq_zip) { /* Zip(second) -> (First, Second) tuples; Zip(second, (a, b) => ...) */
    SELF_SEQ();
    if (argc < 1 || !lib_is_seq(argv[0])) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'second')"); return mcs_null(); }
    bool sel = argc >= 2;
    if (sel && !need_fn(vm, argc, argv, 1, "resultSelector")) return mcs_null();
    mcs_list_t* b = AS_LIST(argv[0]);
    uint32_t n = l->count < b->count ? l->count : b->count;
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, 0);
    for (uint32_t i = 0; i < n && i < l->count && i < b->count; i++) {
        mcs_value_t pair[2] = { l->items[i], b->items[i] }, r;
        if (sel) { if (!lib_call(vm, argv[1], 2, pair, &r)) FAIL_POP(); }
        else r = mcs_lib_tuple(vm, "First,Second", 2, pair);
        mcs_listobj_push(vm, o, r);
    }
    return done(vm, o);
}
NATIVE(seq_chunk) { /* Chunk(size) -> List of arrays */
    SELF_SEQ();
    mcs_int_t sz = mcs_to_int(vm, argv[0]); CHECK();
    if (sz < 1) { mcs_throw(vm, EXC_ARGRANGE, "Specified argument was out of the range of valid values. (Parameter 'size')"); return mcs_null(); }
    mcs_list_t* o = new_rooted(vm, MCS_O_LIST, 0);
    for (uint32_t i = 0; i < l->count; i += (uint32_t)sz) {
        uint32_t k = l->count - i < (uint32_t)sz ? l->count - i : (uint32_t)sz;
        mcs_list_t* c = mcs_new_listobj(vm, MCS_O_ARRAY, k);
        memcpy(c->items, l->items + i, sizeof(mcs_value_t) * k);
        mcs_listobj_push(vm, o, OBJ_VAL(c));
    }
    return done(vm, o);
}
NATIVE(seq_concat) {
    SELF_SEQ();
    if (!lib_is_seq(argv[0])) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'second')"); return mcs_null(); }
    mcs_list_t* b = AS_LIST(argv[0]);
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_LIST, l->count + b->count);
    memcpy(o->items, l->items, sizeof(mcs_value_t) * l->count);
    memcpy(o->items + l->count, b->items, sizeof(mcs_value_t) * b->count);
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(seq_append) {
    SELF_SEQ();
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_LIST, l->count + 1);
    memcpy(o->items, l->items, sizeof(mcs_value_t) * l->count);
    o->items[l->count] = argv[0];
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(seq_todictionary) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "keySelector")) return mcs_null();
    vm->gc_pause++;
    mcs_dict_t* d = mcs_new_dict(vm);
    mcs_push_root(vm, OBJ_VAL(d));
    vm->gc_pause--;
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t it = l->items[i], k, v = it;
        if (!lib_call(vm, argv[0], 1, &it, &k)) FAIL_POP();
        mcs_push_root(vm, k);
        if (argc > 1 && !lib_call(vm, argv[1], 1, &it, &v)) { mcs_pop_root(vm, 2); return mcs_null(); }
        mcs_pop_root(vm, 1);
        if (mcs_dict_get(d, k, NULL)) { mcs_throw(vm, EXC_ARGUMENT, "An item with the same key has already been added."); FAIL_POP(); }
        mcs_dict_set(vm, d, k, v);
    }
    mcs_pop_root(vm, 1);
    return OBJ_VAL(d);
}
NATIVE(seq_groupby) { /* returns Dictionary<key, List<item>> (ordered by first occurrence) */
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "keySelector")) return mcs_null();
    vm->gc_pause++;
    mcs_dict_t* d = mcs_new_dict(vm);
    mcs_push_root(vm, OBJ_VAL(d));
    vm->gc_pause--;
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t it = l->items[i], k, g;
        if (!lib_call(vm, argv[0], 1, &it, &k)) FAIL_POP();
        if (!mcs_dict_get(d, k, &g)) {
            vm->gc_pause++;
            g = OBJ_VAL(mcs_new_listobj(vm, MCS_O_LIST, 0));
            mcs_dict_set(vm, d, k, g);
            vm->gc_pause--;
        }
        mcs_listobj_push(vm, AS_LIST(g), it);
    }
    mcs_pop_root(vm, 1);
    return OBJ_VAL(d);
}
NATIVE(seq_foreach) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "action")) return mcs_null();
    for (uint32_t i = 0; i < l->count; i++) { mcs_value_t r; if (!lib_call(vm, argv[0], 1, &l->items[i], &r)) return mcs_null(); }
    return mcs_null();
}
NATIVE(seq_findall) {
    SELF_SEQ(); if (!need_fn(vm, argc, argv, 0, "match")) return mcs_null();
    mcs_list_t* o = new_rooted(vm, OBJ_KIND(self) == MCS_O_ARRAY ? MCS_O_ARRAY : MCS_O_LIST, 0);
    for (uint32_t i = 0; i < l->count; i++) {
        mcs_value_t r;
        if (!lib_call(vm, argv[0], 1, &l->items[i], &r)) FAIL_POP();
        if (mcs_truthy(r)) mcs_listobj_push(vm, o, l->items[i]);
    }
    return done(vm, o);
}
NATIVE(seq_reverse_copy) {
    SELF_SEQ();
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, OBJ_KIND(self), l->count);
    for (uint32_t i = 0; i < l->count; i++) o->items[i] = l->items[l->count - 1 - i];
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(seq_copyto) {
    SELF_SEQ();
    if (!lib_is_seq(argv[0])) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'array')"); return mcs_null(); }
    mcs_list_t* d = AS_LIST(argv[0]);
    mcs_int_t at = argc > 1 ? mcs_to_int(vm, argv[1]) : 0; CHECK();
    if (!check_range(vm, at, (mcs_int_t)l->count, d->count)) return mcs_null();
    memmove(d->items + at, l->items, sizeof(mcs_value_t) * l->count);
    return mcs_null();
}
NATIVE(seq_binarysearch) {
    SELF_SEQ();
    mcs_value_t cmp = argc > 1 ? argv[1] : mcs_null();
    int64_t lo = 0, hi = (int64_t)l->count - 1;
    while (lo <= hi) {
        int64_t mid = lo + ((hi - lo) >> 1);
        bool ok; int c = cmp2(vm, cmp, l->items[mid], argv[0], &ok);
        if (!ok) return mcs_null();
        if (c == 0) return mcs_int((mcs_int_t)mid);
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return mcs_int((mcs_int_t)~lo);
}

/* ------------------------------------------------------------ Array */
NATIVE(arr_length) { SELF_SEQ(); return mcs_int((mcs_int_t)l->count); }
NATIVE(arr_rank) { return mcs_int(1); }
NATIVE(arr_clone) { SELF_SEQ(); return copy_seq(vm, l, OBJ_KIND(self), 0, l->count); }
NATIVE(arr_getlength) { SELF_SEQ(); return mcs_int((mcs_int_t)l->count); }

static mcs_list_t* arg_seq(mcs_vm_t* vm, mcs_value_t v, const char* name) {
    if (lib_is_seq(v)) return AS_LIST(v);
    mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter '%s')", name);
    return NULL;
}
NATIVE(arrs_sort) {
    mcs_list_t* l = arg_seq(vm, argv[0], "array"); CHECK();
    if (argc == 3 && is_intlike_v(argv[1])) {
        mcs_int_t a = mcs_to_int(vm, argv[1]), n = mcs_to_int(vm, argv[2]); CHECK();
        if (!check_range(vm, a, n, l->count)) return mcs_null();
        sort_values(vm, l, (uint32_t)a, (uint32_t)n, mcs_null(), false);
        return mcs_null();
    }
    sort_values(vm, l, 0, l->count, argc > 1 ? argv[1] : mcs_null(), false);
    return mcs_null();
}
static void reverse_range(mcs_list_t* l, uint32_t a, uint32_t n) {
    for (uint32_t i = 0; i < n / 2; i++) { mcs_value_t t = l->items[a + i]; l->items[a + i] = l->items[a + n - 1 - i]; l->items[a + n - 1 - i] = t; }
}
NATIVE(arrs_reverse) {
    mcs_list_t* l = arg_seq(vm, argv[0], "array"); CHECK();
    if (argc == 3) {
        mcs_int_t a = mcs_to_int(vm, argv[1]), n = mcs_to_int(vm, argv[2]); CHECK();
        if (!check_range(vm, a, n, l->count)) return mcs_null();
        reverse_range(l, (uint32_t)a, (uint32_t)n);
    } else reverse_range(l, 0, l->count);
    return mcs_null();
}
NATIVE(arrs_indexof) { mcs_list_t* l = arg_seq(vm, argv[0], "array"); CHECK(); (void)l; return seq_indexof(vm, argv[0], argc - 1, argv + 1); }
NATIVE(arrs_lastindexof) { arg_seq(vm, argv[0], "array"); CHECK(); return seq_lastindexof(vm, argv[0], argc - 1, argv + 1); }
NATIVE(arrs_fill) {
    mcs_list_t* l = arg_seq(vm, argv[0], "array"); CHECK();
    uint32_t a = 0, n = l->count;
    if (argc == 4) {
        mcs_int_t s = mcs_to_int(vm, argv[2]), c = mcs_to_int(vm, argv[3]); CHECK();
        if (!check_range(vm, s, c, l->count)) return mcs_null();
        a = (uint32_t)s; n = (uint32_t)c;
    }
    for (uint32_t i = a; i < a + n; i++) l->items[i] = argv[1];
    return mcs_null();
}
NATIVE(arrs_copy) {
    mcs_list_t *src, *dst; mcs_int_t si = 0, di = 0, n;
    src = arg_seq(vm, argv[0], "sourceArray"); CHECK();
    if (argc == 3) { dst = arg_seq(vm, argv[1], "destinationArray"); CHECK(); n = mcs_to_int(vm, argv[2]); }
    else if (argc == 5) { si = mcs_to_int(vm, argv[1]); dst = arg_seq(vm, argv[2], "destinationArray"); CHECK(); di = mcs_to_int(vm, argv[3]); n = mcs_to_int(vm, argv[4]); }
    else { mcs_throw(vm, EXC_ARGUMENT, "Array.Copy expects 3 or 5 arguments"); return mcs_null(); }
    CHECK();
    if (!check_range(vm, si, n, src->count) || !check_range(vm, di, n, dst->count)) return mcs_null();
    memmove(dst->items + di, src->items + si, sizeof(mcs_value_t) * (size_t)n);
    return mcs_null();
}
NATIVE(arrs_clear) {
    mcs_list_t* l = arg_seq(vm, argv[0], "array"); CHECK();
    uint32_t a = 0, n = l->count;
    if (argc == 3) {
        mcs_int_t s = mcs_to_int(vm, argv[1]), c = mcs_to_int(vm, argv[2]); CHECK();
        if (!check_range(vm, s, c, l->count)) return mcs_null();
        a = (uint32_t)s; n = (uint32_t)c;
    }
    /* reset to the element type's default when it can be inferred */
    for (uint32_t i = a; i < a + n; i++) {
        mcs_value_t v = l->items[i];
        if (v.type == MCS_T_INT) l->items[i] = mcs_int(0);
#if MCS_ENABLE_FLOAT
        else if (v.type == MCS_T_FLOAT) l->items[i] = mcs_float(0);
#endif
        else if (v.type == MCS_T_BOOL) l->items[i] = mcs_bool(false);
        else if (v.type == MCS_T_CHAR) l->items[i] = mcs_char(0);
        else l->items[i] = mcs_null();
    }
    return mcs_null();
}
#define ARR_FWD(fname, target) NATIVE(fname) { arg_seq(vm, argv[0], "array"); CHECK(); return target(vm, argv[0], argc - 1, argv + 1); }
ARR_FWD(arrs_exists, seq_any)
ARR_FWD(arrs_trueforall, seq_all)
ARR_FWD(arrs_find, seq_firstordefault)
ARR_FWD(arrs_findlast, seq_lastordefault)
ARR_FWD(arrs_findindex, seq_findindex)
ARR_FWD(arrs_findlastindex, seq_findlastindex)
ARR_FWD(arrs_findall, seq_findall)
ARR_FWD(arrs_foreach, seq_foreach)
ARR_FWD(arrs_binarysearch, seq_binarysearch)
NATIVE(arrs_convertall) {
    arg_seq(vm, argv[0], "array"); CHECK();
    mcs_value_t r = seq_select(vm, argv[0], argc - 1, argv + 1);
    if (IS_KIND(r, MCS_O_LIST)) r.as.o->kind = MCS_O_ARRAY;
    return r;
}
NATIVE(arrs_empty) { return OBJ_VAL(mcs_new_listobj(vm, MCS_O_ARRAY, 0)); }

/* ------------------------------------------------------------ List */
NATIVE(list_new) {
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_LIST, 0);
    vm->gc_pause--;
    if (argc >= 1 && lib_is_seq(argv[0])) {
        mcs_list_t* s = AS_LIST(argv[0]);
        for (uint32_t i = 0; i < s->count; i++) mcs_listobj_push(vm, o, s->items[i]);
    } else if (argc >= 1 && IS_KIND(argv[0], MCS_O_DICT)) {
        mcs_dict_t* d = AS_DICT(argv[0]);
        for (uint32_t i = 0; i < d->count; i++) mcs_listobj_push(vm, o, d->keys[i]);
    } else if (argc >= 1 && argv[0].type == MCS_T_INT) {
        if (argv[0].as.i < 0) { mcs_throw(vm, EXC_ARGRANGE, "Non-negative number required. (Parameter 'capacity')"); return mcs_null(); }
        if (argv[0].as.i > 0 && argv[0].as.i < 65536) {
            o->items = MCS_ALLOC(vm, mcs_value_t, (size_t)argv[0].as.i);
            o->cap = (uint32_t)argv[0].as.i;
        }
    } else if (argc >= 1 && argv[0].type != MCS_T_NULL) {
        mcs_throw(vm, EXC_ARGUMENT, "List: unsupported constructor argument of type %s", mcs_type_name(vm, argv[0]));
        return mcs_null();
    }
    return OBJ_VAL(o);
}
#define SELF_LIST() if (!IS_KIND(self, MCS_O_LIST)) { mcs_throw(vm, EXC_NOTSUPPORTED, "Collection was of a fixed size."); return mcs_null(); } \
    mcs_list_t* l = AS_LIST(self)
NATIVE(list_count) { SELF_SEQ(); return mcs_int((mcs_int_t)l->count); }
NATIVE(list_capacity) { SELF_SEQ(); return mcs_int((mcs_int_t)l->cap); }
NATIVE(list_add) { SELF_LIST(); mcs_listobj_push(vm, l, argv[0]); return mcs_null(); }
NATIVE(list_addrange) {
    SELF_LIST();
    if (lib_is_seq(argv[0])) {
        mcs_list_t* s = AS_LIST(argv[0]);
        uint32_t n = s->count; /* AddRange(self) safe */
        for (uint32_t i = 0; i < n; i++) mcs_listobj_push(vm, l, s->items[i]);
    } else if (IS_KIND(argv[0], MCS_O_DICT)) {
        mcs_dict_t* d = AS_DICT(argv[0]);
        for (uint32_t i = 0; i < d->count; i++) mcs_listobj_push(vm, l, d->keys[i]);
    } else { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'collection')"); }
    return mcs_null();
}
NATIVE(list_insert) {
    SELF_LIST();
    mcs_int_t i = mcs_to_int(vm, argv[0]); CHECK();
    if (i < 0 || (mcs_uint_t)i > l->count) { mcs_throw(vm, EXC_ARGRANGE, "Index must be within the bounds of the List. (Parameter 'index')"); return mcs_null(); }
    mcs_listobj_insert(vm, l, (uint32_t)i, argv[1]);
    return mcs_null();
}
NATIVE(list_insertrange) {
    SELF_LIST();
    mcs_int_t at = mcs_to_int(vm, argv[0]); CHECK();
    mcs_list_t* s = arg_seq(vm, argv[1], "collection"); CHECK();
    if (at < 0 || (mcs_uint_t)at > l->count) { mcs_throw(vm, EXC_ARGRANGE, "Index must be within the bounds of the List. (Parameter 'index')"); return mcs_null(); }
    uint32_t n = s->count;
    for (uint32_t i = 0; i < n; i++) mcs_listobj_insert(vm, l, (uint32_t)at + i, s->items[i]);
    return mcs_null();
}
NATIVE(list_remove) {
    SELF_LIST();
    for (uint32_t i = 0; i < l->count; i++) if (lib_equals(vm, l->items[i], argv[0])) { mcs_listobj_remove_at(l, i); return mcs_bool(true); }
    return mcs_bool(false);
}
NATIVE(list_removeat) {
    SELF_LIST();
    mcs_int_t i = mcs_to_int(vm, argv[0]); CHECK();
    if (i < 0 || (mcs_uint_t)i >= l->count) { mcs_throw(vm, EXC_ARGRANGE, "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')"); return mcs_null(); }
    mcs_listobj_remove_at(l, (uint32_t)i);
    return mcs_null();
}
NATIVE(list_removerange) {
    SELF_LIST();
    mcs_int_t a = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
    if (!check_range(vm, a, n, l->count)) return mcs_null();
    memmove(l->items + a, l->items + a + n, sizeof(mcs_value_t) * (l->count - (uint32_t)(a + n)));
    l->count -= (uint32_t)n;
    return mcs_null();
}
NATIVE(list_removeall) {
    SELF_LIST(); if (!need_fn(vm, argc, argv, 0, "match")) return mcs_null();
    /* evaluate predicates first so the list is untouched if one throws */
    uint32_t n = l->count;
    mcs_list_t* flags = new_rooted(vm, MCS_O_ARRAY, n);
    for (uint32_t i = 0; i < n && i < l->count; i++) {
        mcs_value_t r;
        if (!lib_call(vm, argv[0], 1, &l->items[i], &r)) FAIL_POP();
        flags->items[i] = mcs_bool(mcs_truthy(r));
    }
    uint32_t w = 0, removed = 0;
    for (uint32_t i = 0; i < l->count; i++) {
        if (i < n && flags->items[i].as.b) { removed++; continue; }
        l->items[w++] = l->items[i];
    }
    l->count = w;
    mcs_pop_root(vm, 1);
    return mcs_int((mcs_int_t)removed);
}
NATIVE(list_clear) { SELF_LIST(); l->count = 0; return mcs_null(); }
NATIVE(list_sort) {
    SELF_SEQ();
    if (argc == 3 || argc == 4) {
        mcs_int_t a = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
        if (!check_range(vm, a, n, l->count)) return mcs_null();
        sort_values(vm, l, (uint32_t)a, (uint32_t)n, argc == 3 ? argv[2] : argv[3], false);
        return mcs_null();
    }
    sort_values(vm, l, 0, l->count, argc ? argv[0] : mcs_null(), false);
    return mcs_null();
}
NATIVE(list_reverse) {
    SELF_SEQ();
    if (argc == 2) {
        mcs_int_t a = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
        if (!check_range(vm, a, n, l->count)) return mcs_null();
        reverse_range(l, (uint32_t)a, (uint32_t)n);
    } else reverse_range(l, 0, l->count);
    return mcs_null();
}
NATIVE(list_getrange) {
    SELF_SEQ();
    mcs_int_t a = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
    if (!check_range(vm, a, n, l->count)) return mcs_null();
    return copy_seq(vm, l, MCS_O_LIST, (uint32_t)a, (uint32_t)n);
}
NATIVE(list_trimexcess) { return mcs_null(); }
NATIVE(list_asreadonly) { return self; }

/* ------------------------------------------------------------ Enumerable */
NATIVE(enum_range) {
    mcs_int_t a = mcs_to_int(vm, argv[0]), n = mcs_to_int(vm, argv[1]); CHECK();
    if (n < 0 || n > 0x7FFFFFF) { mcs_throw(vm, EXC_ARGRANGE, "Specified argument was out of the range of valid values. (Parameter 'count')"); return mcs_null(); }
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_LIST, (uint32_t)n);
    for (mcs_int_t i = 0; i < n; i++) o->items[i] = mcs_int(a + i);
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(enum_repeat) {
    mcs_int_t n = mcs_to_int(vm, argv[1]); CHECK();
    if (n < 0 || n > 0x7FFFFFF) { mcs_throw(vm, EXC_ARGRANGE, "Specified argument was out of the range of valid values. (Parameter 'count')"); return mcs_null(); }
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_LIST, (uint32_t)n);
    for (mcs_int_t i = 0; i < n; i++) o->items[i] = argv[0];
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(enum_empty) { return OBJ_VAL(mcs_new_listobj(vm, MCS_O_LIST, 0)); }
static const mcs_reg_t enumerable_fns[] = {
    MCS_FN("Range", enum_range, 2), MCS_FN("Repeat", enum_repeat, 2), MCS_FN("Empty", enum_empty, 0), MCS_REG_END
};

/* sequence operators shared by arrays and lists */
#define SEQ_REGS \
    MCS_FN("GetEnumerator", seq_getenum, 0), MCS_FN("Contains", seq_contains, 1), MCS_FN("IndexOf", seq_indexof, -1), \
    MCS_FN("LastIndexOf", seq_lastindexof, 1), MCS_FN("Where", seq_where, 1), MCS_FN("Select", seq_select, 1), \
    MCS_FN("SelectMany", seq_selectmany, 1), MCS_FN("Any", seq_any, -1), MCS_FN("All", seq_all, 1), MCS_FN("Count", seq_count, -1), \
    MCS_FN("Sum", seq_sum, -1), MCS_FN("Min", seq_min, -1), MCS_FN("Max", seq_max, -1), MCS_FN("MinBy", seq_minby, 1), \
    MCS_FN("MaxBy", seq_maxby, 1), MCS_FN("First", seq_first, -1), MCS_FN("FirstOrDefault", seq_firstordefault, -1), \
    MCS_FN("Last", seq_last, -1), MCS_FN("LastOrDefault", seq_lastordefault, -1), MCS_FN("Single", seq_single, -1), \
    MCS_FN("ElementAt", seq_elementat, 1), MCS_FN("ElementAtOrDefault", seq_elementatordefault, 1), \
    MCS_FN("OrderBy", seq_orderby, -1), MCS_FN("OrderByDescending", seq_orderbydesc, -1), MCS_FN("Order", seq_order, 0), MCS_FN("ThenBy", seq_thenby, 1), MCS_FN("ThenByDescending", seq_thenbydesc, 1), \
    MCS_FN("OrderDescending", seq_orderdesc, 0), MCS_FN("ToList", seq_tolist, 0), MCS_FN("ToArray", seq_toarray, 0), \
    MCS_FN("Skip", seq_skip, 1), MCS_FN("Take", seq_take, 1), MCS_FN("SkipWhile", seq_skipwhile, 1), MCS_FN("TakeWhile", seq_takewhile, 1), \
    MCS_FN("Distinct", seq_distinct, 0), MCS_FN("DistinctBy", seq_distinct, 1), MCS_FN("Aggregate", seq_aggregate, -1), \
    MCS_FN("SequenceEqual", seq_sequenceequal, 1), MCS_FN("Concat", seq_concat, 1), MCS_FN("Append", seq_append, 1), \
    MCS_FN("Zip", seq_zip, -1), MCS_FN("Chunk", seq_chunk, 1), \
    MCS_FN("ToDictionary", seq_todictionary, -1), MCS_FN("GroupBy", seq_groupby, 1), MCS_FN("ForEach", seq_foreach, 1), \
    MCS_FN("Find", seq_firstordefault, 1), MCS_FN("FindLast", seq_lastordefault, 1), MCS_FN("FindIndex", seq_findindex, 1), \
    MCS_FN("FindLastIndex", seq_findlastindex, 1), MCS_FN("FindAll", seq_findall, 1), MCS_FN("Exists", seq_any, 1), \
    MCS_FN("TrueForAll", seq_all, 1), MCS_FN("ConvertAll", seq_select, 1), MCS_FN("CopyTo", seq_copyto, -1), \
    MCS_FN("BinarySearch", seq_binarysearch, -1), MCS_FN("Cast", seq_tolist, 0), MCS_FN("AsEnumerable", seq_getenum, 0)
#if MCS_ENABLE_FLOAT
#define SEQ_FLOAT_REGS MCS_FN("Average", seq_average, -1),
#else
#define SEQ_FLOAT_REGS
#endif

static const mcs_reg_t array_methods[] = {
    SEQ_REGS, SEQ_FLOAT_REGS
    MCS_GET("Length", arr_length), MCS_GET("LongLength", arr_length), MCS_GET("Rank", arr_rank),
    MCS_FN("Clone", arr_clone, 0), MCS_FN("GetLength", arr_getlength, 1), MCS_FN("Reverse", seq_reverse_copy, 0),
    MCS_REG_END
};
static const mcs_reg_t array_statics[] = {
    MCS_FN("Sort", arrs_sort, -1), MCS_FN("Reverse", arrs_reverse, -1), MCS_FN("IndexOf", arrs_indexof, -1),
    MCS_FN("LastIndexOf", arrs_lastindexof, 2), MCS_FN("Fill", arrs_fill, -1), MCS_FN("Copy", arrs_copy, -1),
    MCS_FN("Clear", arrs_clear, -1), MCS_FN("Exists", arrs_exists, 2), MCS_FN("TrueForAll", arrs_trueforall, 2),
    MCS_FN("Find", arrs_find, 2), MCS_FN("FindLast", arrs_findlast, 2), MCS_FN("FindIndex", arrs_findindex, 2),
    MCS_FN("FindLastIndex", arrs_findlastindex, 2), MCS_FN("FindAll", arrs_findall, 2), MCS_FN("ForEach", arrs_foreach, 2),
    MCS_FN("BinarySearch", arrs_binarysearch, -1), MCS_FN("ConvertAll", arrs_convertall, 2), MCS_FN("Empty", arrs_empty, 0),
    MCS_REG_END
};
#if MCS_ENABLE_LIST
static const mcs_reg_t list_methods[] = {
    SEQ_REGS, SEQ_FLOAT_REGS
    MCS_GET("Count", list_count), MCS_GET("Capacity", list_capacity),
    MCS_FN("Add", list_add, 1), MCS_FN("AddRange", list_addrange, 1), MCS_FN("Insert", list_insert, 2),
    MCS_FN("InsertRange", list_insertrange, 2), MCS_FN("Remove", list_remove, 1), MCS_FN("RemoveAt", list_removeat, 1),
    MCS_FN("RemoveRange", list_removerange, 2), MCS_FN("RemoveAll", list_removeall, 1), MCS_FN("Clear", list_clear, 0),
    MCS_FN("Sort", list_sort, -1), MCS_FN("Reverse", list_reverse, -1), MCS_FN("GetRange", list_getrange, 2),
    MCS_FN("TrimExcess", list_trimexcess, 0), MCS_FN("AsReadOnly", list_asreadonly, 0),
    MCS_REG_END
};
#endif

/* ------------------------------------------------------------ Dictionary */
#if MCS_ENABLE_DICT
#define SELF_DICT() if (!IS_KIND(self, MCS_O_DICT)) { mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object."); return mcs_null(); } \
    mcs_dict_t* d = AS_DICT(self)
static bool key_ok(mcs_vm_t* vm, mcs_value_t k) {
    if (k.type == MCS_T_NULL) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'key')"); return false; }
    return true;
}
NATIVE(dict_new) {
    vm->gc_pause++;
    mcs_dict_t* d = mcs_new_dict(vm);
    if (argc >= 1 && IS_KIND(argv[0], MCS_O_DICT)) {
        mcs_dict_t* s = AS_DICT(argv[0]);
        for (uint32_t i = 0; i < s->count; i++) mcs_dict_set(vm, d, s->keys[i], s->vals[i]);
    }
    vm->gc_pause--;
    return OBJ_VAL(d);
}
NATIVE(dict_count) { SELF_DICT(); return mcs_int((mcs_int_t)d->count); }
static mcs_value_t dict_list(mcs_vm_t* vm, mcs_dict_t* d, int which) {
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_LIST, d->count);
    for (uint32_t i = 0; i < d->count; i++) {
        if (which == 0) o->items[i] = d->keys[i];
        else if (which == 1) o->items[i] = d->vals[i];
        else {
            mcs_instance_t* kv = mcs_new_instance(vm, vm->cls_kvp);
            kv->fields[0] = d->keys[i]; kv->fields[1] = d->vals[i];
            o->items[i] = OBJ_VAL(kv);
        }
    }
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(dict_keys) { SELF_DICT(); return dict_list(vm, d, 0); }
NATIVE(dict_values) { SELF_DICT(); return dict_list(vm, d, 1); }
NATIVE(dict_tolist) { SELF_DICT(); return dict_list(vm, d, 2); }
NATIVE(dict_add) {
    SELF_DICT();
    if (!key_ok(vm, argv[0])) return mcs_null();
    if (mcs_dict_get(d, argv[0], NULL)) {
        mcs_string_t* ks = mcs_value_to_string(vm, argv[0]); CHECK();
        mcs_throw(vm, EXC_ARGUMENT, "An item with the same key has already been added. Key: %s", ks ? ks->chars : "?");
        return mcs_null();
    }
    mcs_dict_set(vm, d, argv[0], argv[1]);
    return mcs_null();
}
NATIVE(dict_tryadd) {
    SELF_DICT();
    if (!key_ok(vm, argv[0])) return mcs_null();
    if (mcs_dict_get(d, argv[0], NULL)) return mcs_bool(false);
    mcs_dict_set(vm, d, argv[0], argv[1]);
    return mcs_bool(true);
}
NATIVE(dict_containskey) { SELF_DICT(); if (!key_ok(vm, argv[0])) return mcs_null(); return mcs_bool(mcs_dict_get(d, argv[0], NULL)); }
NATIVE(dict_trygetvalue) { /* TryGetValue(key, out value); missing key stores null (no static TValue default) */
    SELF_DICT(); if (!key_ok(vm, argv[0])) return mcs_null();
    mcs_value_t v = mcs_null();
    bool ok = mcs_dict_get(d, argv[0], &v);
    lib_out_set(argv[1], ok ? v : mcs_null());
    return mcs_bool(ok);
}
NATIVE(dict_containsvalue) {
    SELF_DICT();
    for (uint32_t i = 0; i < d->count; i++) { if (lib_equals(vm, d->vals[i], argv[0])) return mcs_bool(true); CHECK(); }
    return mcs_bool(false);
}
NATIVE(dict_remove) { SELF_DICT(); if (!key_ok(vm, argv[0])) return mcs_null(); return mcs_bool(mcs_dict_remove(vm, d, argv[0])); }
NATIVE(dict_clear) { SELF_DICT(); mcs_dict_clear(vm, d); return mcs_null(); }
NATIVE(dict_getordefault) {
    SELF_DICT();
    if (!key_ok(vm, argv[0])) return mcs_null();
    mcs_value_t v;
    if (mcs_dict_get(d, argv[0], &v)) return v;
    return argc > 1 ? argv[1] : mcs_null();
}
NATIVE(dict_getenum) { return self; }
/* LINQ on dictionaries: materialise KeyValuePairs then forward */
#define DICT_LINQ(fname, target) NATIVE(fname) { \
    SELF_DICT(); mcs_value_t lst = dict_list(vm, d, 2); mcs_push_root(vm, lst); \
    mcs_value_t r = target(vm, lst, argc, argv); mcs_pop_root(vm, 1); return r; }
DICT_LINQ(dict_where, seq_where)
DICT_LINQ(dict_select, seq_select)
DICT_LINQ(dict_any, seq_any)
DICT_LINQ(dict_all, seq_all)
DICT_LINQ(dict_countm, seq_count)
DICT_LINQ(dict_sum, seq_sum)
DICT_LINQ(dict_min, seq_min)
DICT_LINQ(dict_max, seq_max)
DICT_LINQ(dict_first, seq_first)
DICT_LINQ(dict_firstordefault, seq_firstordefault)
DICT_LINQ(dict_orderby, seq_orderby)
DICT_LINQ(dict_orderbydesc, seq_orderbydesc)
DICT_LINQ(dict_todictionary, seq_todictionary)
DICT_LINQ(dict_aggregate, seq_aggregate)
DICT_LINQ(dict_foreach, seq_foreach)
DICT_LINQ(dict_maxby, seq_maxby)
DICT_LINQ(dict_minby, seq_minby)
static const mcs_reg_t dict_methods[] = {
    MCS_GET("Count", dict_count), MCS_GET("Keys", dict_keys), MCS_GET("Values", dict_values),
    MCS_FN("Add", dict_add, 2), MCS_FN("TryAdd", dict_tryadd, 2), MCS_FN("ContainsKey", dict_containskey, 1), MCS_FN("TryGetValue", dict_trygetvalue, 2),
    MCS_FN("ContainsValue", dict_containsvalue, 1), MCS_FN("Remove", dict_remove, 1), MCS_FN("Clear", dict_clear, 0),
    MCS_FN("GetValueOrDefault", dict_getordefault, -1), MCS_FN("GetEnumerator", dict_getenum, 0),
    MCS_FN("ToList", dict_tolist, 0), MCS_FN("ToArray", dict_tolist, 0), MCS_FN("Where", dict_where, 1),
    MCS_FN("Select", dict_select, 1), MCS_FN("Any", dict_any, -1), MCS_FN("All", dict_all, 1), MCS_FN("Count", dict_countm, -1),
    MCS_FN("Sum", dict_sum, -1), MCS_FN("Min", dict_min, -1), MCS_FN("Max", dict_max, -1), MCS_FN("First", dict_first, -1),
    MCS_FN("FirstOrDefault", dict_firstordefault, -1), MCS_FN("OrderBy", dict_orderby, -1),
    MCS_FN("OrderByDescending", dict_orderbydesc, -1), MCS_FN("ToDictionary", dict_todictionary, -1),
    MCS_FN("Aggregate", dict_aggregate, -1), MCS_FN("ForEach", dict_foreach, 1), MCS_FN("MaxBy", dict_maxby, 1),
    MCS_FN("MinBy", dict_minby, 1),
    MCS_REG_END
};
#endif

/* ------------------------------------------------------------ HashSet / Stack / Queue
 * Script-visible instances with a single hidden field holding the storage. */
static mcs_value_t inner(mcs_vm_t* vm, mcs_value_t self, uint8_t kind) {
    if (IS_KIND(self, MCS_O_INSTANCE) && AS_INSTANCE(self)->cls->field_count >= 1 && IS_KIND(AS_INSTANCE(self)->fields[0], kind))
        return AS_INSTANCE(self)->fields[0];
    mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
    return mcs_null();
}
static mcs_value_t wrap_new(mcs_vm_t* vm, mcs_value_t cls, uint8_t kind) {
    vm->gc_pause++;
    mcs_instance_t* in = mcs_new_instance(vm, AS_CLASS(cls));
    in->fields[0] = kind == MCS_O_DICT ? OBJ_VAL(mcs_new_dict(vm)) : OBJ_VAL(mcs_new_listobj(vm, MCS_O_LIST, 0));
    vm->gc_pause--;
    return OBJ_VAL(in);
}
#if MCS_ENABLE_DICT
#define SET() mcs_value_t sv = inner(vm, self, MCS_O_DICT); CHECK(); mcs_dict_t* d = AS_DICT(sv)
static mcs_value_t norm_key(mcs_value_t k) {
    if (k.type == MCS_T_CHAR) return k; /* chars hash like ints but keep identity */
    return k;
}
static void set_add_all(mcs_vm_t* vm, mcs_dict_t* d, mcs_value_t src) {
    if (lib_is_seq(src)) { mcs_list_t* l = AS_LIST(src); for (uint32_t i = 0; i < l->count; i++) mcs_dict_set(vm, d, norm_key(l->items[i]), mcs_bool(true)); }
    else if (IS_KIND(src, MCS_O_DICT)) { mcs_dict_t* s = AS_DICT(src); for (uint32_t i = 0; i < s->count; i++) mcs_dict_set(vm, d, s->keys[i], mcs_bool(true)); }
    else if (IS_KIND(src, MCS_O_INSTANCE) && AS_INSTANCE(src)->cls->field_count >= 1 && IS_KIND(AS_INSTANCE(src)->fields[0], MCS_O_DICT)) set_add_all(vm, d, AS_INSTANCE(src)->fields[0]);
}
static mcs_dict_t* set_of(mcs_value_t v) {
    if (IS_KIND(v, MCS_O_INSTANCE) && AS_INSTANCE(v)->cls->field_count >= 1 && IS_KIND(AS_INSTANCE(v)->fields[0], MCS_O_DICT)) return AS_DICT(AS_INSTANCE(v)->fields[0]);
    return NULL;
}
NATIVE(set_new) {
    mcs_value_t s = wrap_new(vm, self, MCS_O_DICT);
    if (argc >= 1) { mcs_push_root(vm, s); set_add_all(vm, AS_DICT(AS_INSTANCE(s)->fields[0]), argv[0]); mcs_pop_root(vm, 1); }
    return s;
}
NATIVE(set_add) { SET(); if (mcs_dict_get(d, argv[0], NULL)) return mcs_bool(false); mcs_dict_set(vm, d, norm_key(argv[0]), mcs_bool(true)); return mcs_bool(true); }
NATIVE(set_remove) { SET(); return mcs_bool(mcs_dict_remove(vm, d, argv[0])); }
NATIVE(set_contains) { SET(); return mcs_bool(mcs_dict_get(d, argv[0], NULL)); }
NATIVE(set_clear) { SET(); mcs_dict_clear(vm, d); return mcs_null(); }
NATIVE(set_count) { SET(); return mcs_int((mcs_int_t)d->count); }
NATIVE(set_union) { SET(); set_add_all(vm, d, argv[0]); return mcs_null(); }
static bool in_other(mcs_value_t other, mcs_value_t k) {
    mcs_dict_t* od = set_of(other);
    if (od) return mcs_dict_get(od, k, NULL);
    if (IS_KIND(other, MCS_O_DICT)) return mcs_dict_get(AS_DICT(other), k, NULL);
    if (lib_is_seq(other)) { mcs_list_t* l = AS_LIST(other); for (uint32_t i = 0; i < l->count; i++) if (mcs_values_equal(l->items[i], k)) return true; }
    return false;
}
NATIVE(set_intersect) {
    SET();
    for (uint32_t i = d->count; i-- > 0;) if (!in_other(argv[0], d->keys[i])) mcs_dict_remove(vm, d, d->keys[i]);
    return mcs_null();
}
NATIVE(set_except) {
    SET();
    for (uint32_t i = d->count; i-- > 0;) if (in_other(argv[0], d->keys[i])) mcs_dict_remove(vm, d, d->keys[i]);
    return mcs_null();
}
NATIVE(set_subset) { SET(); for (uint32_t i = 0; i < d->count; i++) if (!in_other(argv[0], d->keys[i])) return mcs_bool(false); return mcs_bool(true); }
NATIVE(set_overlaps) { SET(); for (uint32_t i = 0; i < d->count; i++) if (in_other(argv[0], d->keys[i])) return mcs_bool(true); return mcs_bool(false); }
NATIVE(set_tolist) { SET(); return dict_list(vm, d, 0); }
static const mcs_reg_t set_methods[] = {
    MCS_FN("Add", set_add, 1), MCS_FN("Remove", set_remove, 1), MCS_FN("Contains", set_contains, 1),
    MCS_FN("Clear", set_clear, 0), MCS_GET("Count", set_count), MCS_FN("UnionWith", set_union, 1),
    MCS_FN("IntersectWith", set_intersect, 1), MCS_FN("ExceptWith", set_except, 1), MCS_FN("IsSubsetOf", set_subset, 1),
    MCS_FN("Overlaps", set_overlaps, 1), MCS_FN("ToList", set_tolist, 0), MCS_FN("ToArray", set_tolist, 0),
    MCS_FN("GetEnumerator", set_tolist, 0), MCS_REG_END
};
#endif

#define STK() mcs_value_t lv = inner(vm, self, MCS_O_LIST); CHECK(); mcs_list_t* l = AS_LIST(lv)
NATIVE(stack_new) {
    mcs_value_t s = wrap_new(vm, self, MCS_O_LIST);
    if (argc >= 1 && lib_is_seq(argv[0])) {
        mcs_list_t* src = AS_LIST(argv[0]), *l = AS_LIST(AS_INSTANCE(s)->fields[0]);
        mcs_push_root(vm, s);
        for (uint32_t i = 0; i < src->count; i++) mcs_listobj_push(vm, l, src->items[i]);
        mcs_pop_root(vm, 1);
    }
    return s;
}
NATIVE(stk_push) { STK(); mcs_listobj_push(vm, l, argv[0]); return mcs_null(); }
NATIVE(stk_pop) { STK(); if (!l->count) { mcs_throw(vm, EXC_INVOP, "Stack empty."); return mcs_null(); } return l->items[--l->count]; }
NATIVE(stk_peek) { STK(); if (!l->count) { mcs_throw(vm, EXC_INVOP, "Stack empty."); return mcs_null(); } return l->items[l->count - 1]; }
NATIVE(stk_trypop) { STK(); bool ok = l->count > 0; lib_out_set(argv[0], ok ? l->items[--l->count] : mcs_null()); return mcs_bool(ok); }
NATIVE(stk_trypeek) { STK(); bool ok = l->count > 0; lib_out_set(argv[0], ok ? l->items[l->count - 1] : mcs_null()); return mcs_bool(ok); }
NATIVE(coll_count) { STK(); return mcs_int((mcs_int_t)l->count); }
NATIVE(coll_clear) { STK(); l->count = 0; return mcs_null(); }
NATIVE(coll_contains) { STK(); for (uint32_t i = 0; i < l->count; i++) if (lib_equals(vm, l->items[i], argv[0])) return mcs_bool(true); return mcs_bool(false); }
NATIVE(stk_toarray) {
    STK();
    vm->gc_pause++;
    mcs_list_t* o = mcs_new_listobj(vm, MCS_O_ARRAY, l->count);
    for (uint32_t i = 0; i < l->count; i++) o->items[i] = l->items[l->count - 1 - i];
    vm->gc_pause--;
    return OBJ_VAL(o);
}
NATIVE(q_dequeue) {
    STK();
    if (!l->count) { mcs_throw(vm, EXC_INVOP, "Queue empty."); return mcs_null(); }
    mcs_value_t v = l->items[0];
    mcs_listobj_remove_at(l, 0);
    return v;
}
NATIVE(q_trydequeue) {
    STK(); bool ok = l->count > 0;
    mcs_value_t v = ok ? l->items[0] : mcs_null();
    if (ok) mcs_listobj_remove_at(l, 0);
    lib_out_set(argv[0], v);
    return mcs_bool(ok);
}
NATIVE(q_trypeek) { STK(); bool ok = l->count > 0; lib_out_set(argv[0], ok ? l->items[0] : mcs_null()); return mcs_bool(ok); }
NATIVE(q_peek) { STK(); if (!l->count) { mcs_throw(vm, EXC_INVOP, "Queue empty."); return mcs_null(); } return l->items[0]; }
NATIVE(q_toarray) { STK(); return copy_seq(vm, l, MCS_O_ARRAY, 0, l->count); }
static const mcs_reg_t stack_methods[] = {
    MCS_FN("Push", stk_push, 1), MCS_FN("Pop", stk_pop, 0), MCS_FN("Peek", stk_peek, 0),
    MCS_FN("TryPop", stk_trypop, 1), MCS_FN("TryPeek", stk_trypeek, 1), MCS_GET("Count", coll_count),
    MCS_FN("Clear", coll_clear, 0), MCS_FN("Contains", coll_contains, 1), MCS_FN("ToArray", stk_toarray, 0),
    MCS_FN("GetEnumerator", stk_toarray, 0), MCS_REG_END
};
static const mcs_reg_t queue_methods[] = {
    MCS_FN("Enqueue", stk_push, 1), MCS_FN("Dequeue", q_dequeue, 0), MCS_FN("Peek", q_peek, 0),
    MCS_FN("TryDequeue", q_trydequeue, 1), MCS_FN("TryPeek", q_trypeek, 1), MCS_GET("Count", coll_count),
    MCS_FN("Clear", coll_clear, 0), MCS_FN("Contains", coll_contains, 1), MCS_FN("ToArray", q_toarray, 0),
    MCS_FN("GetEnumerator", q_toarray, 0), MCS_REG_END
};

static mcs_class_t* wrapper_class(mcs_vm_t* vm, const char* name, mcs_native_fn ctor, const mcs_reg_t* regs) {
    mcs_class_t* c = mcs_define_builtin_class(vm, name, vm->cls_object, CLS_BUILTIN);
    vm->gc_pause++;
    mcs_class_add_field(vm, c, mcs_intern_c(vm, "$items"), mcs_null());
    vm->gc_pause--;
    c->native_ctor = ctor;
    mcs_add_regs(vm, c, regs, false);
    return c;
}

void mcs_lib_open_collections(mcs_vm_t* vm, uint8_t mask) {
    vm->cls_array = mcs_define_builtin_class(vm, "Array", vm->cls_object, CLS_BUILTIN);
    mcs_add_regs(vm, vm->cls_array, array_methods, false);
    mcs_add_regs(vm, vm->cls_array, array_statics, true);
    vm->cls_list = mcs_define_builtin_class(vm, "List", vm->cls_object, CLS_BUILTIN);
    vm->cls_dict = mcs_define_builtin_class(vm, "Dictionary", vm->cls_object, CLS_BUILTIN);
    if (!(mask & MCS_LIB_COLLECTIONS)) return;
#if MCS_ENABLE_LIST
    vm->cls_list->native_ctor = list_new;
    mcs_add_regs(vm, vm->cls_list, list_methods, false);
    mcs_register_module(vm, "Enumerable", enumerable_fns);
    {
        mcs_class_t* st = wrapper_class(vm, "Stack", stack_new, stack_methods);
        mcs_class_t* q = wrapper_class(vm, "Queue", stack_new, queue_methods);
        (void)st; (void)q;
    }
#endif
#if MCS_ENABLE_DICT
    vm->cls_dict->native_ctor = dict_new;
    mcs_add_regs(vm, vm->cls_dict, dict_methods, false);
    wrapper_class(vm, "HashSet", set_new, set_methods);
#endif
}
