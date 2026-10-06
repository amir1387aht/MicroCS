/* MicroCS virtual machine: dispatch loop, calls, members, exceptions, API. */
#include "mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#if MCS_ENABLE_FLOAT
#include <math.h>
#endif

const uint8_t mcs_op_len[OP__COUNT] = {
#define X(name, len) len,
    MCS_OPCODES(X)
#undef X
};
const char* const mcs_op_name[OP__COUNT] = {
#define X(name, len) #name,
    MCS_OPCODES(X)
#undef X
};

/* ================================================================ output */
void mcs_write(mcs_vm_t* vm, const char* s, size_t n) {
    if (vm->cfg.write_fn) vm->cfg.write_fn(vm->cfg.user_data, s, n);
    else { fwrite(s, 1, n, stdout); }
}
void mcs_report_error(mcs_vm_t* vm, const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = sizeof buf - 1;
    if (vm->cfg.error_fn) vm->cfg.error_fn(vm->cfg.user_data, buf, (size_t)n);
    else if (vm->cfg.write_fn) vm->cfg.write_fn(vm->cfg.user_data, buf, (size_t)n);
    else { fflush(stdout); fwrite(buf, 1, (size_t)n, stderr); }
}

/* ================================================================ globals */
uint32_t mcs_global_slot(mcs_vm_t* vm, mcs_string_t* name) {
    if (name->obj.aux) return (uint32_t)name->obj.aux - 1u;
    if (vm->global_count >= 0xFFFFu) mcs_panic(vm, MCS_ERR_MEMORY, "too many globals");
    if (vm->global_count == vm->global_cap) {
        uint32_t nc = vm->global_cap < 64 ? 64 : vm->global_cap + vm->global_cap / 2; /* 1.5x: smaller steps on small heaps */
        vm->globals = MCS_GROW(vm, mcs_value_t, vm->globals, vm->global_cap, nc);
        vm->global_names = MCS_GROW(vm, mcs_string_t*, vm->global_names, vm->global_cap, nc);
        vm->global_cap = nc;
    }
    uint32_t slot = vm->global_count++;
    vm->globals[slot] = mcs_undef();
    vm->global_names[slot] = name;   /* keeps the name (and its aux slot) alive */
    name->obj.aux = (uint16_t)(slot + 1u);
    return slot;
}
void mcs_set_global(mcs_vm_t* vm, const char* name, mcs_value_t v) {
    vm->gc_pause++;
    uint32_t s = mcs_global_slot(vm, mcs_intern_c(vm, name));
    vm->gc_pause--;
    vm->globals[s] = v;
}
mcs_value_t mcs_get_global(mcs_vm_t* vm, const char* name) {
    mcs_string_t* s = mcs_find_interned(vm, name, strlen(name));
    if (!s || !s->obj.aux) return mcs_null();
    mcs_value_t v = vm->globals[s->obj.aux - 1u];
    return v.type == MCS_T_UNDEF ? mcs_null() : v;
}

/* ================================================================ exceptions */
uint32_t mcs_line_of(mcs_function_t* fn, uint32_t pc) {
    uint32_t line = 0;
    for (uint32_t i = 0; i < fn->line_count; i++) { if (fn->lines[i].pc > pc) break; line = fn->lines[i].line; }
    return line;
}

static mcs_string_t* build_trace(mcs_vm_t* vm) {
    mcs_buf_t b; mcs_buf_init(&b, vm);
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        mcs_frame_t* f = &vm->frames[i];
        mcs_function_t* fn = f->closure->fn;
        uint32_t pc = (uint32_t)(f->ip - fn->code);
        if (pc) pc--;
        char line[160];
        snprintf(line, sizeof line, "   at %s in %s:line %u\n", fn->name ? fn->name->chars : "?",
                 fn->source ? fn->source->chars : "?", (unsigned)mcs_line_of(fn, pc));
        mcs_buf_puts(&b, line);
        if (vm->frame_count - i > 20) { mcs_buf_puts(&b, "   ...\n"); break; }
    }
    return mcs_buf_to_string(&b);
}

mcs_instance_t* mcs_make_exception(mcs_vm_t* vm, mcs_class_t* cls, const char* msg) {
    vm->gc_pause++;
    mcs_instance_t* e = mcs_new_instance(vm, cls);
    mcs_value_t slot;
    if (mcs_table_get_s(&cls->fields, vm->s_message, &slot)) e->fields[slot.as.i] = OBJ_VAL(mcs_intern_c(vm, msg));
    vm->gc_pause--;
    return e;
}

void mcs_throw_value(mcs_vm_t* vm, mcs_value_t exc) {
    if (IS_KIND(exc, MCS_O_INSTANCE) && vm->frame_count > 0) {
        mcs_instance_t* in = AS_INSTANCE(exc);
        mcs_value_t slot;
        mcs_string_t* st = mcs_intern_c(vm, "StackTrace");
        if (mcs_table_get_s(&in->cls->fields, st, &slot) && in->fields[slot.as.i].type == MCS_T_NULL) {
            vm->gc_pause++;
            in->fields[slot.as.i] = OBJ_VAL(build_trace(vm));
            vm->gc_pause--;
        }
    }
    vm->has_exc = true;
    vm->exc_value = exc;
}

void mcs_throw(mcs_vm_t* vm, int kind, const char* fmt, ...) {
    char msg[200];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (vm->has_exc) return; /* keep first */
    mcs_throw_value(vm, OBJ_VAL(mcs_make_exception(vm, vm->exc[kind], msg)));
}

void mcs_raise(mcs_vm_t* vm, const char* cls_name, const char* fmt, ...) {
    char msg[200];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (vm->has_exc) return;
    mcs_class_t* cls = vm->exc[EXC_EXCEPTION];
    mcs_value_t g = mcs_get_global(vm, cls_name ? cls_name : "Exception");
    if (IS_KIND(g, MCS_O_CLASS)) cls = AS_CLASS(g);
    mcs_throw_value(vm, OBJ_VAL(mcs_make_exception(vm, cls, msg)));
}
bool mcs_has_exception(mcs_vm_t* vm) { return vm->has_exc; }

/* ================================================================ type tests */
bool mcs_is_instance_of(mcs_vm_t* vm, mcs_value_t v, mcs_string_t* tn) {
    const char* n = tn->chars;
    switch (v.type) {
    case MCS_T_NULL: case MCS_T_UNDEF: return false;
    case MCS_T_INT: return !strcmp(n, "int") || !strcmp(n, "object") || !strcmp(n, "Int32") || !strcmp(n, "long");
    case MCS_T_FLOAT: return !strcmp(n, "double") || !strcmp(n, "object") || !strcmp(n, "float") || !strcmp(n, "Double");
    case MCS_T_BOOL: return !strcmp(n, "bool") || !strcmp(n, "object") || !strcmp(n, "Boolean");
    case MCS_T_CHAR: return !strcmp(n, "char") || !strcmp(n, "object") || !strcmp(n, "Char");
    default: break;
    }
    if (!strcmp(n, "object")) return true;
    if (IS_STRING(v)) return !strcmp(n, "string") || !strcmp(n, "String");
    if (IS_KIND(v, MCS_O_ARRAY)) return !strcmp(n, "Array");
    mcs_class_t* cls = mcs_class_of(vm, v);
    for (mcs_class_t* c = cls; c; c = c->super) if (c->name == tn) return true;
    if (cls && mcs_table_get_s(&cls->ifaces, tn, NULL)) return true;
    return false;
}

/* ================================================================ upvalues */
static mcs_upvalue_t* capture_upvalue(mcs_vm_t* vm, mcs_value_t* slot) {
    mcs_upvalue_t* prev = NULL;
    mcs_upvalue_t* u = vm->open_upvalues;
    while (u && u->location > slot) { prev = u; u = u->next_open; }
    if (u && u->location == slot) return u;
    mcs_upvalue_t* nu = mcs_new_upvalue(vm, slot);
    nu->next_open = u;
    if (prev) prev->next_open = nu; else vm->open_upvalues = nu;
    return nu;
}
static void close_upvalues(mcs_vm_t* vm, mcs_value_t* last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        mcs_upvalue_t* u = vm->open_upvalues;
        u->closed = *u->location;
        u->location = &u->closed;
        vm->open_upvalues = u->next_open;
    }
}

/* ================================================================ calls */
#define PUSH(v) (*vm->sp++ = (v))
#define POP() (*--vm->sp)
#define PEEK(n) (vm->sp[-1 - (n)])

enum { CALL_ERR = 0, CALL_DONE = 1, CALL_FRAME = 2 };

static const char* callee_name(mcs_value_t f) {
    if (IS_KIND(f, MCS_O_CLOSURE)) return AS_CLOSURE(f)->fn->name ? AS_CLOSURE(f)->fn->name->chars : "?";
    if (IS_KIND(f, MCS_O_NATIVE)) return AS_NATIVE(f)->name ? AS_NATIVE(f)->name->chars : "?";
    return "?";
}

static int call_closure(mcs_vm_t* vm, mcs_closure_t* cl, int argc) {
    mcs_function_t* fn = cl->fn;
    int orig = argc;
    bool pass_array = argc == fn->arity && (IS_KIND(PEEK(0), MCS_O_ARRAY) || mcs_is_null(PEEK(0)));
    if (argc != fn->arity || ((fn->flags & FN_HAS_PARAMS) && !pass_array)) {
        if ((fn->flags & FN_HAS_PARAMS) && argc >= fn->arity - 1 && !pass_array) {
            int extra = argc - (fn->arity - 1);
            mcs_list_t* arr = mcs_new_listobj(vm, MCS_O_ARRAY, (uint32_t)extra);
            for (int i = 0; i < extra; i++) arr->items[i] = vm->sp[-extra + i];
            vm->sp -= extra;
            PUSH(OBJ_VAL(arr));
            argc = fn->arity;
            orig = argc;
        } else if (argc >= fn->min_arity && argc < fn->arity) {
            if (vm->sp + (fn->arity - argc) >= vm->stack_end) { mcs_throw(vm, EXC_STACKOVF, "Stack overflow."); return CALL_ERR; }
            while (argc < fn->arity) { PUSH(mcs_null()); argc++; }
        } else {
            mcs_throw(vm, EXC_ARGUMENT, "No overload for method '%s' takes %d arguments", callee_name(OBJ_VAL(cl)), argc);
            return CALL_ERR;
        }
    }
    if (vm->frame_count >= vm->cfg.max_frames || vm->sp + fn->max_slots + MCS_STACK_MARGIN >= vm->stack_end) {
        mcs_throw(vm, EXC_STACKOVF, "Stack overflow (call depth %d).", vm->frame_count);
        return CALL_ERR;
    }
    mcs_frame_t* f = &vm->frames[vm->frame_count++];
    f->closure = cl; f->ip = fn->code; f->slots = vm->sp - argc - 1; f->argc = (uint8_t)orig;
    return CALL_FRAME;
}

static int call_native(mcs_vm_t* vm, mcs_native_t* nf, mcs_value_t self, int argc) {
    if (nf->arity >= 0 && argc != nf->arity) {
        mcs_throw(vm, EXC_ARGUMENT, "No overload for method '%s' takes %d arguments", nf->name ? nf->name->chars : "?", argc);
        return CALL_ERR;
    }
    mcs_value_t* args = vm->sp - argc;
    mcs_value_t r = nf->fn(vm, self, argc, args);
    if (vm->has_exc) return CALL_ERR;
    vm->sp -= argc + 1;
    PUSH(r);
    return CALL_DONE;
}

static bool sig_matches(mcs_vm_t* vm, mcs_value_t f, int argc) {
    if (IS_KIND(f, MCS_O_NATIVE)) return AS_NATIVE(f)->arity < 0 || AS_NATIVE(f)->arity == argc;
    if (!IS_KIND(f, MCS_O_CLOSURE)) return false;
    mcs_function_t* fn = AS_CLOSURE(f)->fn;
    bool ok = argc == fn->arity || (argc >= fn->min_arity && argc < fn->arity) || ((fn->flags & FN_HAS_PARAMS) && argc >= fn->arity - 1);
    if (!ok) return false;
    if (fn->param_types) {
        int n = argc < fn->arity ? argc : fn->arity;
        for (int i = 0; i < n; i++) {
            mcs_value_t a = vm->sp[-argc + i];
            switch (fn->param_types[i]) {
            case PT_INT: if (a.type != MCS_T_INT && a.type != MCS_T_CHAR) return false; break;
            case PT_FLOAT: if (a.type != MCS_T_FLOAT && a.type != MCS_T_INT) return false; break;
            case PT_BOOL: if (a.type != MCS_T_BOOL) return false; break;
            case PT_STRING: if (!IS_STRING(a) && a.type != MCS_T_NULL) return false; break;
            case PT_CHAR: if (a.type != MCS_T_CHAR) return false; break;
            default: break;
            }
        }
    }
    return true;
}

static mcs_value_t pick_overload(mcs_vm_t* vm, mcs_overloads_t* o, int argc) {
    /* exact arity + types first, then loose */
    for (uint32_t i = 0; i < o->count; i++) {
        mcs_value_t f = o->items[i];
        if (IS_KIND(f, MCS_O_CLOSURE) && AS_CLOSURE(f)->fn->arity != argc) continue;
        if (IS_KIND(f, MCS_O_NATIVE) && AS_NATIVE(f)->arity != argc) continue;
        if (sig_matches(vm, f, argc)) return f;
    }
    for (uint32_t i = 0; i < o->count; i++) if (sig_matches(vm, o->items[i], argc)) return o->items[i];
    return mcs_null();
}

/* call `method` with receiver already in callee slot (sp[-argc-1]) */
static int call_method(mcs_vm_t* vm, mcs_value_t method, int argc) {
    if (IS_KIND(method, MCS_O_OVERLOADS)) {
        mcs_value_t m = pick_overload(vm, AS_OVL(method), argc);
        if (m.type == MCS_T_NULL) { mcs_throw(vm, EXC_ARGUMENT, "No overload takes %d arguments", argc); return CALL_ERR; }
        method = m;
    }
    if (IS_KIND(method, MCS_O_CLOSURE)) return call_closure(vm, AS_CLOSURE(method), argc);
    if (IS_KIND(method, MCS_O_NATIVE)) return call_native(vm, AS_NATIVE(method), vm->sp[-argc - 1], argc);
    mcs_throw(vm, EXC_INVOP, "member is not a method");
    return CALL_ERR;
}

static int call_value(mcs_vm_t* vm, mcs_value_t callee, int argc) {
    if (IS_OBJ(callee)) {
        switch (OBJ_KIND(callee)) {
        case MCS_O_CLOSURE: return call_closure(vm, AS_CLOSURE(callee), argc);
        case MCS_O_NATIVE: return call_native(vm, AS_NATIVE(callee), mcs_null(), argc);
        case MCS_O_OVERLOADS: return call_method(vm, callee, argc);
        case MCS_O_BOUND: {
            mcs_bound_t* b = AS_BOUND(callee);
            vm->sp[-argc - 1] = b->receiver;
            return call_method(vm, b->method, argc);
        }
        case MCS_O_CLASS: {
            mcs_class_t* cls = AS_CLASS(callee);
            if (cls->ckind == CLS_STATIC || cls->ckind == CLS_INTERFACE) {
                mcs_throw(vm, EXC_INVOP, "Cannot create an instance of the static class or interface '%s'", cls->name->chars);
                return CALL_ERR;
            }
            if (cls->def) {
                mcs_userdata_t* u = mcs_new_userdata(vm, cls, cls->def->instance_size);
                vm->sp[-argc - 1] = OBJ_VAL(u);
                if (cls->def->ctor) {
                    cls->def->ctor(vm, OBJ_VAL(u), argc, vm->sp - argc);
                    if (vm->has_exc) return CALL_ERR;
                }
                vm->sp -= argc;
                return CALL_DONE;
            }
            if (cls->native_ctor) {
                mcs_value_t r = cls->native_ctor(vm, callee, argc, vm->sp - argc);
                if (vm->has_exc) return CALL_ERR;
                vm->sp -= argc + 1;
                PUSH(r);
                return CALL_DONE;
            }
            mcs_instance_t* in = mcs_new_instance(vm, cls);
            vm->sp[-argc - 1] = OBJ_VAL(in);
            mcs_value_t ctor;
            if (mcs_cls_get(vm, cls, MCS_TAB_METHODS, vm->s_ctor, &ctor)) return call_method(vm, ctor, argc);
            if (argc != 0) { mcs_throw(vm, EXC_ARGUMENT, "'%s' does not contain a constructor that takes %d arguments", cls->name->chars, argc); return CALL_ERR; }
            return CALL_DONE;
        }
        default: break;
        }
    }
    if (callee.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
    else if (callee.type == MCS_T_UNDEF) mcs_throw(vm, EXC_MISSINGMEMBER, "The name does not exist in the current context");
    else mcs_throw(vm, EXC_INVOP, "Value of type '%s' is not callable", mcs_type_name(vm, callee));
    return CALL_ERR;
}

/* ================================================================ members */
static void missing_member(mcs_vm_t* vm, mcs_value_t obj, mcs_string_t* name) {
    if (obj.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object. (accessing '%s')", name->chars);
    else mcs_throw(vm, EXC_MISSINGMEMBER, "'%s' does not contain a definition for '%s'", mcs_type_name(vm, obj), name->chars);
}

/* Nullable<T> members on erased values: x.HasValue / x.Value (only when the
 * value's own type has no such member) */
static int nullable_member(mcs_vm_t* vm, mcs_value_t obj, mcs_string_t* name) {
    if (name->len == 8 && memcmp(name->chars, "HasValue", 8) == 0) { vm->sp[-1] = mcs_bool(obj.type != MCS_T_NULL); return CALL_DONE; }
    if (name->len == 5 && memcmp(name->chars, "Value", 5) == 0) {
        if (obj.type == MCS_T_NULL) { mcs_throw(vm, EXC_INVOP, "Nullable object must have a value."); return CALL_ERR; }
        return CALL_DONE;
    }
    missing_member(vm, obj, name);
    return CALL_ERR;
}

/* replaces obj at stack top with obj.name */
static int get_member_op(mcs_vm_t* vm, mcs_string_t* name) {
    mcs_value_t obj = PEEK(0), v;
    mcs_class_t* cls;
    if (IS_KIND(obj, MCS_O_INSTANCE)) {
        mcs_instance_t* in = AS_INSTANCE(obj);
        if (mcs_table_get_s(&in->cls->fields, name, &v)) { vm->sp[-1] = in->fields[v.as.i]; return CALL_DONE; }
        cls = in->cls;
    } else if (IS_KIND(obj, MCS_O_CLASS)) {
        cls = AS_CLASS(obj);
        for (mcs_class_t* c = cls; c; c = c->super)
            if (mcs_cls_get(vm, c, MCS_TAB_STATICS, name, &v)) {
                if (v.type == MCS_T_UNDEF) break;
                vm->sp[-1] = v; return CALL_DONE;
            }
        if (mcs_cls_get(vm, cls, MCS_TAB_GETTERS, name, &v)) return call_method(vm, v, 0);
        missing_member(vm, obj, name);
        return CALL_ERR;
    } else {
        cls = mcs_class_of(vm, obj);
        if (!cls) return nullable_member(vm, obj, name);
    }
    if (mcs_cls_get(vm, cls, MCS_TAB_GETTERS, name, &v)) return call_method(vm, v, 0);
    if (mcs_cls_get(vm, cls, MCS_TAB_METHODS, name, &v)) { vm->sp[-1] = OBJ_VAL(mcs_new_bound(vm, obj, v)); return CALL_DONE; }
    return nullable_member(vm, obj, name);
}

/* [obj val] -> [val] */
static int set_member_op(mcs_vm_t* vm, mcs_string_t* name) {
    mcs_value_t obj = PEEK(1), val = PEEK(0), v;
    mcs_class_t* cls;
    if (IS_KIND(obj, MCS_O_INSTANCE)) {
        mcs_instance_t* in = AS_INSTANCE(obj);
        if (mcs_table_get_s(&in->cls->fields, name, &v)) { in->fields[v.as.i] = val; vm->sp--; vm->sp[-1] = val; return CALL_DONE; }
        cls = in->cls;
    } else if (IS_KIND(obj, MCS_O_CLASS)) {
        cls = AS_CLASS(obj);
        for (mcs_class_t* c = cls; c; c = c->super)
            if (mcs_cls_get(vm, c, MCS_TAB_STATICS, name, &v)) { mcs_table_set(vm, &c->statics, OBJ_VAL(name), val); vm->sp--; vm->sp[-1] = val; return CALL_DONE; }
    } else {
        cls = mcs_class_of(vm, obj);
        if (!cls) { missing_member(vm, obj, name); return CALL_ERR; }
    }
    if (mcs_cls_get(vm, cls, MCS_TAB_SETTERS, name, &v)) return call_method(vm, v, 1);
    if (mcs_cls_get(vm, cls, MCS_TAB_GETTERS, name, NULL)) { mcs_throw(vm, EXC_INVOP, "Property '%s' is read only", name->chars); return CALL_ERR; }
    missing_member(vm, obj, name);
    return CALL_ERR;
}

static int invoke_op(mcs_vm_t* vm, mcs_string_t* name, int argc) {
    mcs_value_t recv = PEEK(argc), v;
    mcs_class_t* cls;
    if (IS_KIND(recv, MCS_O_INSTANCE)) {
        mcs_instance_t* in = AS_INSTANCE(recv);
        if (mcs_table_get_s(&in->cls->fields, name, &v)) { vm->sp[-argc - 1] = in->fields[v.as.i]; return call_value(vm, in->fields[v.as.i], argc); }
        cls = in->cls;
    } else if (IS_KIND(recv, MCS_O_CLASS)) {
        for (mcs_class_t* c = AS_CLASS(recv); c; c = c->super)
            if (mcs_cls_get(vm, c, MCS_TAB_STATICS, name, &v)) {
                if (IS_KIND(v, MCS_O_CLOSURE) || IS_KIND(v, MCS_O_NATIVE) || IS_KIND(v, MCS_O_OVERLOADS)) return call_method(vm, v, argc);
                vm->sp[-argc - 1] = v;
                return call_value(vm, v, argc);
            }
        cls = AS_CLASS(recv);
        if (mcs_cls_get(vm, cls, MCS_TAB_GETTERS, name, &v)) {
            mcs_value_t d;
            if (mcs_call_internal(vm, v, recv, 0, NULL, &d) != MCS_OK) return CALL_ERR;
            vm->sp[-argc - 1] = d;
            return call_value(vm, d, argc);
        }
        missing_member(vm, recv, name);
        return CALL_ERR;
    } else {
        cls = mcs_class_of(vm, recv);
        if (!cls) goto nullable;
    }
    if (mcs_cls_get(vm, cls, MCS_TAB_METHODS, name, &v)) return call_method(vm, v, argc);
    if (mcs_cls_get(vm, cls, MCS_TAB_GETTERS, name, &v)) {
        mcs_value_t d;
        if (mcs_call_internal(vm, v, recv, 0, NULL, &d) != MCS_OK) return CALL_ERR;
        vm->sp[-argc - 1] = d;
        return call_value(vm, d, argc);
    }
nullable:
    if (name->len == 17 && memcmp(name->chars, "GetValueOrDefault", 17) == 0 && argc <= 1) {
        mcs_value_t r = recv.type != MCS_T_NULL ? recv : (argc ? PEEK(0) : mcs_int(0));
        vm->sp -= argc; vm->sp[-1] = r;
        return CALL_DONE;
    }
    missing_member(vm, recv, name);
    return CALL_ERR;
}

bool mcs_lookup_member(mcs_vm_t* vm, mcs_value_t recv, mcs_string_t* name, mcs_value_t* out) {
    mcs_class_t* cls = IS_KIND(recv, MCS_O_CLASS) ? AS_CLASS(recv) : mcs_class_of(vm, recv);
    if (!cls) return false;
    return mcs_cls_get(vm, cls, MCS_TAB_METHODS, name, out);
}

/* ================================================================ operators */
static bool overload_op(mcs_vm_t* vm, const char* opname, mcs_value_t a, mcs_value_t b, mcs_value_t* out, bool* found) {
    *found = false;
    mcs_value_t args[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        if (!IS_KIND(args[k], MCS_O_INSTANCE)) continue;
        mcs_class_t* cls = AS_INSTANCE(args[k])->cls;
        mcs_string_t* nm = mcs_find_interned(vm, opname, strlen(opname));
        if (!nm) return true;
        for (mcs_class_t* c = cls; c; c = c->super) {
            mcs_value_t f;
            if (mcs_cls_get(vm, c, MCS_TAB_STATICS, nm, &f)) {
                *found = true;
                return mcs_call_internal(vm, f, OBJ_VAL(c), 2, args, out) == MCS_OK;
            }
        }
    }
    return true;
}

static const char* op_method(int op) {
    switch (op) {
    case OP_ADD: return "op_Addition"; case OP_SUB: return "op_Subtraction"; case OP_MUL: return "op_Multiply";
    case OP_DIV: return "op_Division"; case OP_MOD: return "op_Modulus"; case OP_LT: return "op_LessThan";
    case OP_LE: return "op_LessThanOrEqual"; case OP_GT: return "op_GreaterThan"; case OP_GE: return "op_GreaterThanOrEqual";
    case OP_EQ: return "op_Equality"; case OP_NE: return "op_Inequality"; case OP_BAND: return "op_BitwiseAnd";
    case OP_BOR: return "op_BitwiseOr"; case OP_BXOR: return "op_ExclusiveOr";
    default: return "";
    }
}

int mcs_compare_values(mcs_vm_t* vm, mcs_value_t a, mcs_value_t b, bool* ok) {
    *ok = true;
    if (is_intlike_v(a) && is_intlike_v(b)) return a.as.i < b.as.i ? -1 : a.as.i > b.as.i;
#if MCS_ENABLE_FLOAT
    if ((a.type == MCS_T_FLOAT || is_intlike_v(a)) && (b.type == MCS_T_FLOAT || is_intlike_v(b))) {
        mcs_float_t x = a.type == MCS_T_FLOAT ? a.as.f : (mcs_float_t)a.as.i;
        mcs_float_t y = b.type == MCS_T_FLOAT ? b.as.f : (mcs_float_t)b.as.i;
        return x < y ? -1 : x > y;
    }
#endif
    if (IS_STRING(a) && IS_STRING(b)) { int r = strcmp(AS_CSTR(a), AS_CSTR(b)); return r < 0 ? -1 : r > 0; }
    if (a.type == MCS_T_BOOL && b.type == MCS_T_BOOL) return (int)a.as.b - (int)b.as.b;
    if (a.type == MCS_T_NULL && b.type == MCS_T_NULL) return 0;
    if (a.type == MCS_T_NULL) return -1;
    if (b.type == MCS_T_NULL) return 1;
    if (IS_KIND(a, MCS_O_INSTANCE)) {
        mcs_value_t m, r;
        mcs_string_t* nm = mcs_intern_c(vm, "CompareTo");
        if (mcs_table_get_s(&AS_INSTANCE(a)->cls->methods, nm, &m)) {
            if (mcs_call_internal(vm, m, a, 1, &b, &r) != MCS_OK) { *ok = false; return 0; }
            return (int)(r.type == MCS_T_INT ? r.as.i : 0);
        }
    }
    *ok = false;
    mcs_throw(vm, EXC_INVOP, "Cannot compare values of type '%s' and '%s'", mcs_type_name(vm, a), mcs_type_name(vm, b));
    return 0;
}

static bool concat2(mcs_vm_t* vm, mcs_value_t a, mcs_value_t b, mcs_value_t* out) {
    mcs_buf_t buf; mcs_buf_init(&buf, vm);
    if (!mcs_value_to_buf(vm, &buf, a) || !mcs_value_to_buf(vm, &buf, b)) { mcs_buf_free(&buf); return false; }
    *out = OBJ_VAL(mcs_buf_to_string(&buf));
    return true;
}

/* generic binary operation; returns false on exception */
/* ---- multicast delegates: bound(receiver = array of targets, method = mc_invoke) */
static bool is_callable_v(mcs_value_t v) {
    return IS_OBJ(v) && (OBJ_KIND(v) == MCS_O_CLOSURE || OBJ_KIND(v) == MCS_O_NATIVE || OBJ_KIND(v) == MCS_O_BOUND || OBJ_KIND(v) == MCS_O_OVERLOADS);
}
static mcs_value_t mc_invoke(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    mcs_list_t* t = AS_LIST(self);
    mcs_value_t r = mcs_null();
    for (uint32_t i = 0; i < t->count; i++)
        if (mcs_call_internal(vm, t->items[i], mcs_null(), argc, argv, &r) != MCS_OK) return mcs_null();
    return r;
}
static bool is_multicast(mcs_value_t v) {
    return IS_KIND(v, MCS_O_BOUND) && IS_KIND(((mcs_bound_t*)AS_OBJ(v))->method, MCS_O_NATIVE) &&
           AS_NATIVE(((mcs_bound_t*)AS_OBJ(v))->method)->fn == mc_invoke;
}
static bool same_target(mcs_value_t x, mcs_value_t y) {
    if (IS_KIND(x, MCS_O_BOUND) && IS_KIND(y, MCS_O_BOUND)) {
        mcs_bound_t *p = (mcs_bound_t*)AS_OBJ(x), *q = (mcs_bound_t*)AS_OBJ(y);
        return mcs_values_equal(p->receiver, q->receiver) && mcs_values_equal(p->method, q->method);
    }
    return mcs_values_equal(x, y);
}
static uint32_t mc_count(mcs_value_t v) { return is_multicast(v) ? AS_LIST(((mcs_bound_t*)AS_OBJ(v))->receiver)->count : 1; }
static mcs_value_t mc_at(mcs_value_t v, uint32_t i) { return is_multicast(v) ? AS_LIST(((mcs_bound_t*)AS_OBJ(v))->receiver)->items[i] : v; }
static mcs_value_t delegate_combine(mcs_vm_t* vm, mcs_value_t a, mcs_value_t b, bool remove) {
    uint32_t na = mc_count(a), nb = mc_count(b), n = 0;
    vm->gc_pause++;
    mcs_list_t* arr = mcs_new_listobj(vm, MCS_O_ARRAY, remove ? na : na + nb);
    if (!remove) {
        for (uint32_t i = 0; i < na; i++) arr->items[n++] = mc_at(a, i);
        for (uint32_t i = 0; i < nb; i++) arr->items[n++] = mc_at(b, i);
    } else {
        /* remove the last occurrence of b's invocation list (single target case covers events) */
        int64_t hit = -1;
        for (uint32_t i = na; i-- > 0;) if (same_target(mc_at(a, i), mc_at(b, 0))) { hit = i; break; }
        for (uint32_t i = 0; i < na; i++) if ((int64_t)i != hit) arr->items[n++] = mc_at(a, i);
    }
    arr->count = n;
    mcs_value_t res;
    if (n == 0) res = mcs_null();
    else if (n == 1) res = arr->items[0];
    else res = OBJ_VAL(mcs_new_bound(vm, OBJ_VAL(arr), OBJ_VAL(mcs_new_native(vm, mc_invoke, -1, mcs_intern_c(vm, "Invoke")))));
    vm->gc_pause--;
    return res;
}

static bool arith(mcs_vm_t* vm, int op, mcs_value_t a, mcs_value_t b, mcs_value_t* out) {
    if (is_intlike_v(a) && is_intlike_v(b)) {
        mcs_uint_t x = (mcs_uint_t)a.as.i, y = (mcs_uint_t)b.as.i;
        const int bits = (int)sizeof(mcs_int_t) * 8;
        switch (op) {
        case OP_ADD: *out = mcs_int((mcs_int_t)(x + y)); return true;
        case OP_SUB: *out = mcs_int((mcs_int_t)(x - y)); return true;
        case OP_MUL: *out = mcs_int((mcs_int_t)(x * y)); return true;
        case OP_DIV: case OP_MOD:
            if (b.as.i == 0) { mcs_throw(vm, EXC_DIVZERO, "Attempted to divide by zero."); return false; }
            if (b.as.i == -1) { *out = mcs_int(op == OP_DIV ? (mcs_int_t)(0 - x) : 0); return true; }
            *out = mcs_int(op == OP_DIV ? a.as.i / b.as.i : a.as.i % b.as.i); return true;
        case OP_BAND: *out = mcs_int((mcs_int_t)(x & y)); return true;
        case OP_BOR: *out = mcs_int((mcs_int_t)(x | y)); return true;
        case OP_BXOR: *out = mcs_int((mcs_int_t)(x ^ y)); return true;
        case OP_SHL: *out = mcs_int((mcs_int_t)(x << (y & (mcs_uint_t)(bits - 1)))); return true;
        case OP_SHR: *out = mcs_int(a.as.i >> (y & (mcs_uint_t)(bits - 1))); return true;
        case OP_USHR: *out = mcs_int((mcs_int_t)(x >> (y & (mcs_uint_t)(bits - 1)))); return true;
        default: break;
        }
    }
#if MCS_ENABLE_FLOAT
    if ((a.type == MCS_T_FLOAT || is_intlike_v(a)) && (b.type == MCS_T_FLOAT || is_intlike_v(b)) && op <= OP_MOD) {
        mcs_float_t x = a.type == MCS_T_FLOAT ? a.as.f : (mcs_float_t)a.as.i;
        mcs_float_t y = b.type == MCS_T_FLOAT ? b.as.f : (mcs_float_t)b.as.i;
        switch (op) {
        case OP_ADD: *out = mcs_float(x + y); return true;
        case OP_SUB: *out = mcs_float(x - y); return true;
        case OP_MUL: *out = mcs_float(x * y); return true;
        case OP_DIV: *out = mcs_float(x / y); return true;
        case OP_MOD: *out = mcs_float((mcs_float_t)fmod((double)x, (double)y)); return true;
        default: break;
        }
    }
#endif
    if (op == OP_ADD && (IS_STRING(a) || IS_STRING(b))) return concat2(vm, a, b, out);
    if (a.type == MCS_T_BOOL && b.type == MCS_T_BOOL) {
        if (op == OP_BAND) { *out = mcs_bool(a.as.b && b.as.b); return true; }
        if (op == OP_BOR) { *out = mcs_bool(a.as.b || b.as.b); return true; }
        if (op == OP_BXOR) { *out = mcs_bool(a.as.b != b.as.b); return true; }
    }
    bool found;
    if (!overload_op(vm, op_method(op), a, b, out, &found)) return false;
    if (found) return true;
    if ((op == OP_ADD || op == OP_SUB) && is_callable_v(a) && is_callable_v(b)) {
        *out = delegate_combine(vm, a, b, op == OP_SUB); return true;
    }
    if (op == OP_ADD && a.type == MCS_T_NULL && is_callable_v(b)) { *out = b; return true; }
    if (a.type == MCS_T_NULL || b.type == MCS_T_NULL) { *out = mcs_null(); return true; } /* lifted nullable */
    mcs_throw(vm, EXC_INVOP, "Operator '%s' cannot be applied to operands of type '%s' and '%s'",
              op == OP_ADD ? "+" : op == OP_SUB ? "-" : op == OP_MUL ? "*" : op == OP_DIV ? "/" : op == OP_MOD ? "%" : "op",
              mcs_type_name(vm, a), mcs_type_name(vm, b));
    return false;
}

static bool compare_op(mcs_vm_t* vm, int op, mcs_value_t a, mcs_value_t b, mcs_value_t* out) {
    if (IS_KIND(a, MCS_O_INSTANCE) || IS_KIND(b, MCS_O_INSTANCE)) {
        bool found;
        if (!overload_op(vm, op_method(op), a, b, out, &found)) return false;
        if (found) return true;
    }
    if (op == OP_EQ || op == OP_NE) {
        bool eq = mcs_values_equal(a, b);
        *out = mcs_bool(op == OP_EQ ? eq : !eq);
        return true;
    }
    if (a.type == MCS_T_NULL || b.type == MCS_T_NULL) { *out = mcs_bool(false); return true; }
    bool ok;
    int c = mcs_compare_values(vm, a, b, &ok);
    if (!ok) return false;
#if MCS_ENABLE_FLOAT
    if ((a.type == MCS_T_FLOAT && a.as.f != a.as.f) || (b.type == MCS_T_FLOAT && b.as.f != b.as.f)) { *out = mcs_bool(false); return true; }
#endif
    switch (op) {
    case OP_LT: *out = mcs_bool(c < 0); break;
    case OP_LE: *out = mcs_bool(c <= 0); break;
    case OP_GT: *out = mcs_bool(c > 0); break;
    default: *out = mcs_bool(c >= 0); break;
    }
    return true;
}

static bool convert(mcs_vm_t* vm, int kind, mcs_value_t v, mcs_value_t* out) {
    if (v.type == MCS_T_NULL) { *out = v; return true; }
    mcs_int_t i;
    if (v.type == MCS_T_INT || v.type == MCS_T_CHAR) i = v.as.i;
#if MCS_ENABLE_FLOAT
    else if (v.type == MCS_T_FLOAT) {
        if (kind == CV_FLOAT) { *out = v; return true; }
        mcs_float_t f = v.as.f;
        i = f != f ? 0 : (mcs_int_t)f;
    }
#endif
    else if (v.type == MCS_T_BOOL && kind == CV_BOOL) { *out = v; return true; }
    else {
        mcs_throw(vm, EXC_INVCAST, "Cannot convert type '%s' to a number", mcs_type_name(vm, v));
        return false;
    }
    switch (kind) {
    case CV_INT: case CV_UINT: *out = mcs_int(i); return true;
#if MCS_ENABLE_FLOAT
    case CV_FLOAT: *out = mcs_float((mcs_float_t)i); return true;
#else
    case CV_FLOAT: *out = mcs_int(i); return true;
#endif
    case CV_CHAR: *out = mcs_char((uint32_t)i & 0x1FFFFF); return true;
    case CV_BYTE: *out = mcs_int((uint8_t)i); return true;
    case CV_SBYTE: *out = mcs_int((int8_t)i); return true;
    case CV_SHORT: *out = mcs_int((int16_t)i); return true;
    case CV_USHORT: *out = mcs_int((uint16_t)i); return true;
    case CV_BOOL: *out = mcs_bool(i != 0); return true;
    default: *out = v; return true;
    }
}

/* ================================================================ indexing */
static bool check_index(mcs_vm_t* vm, mcs_value_t obj, mcs_value_t idx, uint32_t count, uint32_t* out) {
    if (!is_intlike_v(idx)) { mcs_throw(vm, EXC_ARGUMENT, "Index must be an integer, got '%s'", mcs_type_name(vm, idx)); return false; }
    if (idx.as.i < 0 || (mcs_uint_t)idx.as.i >= count) {
        if (IS_KIND(obj, MCS_O_LIST)) mcs_throw(vm, EXC_ARGRANGE, "Index was out of range. Must be non-negative and less than the size of the collection. (index %ld, count %lu)", (long)idx.as.i, (unsigned long)count);
        else mcs_throw(vm, EXC_INDEX, "Index was outside the bounds of the array. (index %ld, length %lu)", (long)idx.as.i, (unsigned long)count);
        return false;
    }
    *out = (uint32_t)idx.as.i;
    return true;
}

static void key_not_found(mcs_vm_t* vm, mcs_value_t key) {
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (!IS_KIND(key, MCS_O_INSTANCE)) mcs_value_to_buf(vm, &b, key);
    mcs_throw(vm, EXC_KEYNOTFOUND, "The given key '%s' was not present in the dictionary.", b.data ? b.data : "");
    mcs_buf_free(&b);
}

/* [obj idx] -> [value] */
static int get_index_op(mcs_vm_t* vm) {
    mcs_value_t obj = PEEK(1), idx = PEEK(0);
    uint32_t i;
    if (IS_OBJ(obj)) {
        switch (OBJ_KIND(obj)) {
        case MCS_O_ARRAY: case MCS_O_LIST:
            if (!check_index(vm, obj, idx, AS_LIST(obj)->count, &i)) return CALL_ERR;
            vm->sp--; vm->sp[-1] = AS_LIST(obj)->items[i]; return CALL_DONE;
        case MCS_O_STRING:
            if (!check_index(vm, obj, idx, AS_STRING(obj)->len, &i)) return CALL_ERR;
            vm->sp--; vm->sp[-1] = mcs_char((uint8_t)AS_CSTR(obj)[i]); return CALL_DONE;
        case MCS_O_DICT: {
            mcs_value_t v;
            if (!mcs_dict_get(AS_DICT(obj), idx, &v)) { key_not_found(vm, idx); return CALL_ERR; }
            vm->sp--; vm->sp[-1] = v; return CALL_DONE;
        }
        case MCS_O_INSTANCE: case MCS_O_USERDATA: {
            mcs_value_t m;
            mcs_string_t* nm = mcs_intern_c(vm, "get_Item");
            if (mcs_table_get_s(&mcs_class_of(vm, obj)->methods, nm, &m)) return call_method(vm, m, 1);
            break;
        }
        default: break;
        }
    }
    if (obj.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
    else mcs_throw(vm, EXC_INVOP, "Cannot apply indexing to a value of type '%s'", mcs_type_name(vm, obj));
    return CALL_ERR;
}

/* [obj idx val] -> [val] */
static int set_index_op(mcs_vm_t* vm) {
    mcs_value_t obj = PEEK(2), idx = PEEK(1), val = PEEK(0);
    uint32_t i;
    if (IS_OBJ(obj)) {
        switch (OBJ_KIND(obj)) {
        case MCS_O_ARRAY: case MCS_O_LIST:
            if (!check_index(vm, obj, idx, AS_LIST(obj)->count, &i)) return CALL_ERR;
            AS_LIST(obj)->items[i] = val;
            vm->sp -= 2; vm->sp[-1] = val; return CALL_DONE;
        case MCS_O_DICT:
            if (idx.type == MCS_T_NULL) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'key')"); return CALL_ERR; }
            mcs_dict_set(vm, AS_DICT(obj), idx, val);
            vm->sp -= 2; vm->sp[-1] = val; return CALL_DONE;
        case MCS_O_INSTANCE: case MCS_O_USERDATA: {
            mcs_value_t m;
            mcs_string_t* nm = mcs_intern_c(vm, "set_Item");
            if (mcs_table_get_s(&mcs_class_of(vm, obj)->methods, nm, &m)) return call_method(vm, m, 2);
            break;
        }
        case MCS_O_STRING: mcs_throw(vm, EXC_INVOP, "Strings are immutable; indexer is read only"); return CALL_ERR;
        default: break;
        }
    }
    if (obj.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
    else mcs_throw(vm, EXC_INVOP, "Cannot apply indexing to a value of type '%s'", mcs_type_name(vm, obj));
    return CALL_ERR;
}

/* ================================================================ run loop */
static bool unwind(mcs_vm_t* vm, int base_frame) {
    while (vm->handler_count > 0) {
        mcs_handler_t* h = &vm->handlers[vm->handler_count - 1];
        if (h->frame < base_frame) return false;
        vm->handler_count--;
        mcs_value_t* nsp = vm->stack + h->sp;
        close_upvalues(vm, nsp);
        vm->frame_count = h->frame + 1;
        vm->sp = nsp;
        PUSH(vm->exc_value);
        vm->has_exc = false;
        vm->exc_value = mcs_null();
        vm->frames[h->frame].ip = h->ip;
        return true;
    }
    return false;
}

static uint32_t hook_budget(mcs_vm_t* vm) {
    uint32_t n = MCS_HOOK_INTERVAL;
    if (vm->limits.steps) {
        uint32_t left = vm->steps_used < vm->limits.steps ? vm->limits.steps - vm->steps_used : 1;
        if (left < n) n = left;
    }
    return n;
}

/* Called every hook_reload safepoints. Non-zero = abort the run. */
static int hook_tick(mcs_vm_t* vm) {
    vm->steps_used += vm->hook_reload;
    if (vm->abort_req) { vm->abort_reason = MCS_ABORT_REQUEST; return 1; }
    if (vm->limits.steps && vm->steps_used >= vm->limits.steps) { vm->abort_reason = MCS_ABORT_STEPS; return 1; }
    if (vm->limits.time_ms && vm->cfg.ticks_fn &&
        (uint32_t)(vm->cfg.ticks_fn(vm->cfg.user_data) - vm->run_start) >= vm->limits.time_ms) {
        vm->abort_reason = MCS_ABORT_TIME; return 1;
    }
    if (vm->cfg.hook_fn && vm->cfg.hook_fn(vm, vm->cfg.user_data)) { vm->abort_reason = MCS_ABORT_HOOK; return 1; }
    vm->hook_counter = vm->hook_reload = hook_budget(vm);
    return 0;
}

#if MCS_FIELD_CACHE
static void fcache_put(mcs_vm_t* vm, mcs_function_t* fn, uint32_t k, mcs_class_t* cls, uint32_t slot) {
    if (!fn->fcache) {
        if (vm->cfg.heap_limit && vm->bytes_allocated + fn->const_count * sizeof(mcs_fcache_t) > vm->cfg.heap_limit) return; /* cache is optional */
        fn->fcache = MCS_ALLOC(vm, mcs_fcache_t, fn->const_count);
        fn->fcache_n = fn->const_count;
        for (uint32_t i = 0; i < fn->fcache_n; i++) { fn->fcache[i].cls = NULL; fn->fcache[i].slot = 0; }
    }
    if (k < fn->fcache_n) { fn->fcache[k].cls = cls; fn->fcache[k].slot = slot; }
}
#endif

static mcs_result_t run(mcs_vm_t* vm, int base_frame) {
    mcs_frame_t* frame;
    uint8_t* ip;
    mcs_value_t* slots;
    mcs_value_t* consts;
#if MCS_ENABLE_XIP
    const uint16_t* gmap;
#define GSLOT(g) (gmap ? gmap[g] : (g))
#define LOAD_GMAP(f) (gmap = (f)->gmap)
#else
#define GSLOT(g) (g)
#define LOAD_GMAP(f) ((void)0)
#endif
    mcs_value_t a, b, r;
    if (vm->run_depth == 0) {
        vm->steps_used = 0;
        if (vm->limits.time_ms && vm->cfg.ticks_fn) vm->run_start = vm->cfg.ticks_fn(vm->cfg.user_data);
        if (!vm->abort_req) vm->hook_counter = vm->hook_reload = hook_budget(vm);
    }
    vm->run_depth++;

#define LOAD() do { frame = &vm->frames[vm->frame_count - 1]; ip = frame->ip; slots = frame->slots; consts = frame->closure->fn->consts; LOAD_GMAP(frame->closure->fn); } while (0)
#define SAVE() (frame->ip = ip)
#define READ8() (*ip++)
#define READ16() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define KSTR(i) AS_STRING(consts[i])
#define THROWN() goto on_exception
#define CHECKCALL(res) do { int _r = (res); if (_r == CALL_ERR) THROWN(); if (_r == CALL_FRAME) LOAD(); } while (0)
#define GCPOINT() do { if (vm->gc_wanted) { SAVE(); mcs_collect(vm); } } while (0)
#define SAFEPOINT() do { \
        if (vm->gc_wanted) mcs_collect(vm); \
        if (--vm->hook_counter == 0) { SAVE(); if (hook_tick(vm)) goto on_abort; } \
    } while (0)

#if MCS_COMPUTED_GOTO
    static void* labels[OP__COUNT] = {
#define X(name, len) &&L_##name,
        MCS_OPCODES(X)
#undef X
    };
#define CASE(name) L_##name:
#define DISPATCH() goto *labels[*ip++]
#define DISPATCH_START() DISPATCH();
#define DISPATCH_END()
#else
#define CASE(name) case OP_##name:
#define DISPATCH() goto dispatch_top
#define DISPATCH_START() dispatch_top: switch (*ip++) {
#define DISPATCH_END() default: mcs_throw(vm, EXC_SYSTEM, "invalid opcode"); THROWN(); }
#endif

    LOAD();
    DISPATCH_START()
    CASE(CONST) { uint16_t k = READ16(); PUSH(consts[k]); DISPATCH(); }
    CASE(NULL) PUSH(mcs_null()); DISPATCH();
    CASE(TRUE) PUSH(mcs_bool(true)); DISPATCH();
    CASE(FALSE) PUSH(mcs_bool(false)); DISPATCH();
    CASE(INT8) { int8_t v = (int8_t)READ8(); PUSH(mcs_int(v)); DISPATCH(); }
    CASE(POP) vm->sp--; DISPATCH();
    CASE(DUP) { a = PEEK(0); PUSH(a); DISPATCH(); }
    CASE(DUP2) { a = PEEK(1); b = PEEK(0); PUSH(a); PUSH(b); DISPATCH(); }
    CASE(SWAP) { a = PEEK(0); vm->sp[-1] = vm->sp[-2]; vm->sp[-2] = a; DISPATCH(); }
    CASE(ROT) {
        int n = READ8();
        a = PEEK(0);
        memmove(vm->sp - n, vm->sp - n - 1, sizeof(mcs_value_t) * (size_t)n);
        vm->sp[-n - 1] = a;
        DISPATCH();
    }
    CASE(GET_LOCAL) PUSH(slots[READ8()]); DISPATCH();
    CASE(SET_LOCAL) slots[READ8()] = PEEK(0); DISPATCH();
    CASE(GET_UPVAL) PUSH(*frame->closure->upvalues[READ8()]->location); DISPATCH();
    CASE(SET_UPVAL) *frame->closure->upvalues[READ8()]->location = PEEK(0); DISPATCH();
    CASE(GET_GLOBAL) {
        uint16_t s = GSLOT(READ16());
        a = vm->globals[s];
        if (a.type == MCS_T_UNDEF) { SAVE(); mcs_throw(vm, EXC_MISSINGMEMBER, "The name '%s' does not exist in the current context", vm->global_names[s]->chars); THROWN(); }
        PUSH(a);
        DISPATCH();
    }
    CASE(SET_GLOBAL) { uint16_t g = READ16(); vm->globals[GSLOT(g)] = PEEK(0); DISPATCH(); }
    CASE(SET_LOCAL_POP) { uint8_t s = READ8(); slots[s] = POP(); DISPATCH(); }
    CASE(SET_GLOBAL_POP) { uint16_t g = READ16(); vm->globals[GSLOT(g)] = POP(); DISPATCH(); }
    CASE(GET_FIELD) {
        uint16_t k = READ16();
        mcs_string_t* nm = KSTR(k);
        a = PEEK(0);
        if (IS_KIND(a, MCS_O_INSTANCE)) {
            mcs_instance_t* in = AS_INSTANCE(a);
#if MCS_FIELD_CACHE
            mcs_function_t* cf = frame->closure->fn;
            if (k < cf->fcache_n && cf->fcache[k].cls == in->cls) { vm->sp[-1] = in->fields[cf->fcache[k].slot]; DISPATCH(); }
#endif
            mcs_value_t slot;
            if (mcs_table_get_s(&in->cls->fields, nm, &slot)) {
#if MCS_FIELD_CACHE
                fcache_put(vm, cf, k, in->cls, (uint32_t)slot.as.i);
#endif
                vm->sp[-1] = in->fields[slot.as.i]; DISPATCH();
            }
        }
        SAVE();
        CHECKCALL(get_member_op(vm, nm));
        DISPATCH();
    }
    CASE(SET_FIELD) {
        uint16_t k = READ16();
        mcs_string_t* nm = KSTR(k);
        a = PEEK(1);
        if (IS_KIND(a, MCS_O_INSTANCE)) {
            mcs_instance_t* in = AS_INSTANCE(a);
#if MCS_FIELD_CACHE
            mcs_function_t* cf = frame->closure->fn;
            if (k < cf->fcache_n && cf->fcache[k].cls == in->cls) { b = POP(); in->fields[cf->fcache[k].slot] = b; vm->sp[-1] = b; DISPATCH(); }
#endif
            mcs_value_t slot;
            if (mcs_table_get_s(&in->cls->fields, nm, &slot)) {
#if MCS_FIELD_CACHE
                fcache_put(vm, cf, k, in->cls, (uint32_t)slot.as.i);
#endif
                b = POP(); in->fields[slot.as.i] = b; vm->sp[-1] = b; DISPATCH();
            }
        }
        SAVE();
        CHECKCALL(set_member_op(vm, nm));
        DISPATCH();
    }
    CASE(GET_INDEX) {
        a = PEEK(1); b = PEEK(0);
        if ((IS_KIND(a, MCS_O_ARRAY) || IS_KIND(a, MCS_O_LIST)) && b.type == MCS_T_INT && (mcs_uint_t)b.as.i < AS_LIST(a)->count) {
            vm->sp--; vm->sp[-1] = AS_LIST(a)->items[b.as.i]; DISPATCH();
        }
        SAVE();
        CHECKCALL(get_index_op(vm));
        DISPATCH();
    }
    CASE(SET_INDEX) {
        a = PEEK(2); b = PEEK(1);
        if ((IS_KIND(a, MCS_O_ARRAY) || IS_KIND(a, MCS_O_LIST)) && b.type == MCS_T_INT && (mcs_uint_t)b.as.i < AS_LIST(a)->count) {
            r = PEEK(0); AS_LIST(a)->items[b.as.i] = r; vm->sp -= 2; vm->sp[-1] = r; DISPATCH();
        }
        SAVE();
        CHECKCALL(set_index_op(vm));
        DISPATCH();
    }
#define BINOP(NAME, OPC, EXPR) \
    CASE(NAME) { \
        a = PEEK(1); b = PEEK(0); \
        if (a.type == MCS_T_INT && b.type == MCS_T_INT) { vm->sp--; vm->sp[-1] = mcs_int((mcs_int_t)(EXPR)); DISPATCH(); } \
        SAVE(); \
        if (!arith(vm, OPC, a, b, &r)) THROWN(); \
        vm->sp--; vm->sp[-1] = r; DISPATCH(); \
    }
    BINOP(ADD, OP_ADD, (mcs_uint_t)a.as.i + (mcs_uint_t)b.as.i)
    BINOP(SUB, OP_SUB, (mcs_uint_t)a.as.i - (mcs_uint_t)b.as.i)
    BINOP(MUL, OP_MUL, (mcs_uint_t)a.as.i * (mcs_uint_t)b.as.i)
    BINOP(BAND, OP_BAND, a.as.i & b.as.i)
    BINOP(BOR, OP_BOR, a.as.i | b.as.i)
    BINOP(BXOR, OP_BXOR, a.as.i ^ b.as.i)
#undef BINOP
    /* int fast path for / and % (divisor 0 and -1 go to the checked slow path) */
#define DIVOP(NAME, OPC, OPR) \
    CASE(NAME) { \
        a = PEEK(1); b = PEEK(0); \
        if (a.type == MCS_T_INT && b.type == MCS_T_INT && b.as.i > 0) { vm->sp--; vm->sp[-1] = mcs_int(a.as.i OPR b.as.i); DISPATCH(); } \
        SAVE(); if (!arith(vm, OPC, a, b, &r)) THROWN(); vm->sp--; vm->sp[-1] = r; DISPATCH(); \
    }
    DIVOP(DIV, OP_DIV, /)
    DIVOP(MOD, OP_MOD, %)
#undef DIVOP
#define SLOWOP(NAME, OPC) \
    CASE(NAME) { a = PEEK(1); b = PEEK(0); SAVE(); if (!arith(vm, OPC, a, b, &r)) THROWN(); vm->sp--; vm->sp[-1] = r; DISPATCH(); }
    SLOWOP(SHL, OP_SHL)
    SLOWOP(SHR, OP_SHR)
    SLOWOP(USHR, OP_USHR)
#undef SLOWOP
    CASE(NEG) {
        a = PEEK(0);
        if (a.type == MCS_T_INT || a.type == MCS_T_CHAR) { vm->sp[-1] = mcs_int((mcs_int_t)(0 - (mcs_uint_t)a.as.i)); DISPATCH(); }
#if MCS_ENABLE_FLOAT
        if (a.type == MCS_T_FLOAT) { vm->sp[-1] = mcs_float(-a.as.f); DISPATCH(); }
#endif
        if (IS_KIND(a, MCS_O_INSTANCE)) {
            mcs_value_t f; mcs_string_t* nm = mcs_intern_c(vm, "op_UnaryNegation");
            if (mcs_table_get_s(&AS_INSTANCE(a)->cls->statics, nm, &f)) {
                SAVE(); if (mcs_call_internal(vm, f, mcs_null(), 1, &a, &r) != MCS_OK) THROWN();
                vm->sp[-1] = r; DISPATCH();
            }
        }
        SAVE(); mcs_throw(vm, EXC_INVOP, "Operator '-' cannot be applied to operand of type '%s'", mcs_type_name(vm, a)); THROWN();
    }
    CASE(BNOT) {
        a = PEEK(0);
        if (a.type == MCS_T_INT || a.type == MCS_T_CHAR) { vm->sp[-1] = mcs_int(~a.as.i); DISPATCH(); }
        SAVE(); mcs_throw(vm, EXC_INVOP, "Operator '~' cannot be applied to operand of type '%s'", mcs_type_name(vm, a)); THROWN();
    }
    CASE(NOT) vm->sp[-1] = mcs_bool(!mcs_truthy(PEEK(0))); DISPATCH();
#define CMPOP(NAME, OPC, CMP) \
    CASE(NAME) { \
        a = PEEK(1); b = PEEK(0); \
        if (a.type == MCS_T_INT && b.type == MCS_T_INT) { vm->sp--; vm->sp[-1] = mcs_bool(a.as.i CMP b.as.i); DISPATCH(); } \
        SAVE(); \
        if (!compare_op(vm, OPC, a, b, &r)) THROWN(); \
        vm->sp--; vm->sp[-1] = r; DISPATCH(); \
    }
    CMPOP(EQ, OP_EQ, ==)
    CMPOP(NE, OP_NE, !=)
    CMPOP(LT, OP_LT, <)
    CMPOP(LE, OP_LE, <=)
    CMPOP(GT, OP_GT, >)
    CMPOP(GE, OP_GE, >=)
#undef CMPOP
    /* fused compare-and-branch: jump when !(a CMP b); operands stay rooted on the
     * stack while a user-defined operator may run */
#define JCMP(NAME, OPC, CMP) \
    CASE(NAME) { \
        uint16_t o = READ16(); \
        a = PEEK(1); b = PEEK(0); \
        if (a.type == MCS_T_INT && b.type == MCS_T_INT) { vm->sp -= 2; if (!(a.as.i CMP b.as.i)) ip += o; DISPATCH(); } \
        SAVE(); \
        if (!compare_op(vm, OPC, a, b, &r)) THROWN(); \
        vm->sp -= 2; if (!mcs_truthy(r)) ip += o; DISPATCH(); \
    }
    JCMP(JF_EQ, OP_EQ, ==)
    JCMP(JF_NE, OP_NE, !=)
    JCMP(JF_LT, OP_LT, <)
    JCMP(JF_LE, OP_LE, <=)
    JCMP(JF_GT, OP_GT, >)
    JCMP(JF_GE, OP_GE, >=)
#undef JCMP
    CASE(INC_LOCAL) {
        uint8_t s = READ8(); int8_t d = (int8_t)READ8();
        a = slots[s];
        if (a.type == MCS_T_INT) { slots[s].as.i = (mcs_int_t)((mcs_uint_t)a.as.i + (mcs_uint_t)(mcs_int_t)d); DISPATCH(); }
#if MCS_ENABLE_FLOAT
        if (a.type == MCS_T_FLOAT) { slots[s].as.f += (mcs_float_t)d; DISPATCH(); }
#endif
        if (a.type == MCS_T_CHAR) { slots[s].as.i += d; DISPATCH(); }
        SAVE();
        if (!arith(vm, OP_ADD, a, mcs_int(d), &r)) THROWN();
        slots[s] = r;
        DISPATCH();
    }
    CASE(JUMP) { uint16_t o = READ16(); ip += o; DISPATCH(); }
    CASE(JUMP_IF_FALSE) { uint16_t o = READ16(); a = POP(); if (!mcs_truthy(a)) ip += o; DISPATCH(); }
    CASE(JUMP_IF_TRUE) { uint16_t o = READ16(); a = POP(); if (mcs_truthy(a)) ip += o; DISPATCH(); }
    CASE(JUMP_IF_FALSE_KEEP) { uint16_t o = READ16(); if (!mcs_truthy(PEEK(0))) ip += o; DISPATCH(); }
    CASE(JUMP_IF_TRUE_KEEP) { uint16_t o = READ16(); if (mcs_truthy(PEEK(0))) ip += o; DISPATCH(); }
    CASE(JUMP_IF_NULL_KEEP) { uint16_t o = READ16(); if (PEEK(0).type == MCS_T_NULL) ip += o; DISPATCH(); }
    CASE(JUMP_IF_NOT_NULL_KEEP) { uint16_t o = READ16(); if (PEEK(0).type != MCS_T_NULL) ip += o; DISPATCH(); }
    CASE(LOOP) { uint16_t o = READ16(); ip -= o; SAFEPOINT(); DISPATCH(); }
    CASE(ARGC_JUMP) { uint8_t n = READ8(); uint16_t o = READ16(); if (frame->argc >= n) ip += o; DISPATCH(); }
    CASE(CALL) {
        int argc = READ8();
        SAVE();
        SAFEPOINT();
        a = PEEK(argc);
        if (IS_KIND(a, MCS_O_CLOSURE)) {   /* fast path: exact arity, no params array */
            mcs_closure_t* cl = AS_CLOSURE(a);
            mcs_function_t* fn = cl->fn;
            if (argc == fn->arity && !(fn->flags & FN_HAS_PARAMS) && vm->frame_count < vm->cfg.max_frames &&
                vm->sp + fn->max_slots + MCS_STACK_MARGIN < vm->stack_end) {
                frame = &vm->frames[vm->frame_count++];
                frame->closure = cl; frame->ip = ip = fn->code; frame->slots = slots = vm->sp - argc - 1; frame->argc = (uint8_t)argc;
                consts = fn->consts; LOAD_GMAP(fn);
                DISPATCH();
            }
        }
        CHECKCALL(call_value(vm, a, argc));
        DISPATCH();
    }
    CASE(INVOKE) {
        mcs_string_t* nm = KSTR(READ16());
        int argc = READ8();
        SAVE();
        SAFEPOINT();
        a = PEEK(argc);
        if (IS_KIND(a, MCS_O_INSTANCE)) {
            mcs_value_t m;
            mcs_instance_t* in = AS_INSTANCE(a);
            if (!mcs_table_get_s(&in->cls->fields, nm, NULL) && mcs_cls_get(vm, in->cls, MCS_TAB_METHODS, nm, &m) && IS_KIND(m, MCS_O_CLOSURE)) {
                CHECKCALL(call_closure(vm, AS_CLOSURE(m), argc));
                DISPATCH();
            }
        }
        CHECKCALL(invoke_op(vm, nm, argc));
        DISPATCH();
    }
    CASE(SUPER_INVOKE) {
        mcs_string_t* nm = KSTR(READ16());
        int argc = READ8();
        SAVE();
        a = POP();
        mcs_value_t m;
        if (!IS_KIND(a, MCS_O_CLASS)) { mcs_throw(vm, EXC_INVOP, "base class is not defined"); THROWN(); }
        /* mcs_cls_get also materializes lazily registered native members
         * (e.g. Exception's .ctor when a script class calls base(msg)) */
        if (!mcs_cls_get(vm, AS_CLASS(a), MCS_TAB_METHODS, nm, &m)) {
            if ((nm == vm->s_ctor || nm == vm->s_init) && argc == 0) DISPATCH();
            missing_member(vm, PEEK(argc), nm);
            THROWN();
        }
        CHECKCALL(call_method(vm, m, argc));
        DISPATCH();
    }
    CASE(CLOSURE) { GCPOINT();
        mcs_function_t* fn = AS_FUNCTION(consts[READ16()]);
        mcs_closure_t* cl = mcs_new_closure(vm, fn);
        PUSH(OBJ_VAL(cl));
        for (uint32_t i = 0; i < cl->upvalue_count; i++) {
            uint8_t is_local = READ8(), index = READ8();
            cl->upvalues[i] = is_local ? capture_upvalue(vm, slots + index) : frame->closure->upvalues[index];
        }
        DISPATCH();
    }
    CASE(CLOSE_UPVAL) close_upvalues(vm, vm->sp - 1); vm->sp--; DISPATCH();
    CASE(RETURN) {
        r = POP();
    do_return:
        while (vm->handler_count > 0 && vm->handlers[vm->handler_count - 1].frame >= vm->frame_count - 1) vm->handler_count--;
        close_upvalues(vm, slots);
        vm->frame_count--;
        vm->sp = slots;
        PUSH(r);
        if (vm->frame_count <= base_frame) { vm->run_depth--; return MCS_OK; }
        LOAD();
        DISPATCH();
    }
    CASE(RETURN_NULL) { r = mcs_null(); goto do_return; }
    CASE(CLASS) {
        mcs_string_t* nm = KSTR(READ16());
        uint8_t flags = READ8();
        mcs_class_t* cls = mcs_new_class(vm, nm, flags & 2 ? CLS_INTERFACE : flags & 1 ? CLS_STATIC : CLS_SCRIPT);
        PUSH(OBJ_VAL(cls));
        if (!(flags & 2)) mcs_class_inherit(vm, cls, vm->cls_object);
        DISPATCH();
    }
    CASE(INHERIT) {
        a = PEEK(1); b = PEEK(0);
        if (!IS_KIND(b, MCS_O_CLASS) || AS_CLASS(b)->ckind == CLS_STATIC) {
            SAVE(); mcs_throw(vm, EXC_INVOP, "'%s' cannot be used as a base class", b.type == MCS_T_UNDEF ? "undefined" : mcs_type_name(vm, b)); THROWN();
        }
        mcs_class_inherit(vm, AS_CLASS(a), AS_CLASS(b));
        vm->sp--;
        DISPATCH();
    }
    CASE(IMPLEMENTS) { mcs_string_t* nm = KSTR(READ16()); mcs_table_set(vm, &AS_CLASS(PEEK(0))->ifaces, OBJ_VAL(nm), mcs_bool(true)); DISPATCH(); }
    CASE(FIELD) {
        mcs_string_t* nm = KSTR(READ16());
        uint16_t k = READ16();
        mcs_class_add_field(vm, AS_CLASS(PEEK(0)), nm, k == 0xFFFF ? mcs_null() : consts[k]);
        DISPATCH();
    }
    CASE(METHOD) {
        mcs_string_t* nm = KSTR(READ16());
        uint8_t flags = READ8();
        mcs_class_add_method(vm, &AS_CLASS(PEEK(1))->methods, nm, PEEK(0), flags & 1);
        vm->sp--;
        DISPATCH();
    }
    CASE(STATIC) {
        mcs_string_t* nm = KSTR(READ16());
        uint8_t flags = READ8();
        a = PEEK(0);
        if (IS_KIND(a, MCS_O_CLOSURE)) mcs_class_add_method(vm, &AS_CLASS(PEEK(1))->statics, nm, a, flags & 1);
        else mcs_table_set(vm, &AS_CLASS(PEEK(1))->statics, OBJ_VAL(nm), a);
        vm->sp--;
        DISPATCH();
    }
    CASE(GETTER) { mcs_string_t* nm = KSTR(READ16()); ip++; mcs_table_set(vm, &AS_CLASS(PEEK(1))->getters, OBJ_VAL(nm), PEEK(0)); vm->sp--; DISPATCH(); }
    CASE(SETTER) { mcs_string_t* nm = KSTR(READ16()); ip++; mcs_table_set(vm, &AS_CLASS(PEEK(1))->setters, OBJ_VAL(nm), PEEK(0)); vm->sp--; DISPATCH(); }
    CASE(ARRAY) { GCPOINT();
        uint16_t n = READ16();
        mcs_list_t* l = mcs_new_listobj(vm, MCS_O_ARRAY, n);
        for (uint16_t i = 0; i < n; i++) l->items[i] = vm->sp[-(int)n + i];
        vm->sp -= n;
        PUSH(OBJ_VAL(l));
        DISPATCH();
    }
    CASE(NEW_ARRAY) { GCPOINT();
        uint8_t kind = READ8();
        a = PEEK(0);
        if (!is_intlike_v(a) || a.as.i < 0) { SAVE(); mcs_throw(vm, EXC_ARGRANGE, "Array size must be a non-negative integer"); THROWN(); }
        mcs_list_t* l = mcs_new_listobj(vm, MCS_O_ARRAY, (uint32_t)a.as.i);
        mcs_value_t d = mcs_null();
        switch (kind) {
        case CV_INT: d = mcs_int(0); break;
#if MCS_ENABLE_FLOAT
        case CV_FLOAT: d = mcs_float(0); break;
#endif
        case CV_BOOL: d = mcs_bool(false); break;
        case CV_CHAR: d = mcs_char(0); break;
        default: break;
        }
        for (uint32_t i = 0; i < l->count; i++) l->items[i] = d;
        vm->sp[-1] = OBJ_VAL(l);
        DISPATCH();
    }
    CASE(CONV) {
        uint8_t k = READ8();
        a = PEEK(0);
        if (k == CV_INT && a.type == MCS_T_INT) DISPATCH();
        SAVE();
        if (!convert(vm, k, a, &r)) THROWN();
        vm->sp[-1] = r;
        DISPATCH();
    }
    CASE(TOSTR) { GCPOINT();
        a = PEEK(0);
        if (!IS_STRING(a)) { SAVE(); mcs_string_t* s = mcs_value_to_string(vm, a); if (!s) THROWN(); vm->sp[-1] = OBJ_VAL(s); }
        DISPATCH();
    }
    CASE(TOSTR_FMT) {
        a = PEEK(1); b = PEEK(0);
        SAVE();
        mcs_buf_t buf; mcs_buf_init(&buf, vm);
        if (!mcs_format_spec(vm, &buf, a, AS_CSTR(b), AS_STRING(b)->len)) { mcs_buf_free(&buf); THROWN(); }
        vm->sp--;
        vm->sp[-1] = OBJ_VAL(mcs_buf_to_string(&buf));
        DISPATCH();
    }
    CASE(CONCAT) { GCPOINT();
        int n = READ8();
        SAVE();
        size_t total = 0;
        for (int i = n; i >= 1; i--) {
            mcs_value_t* p = vm->sp - i;
            if (!IS_STRING(*p)) {
                if (p->type == MCS_T_NULL) { *p = OBJ_VAL(mcs_intern(vm, "", 0)); }
                else { mcs_string_t* s = mcs_value_to_string(vm, *p); if (!s) THROWN(); *p = OBJ_VAL(s); }
            }
            total += AS_STRING(*p)->len;
        }
        char* buf = (char*)mcs_realloc(vm, NULL, 0, total + 1);
        size_t off = 0;
        for (int i = n; i >= 1; i--) { mcs_string_t* s = AS_STRING(vm->sp[-i]); memcpy(buf + off, s->chars, s->len); off += s->len; }
        mcs_string_t* res = mcs_take_buffer(vm, buf, total, total + 1);
        vm->sp -= n;
        PUSH(OBJ_VAL(res));
        DISPATCH();
    }
    CASE(IS) { mcs_string_t* tn = KSTR(READ16()); vm->sp[-1] = mcs_bool(mcs_is_instance_of(vm, PEEK(0), tn)); DISPATCH(); }
    CASE(AS) { mcs_string_t* tn = KSTR(READ16()); if (!mcs_is_instance_of(vm, PEEK(0), tn)) vm->sp[-1] = mcs_null(); DISPATCH(); }
    CASE(CAST) {
        mcs_string_t* tn = KSTR(READ16());
        a = PEEK(0);
        if (a.type != MCS_T_NULL && !mcs_is_instance_of(vm, a, tn)) {
            SAVE(); mcs_throw(vm, EXC_INVCAST, "Unable to cast object of type '%s' to type '%s'.", mcs_type_name(vm, a), tn->chars); THROWN();
        }
        DISPATCH();
    }
    CASE(FOR_ITER) {
        uint8_t s = READ8();
        uint16_t o = READ16();
        a = slots[s];
        mcs_int_t i = slots[s + 1].as.i;
        if (IS_KIND(a, MCS_O_ARRAY) || IS_KIND(a, MCS_O_LIST)) {
            if ((mcs_uint_t)i < AS_LIST(a)->count) { PUSH(AS_LIST(a)->items[i]); slots[s + 1].as.i = i + 1; }
            else ip += o;
            DISPATCH();
        }
        if (IS_STRING(a)) {
            mcs_string_t* str = AS_STRING(a);
            if ((mcs_uint_t)i < str->len) {
                uint8_t c0 = (uint8_t)str->chars[i];
                uint32_t cp = c0; int extra = 0;
                if (c0 >= 0xF0) { cp = c0 & 7; extra = 3; } else if (c0 >= 0xE0) { cp = c0 & 15; extra = 2; } else if (c0 >= 0xC0) { cp = c0 & 31; extra = 1; }
                for (int k = 1; k <= extra && (mcs_uint_t)(i + k) < str->len; k++) cp = (cp << 6) | ((uint8_t)str->chars[i + k] & 0x3F);
                PUSH(mcs_char(cp));
                slots[s + 1].as.i = i + 1 + extra;
            } else ip += o;
            DISPATCH();
        }
        if (IS_KIND(a, MCS_O_DICT)) {
            mcs_dict_t* d = AS_DICT(a);
            if ((mcs_uint_t)i < d->count) {
                mcs_instance_t* kv = mcs_new_instance(vm, vm->cls_kvp);
                kv->fields[0] = d->keys[i]; kv->fields[1] = DICT_VAL(d, i);
                PUSH(OBJ_VAL(kv));
                slots[s + 1].as.i = i + 1;
            } else ip += o;
            DISPATCH();
        }
        SAVE();
        if ((IS_KIND(a, MCS_O_INSTANCE) || IS_KIND(a, MCS_O_USERDATA)) && i == 0) {
            mcs_value_t m, r;
            mcs_string_t* gn = mcs_intern_c(vm, "GetEnumerator");
            if (mcs_table_get_s(&mcs_class_of(vm, a)->methods, gn, &m)) {
                if (mcs_call_internal(vm, m, a, 0, NULL, &r) != MCS_OK) THROWN();
                if (IS_KIND(r, MCS_O_ARRAY) || IS_KIND(r, MCS_O_LIST) || IS_KIND(r, MCS_O_DICT) || IS_STRING(r)) {
                    slots[s] = r;
                    ip -= 4; /* re-run FOR_ITER on the returned collection */
                    DISPATCH();
                }
                mcs_throw(vm, EXC_INVOP, "GetEnumerator() must return an array, List, Dictionary or string");
                THROWN();
            }
        }
        if (a.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
        else mcs_throw(vm, EXC_INVOP, "foreach cannot operate on a value of type '%s'", mcs_type_name(vm, a));
        THROWN();
    }
    CASE(TRY) {
        uint16_t o = READ16();
        if (vm->handler_count >= MCS_MAX_HANDLERS) { SAVE(); mcs_throw(vm, EXC_STACKOVF, "too many nested try blocks"); THROWN(); }
        mcs_handler_t* h = &vm->handlers[vm->handler_count++];
        h->frame = vm->frame_count - 1; h->ip = ip + o; h->sp = (uint32_t)(vm->sp - vm->stack);
        DISPATCH();
    }
    CASE(END_TRY) vm->handler_count--; DISPATCH();
    CASE(THROW) {
        a = POP();
        SAVE();
        if (a.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
        else mcs_throw_value(vm, a);
        THROWN();
    }
    DISPATCH_END()

on_exception:
    if (unwind(vm, base_frame)) { LOAD(); DISPATCH(); }
    vm->run_depth--;
    return MCS_ERR_RUNTIME;
on_abort:
    vm->abort_req = false;
    vm->run_depth--;
    mcs_throw(vm, EXC_SYSTEM, "Script aborted");
    vm->exc_value = mcs_null(); vm->has_exc = true;
    return MCS_ERR_ABORTED;
}

/* ================================================================ calling from C */
mcs_result_t mcs_call_internal(mcs_vm_t* vm, mcs_value_t callee, mcs_value_t self, int argc, const mcs_value_t* argv, mcs_value_t* result) {
    if (vm->sp + argc + 1 + MCS_STACK_MARGIN >= vm->stack_end) { mcs_throw(vm, EXC_STACKOVF, "Stack overflow."); return MCS_ERR_RUNTIME; }
    mcs_value_t* base = vm->sp;
    int base_frame = vm->frame_count;
    int base_handlers = vm->handler_count;
    PUSH(self.type == MCS_T_NULL ? callee : self);
    for (int i = 0; i < argc; i++) PUSH(argv[i]);
    int st;
    if (self.type != MCS_T_NULL) st = call_method(vm, callee, argc);
    else st = call_value(vm, callee, argc);
    mcs_result_t res = MCS_OK;
    if (st == CALL_FRAME) res = run(vm, base_frame);
    else if (st == CALL_ERR) res = MCS_ERR_RUNTIME;
    if (res == MCS_OK) { if (result) *result = vm->sp[-1]; }
    else if (result) *result = mcs_null();
    if (res != MCS_OK) {
        close_upvalues(vm, base);
        vm->frame_count = base_frame;
        vm->handler_count = base_handlers;
    }
    vm->sp = base;
    return res;
}

static void report_uncaught(mcs_vm_t* vm) {
    mcs_value_t e = vm->exc_value;
    char head[256];
    const char* tname = mcs_type_name(vm, e);
    const char* msg = "";
    const char* trace = "";
    if (IS_KIND(e, MCS_O_INSTANCE)) {
        mcs_value_t v;
        mcs_instance_t* in = AS_INSTANCE(e);
        if (mcs_table_get_s(&in->cls->fields, vm->s_message, &v) && IS_STRING(in->fields[v.as.i])) msg = AS_CSTR(in->fields[v.as.i]);
        mcs_string_t* st = mcs_intern_c(vm, "StackTrace");
        if (mcs_table_get_s(&in->cls->fields, st, &v) && IS_STRING(in->fields[v.as.i])) trace = AS_CSTR(in->fields[v.as.i]);
    } else if (IS_STRING(e)) msg = AS_CSTR(e);
    snprintf(head, sizeof head, "Unhandled exception. %s: %s", tname, msg);
    { size_t n = strlen(head); if (n >= sizeof vm->error) n = sizeof vm->error - 1; memcpy(vm->error, head, n); vm->error[n] = 0; }
    mcs_report_error(vm, "%s\n%s", head, trace);
}

static mcs_result_t finish(mcs_vm_t* vm, mcs_result_t r) {
    if (r == MCS_ERR_RUNTIME && vm->has_exc) report_uncaught(vm);
    if (r == MCS_ERR_ABORTED) {
        const char* why = vm->abort_reason == MCS_ABORT_TIME ? ": time limit exceeded"
                        : vm->abort_reason == MCS_ABORT_STEPS ? ": step limit exceeded" : "";
        snprintf(vm->error, sizeof vm->error, "script aborted%s", why);
        mcs_report_error(vm, "Script aborted%s.\n", why);
    }
    if (r != MCS_OK) {
        vm->has_exc = false; vm->exc_value = mcs_null();
        if (vm->run_depth == 0) {
            vm->sp = vm->stack; vm->frame_count = 0; vm->handler_count = 0;
            close_upvalues(vm, vm->stack);
        }
    }
    return r;
}

/* protects against OOM panics */
#define GUARD_BEGIN(vm) jmp_buf _jb; jmp_buf* _prev = (vm)->panic; mcs_value_t* _sp = (vm)->sp; int _fc = (vm)->frame_count; \
    int _hc = (vm)->handler_count; int _gp = (vm)->gc_pause; int _rd = (vm)->run_depth; int _code; \
    (vm)->panic = &_jb; \
    if ((_code = setjmp(_jb)) != 0) { (vm)->panic = _prev; (vm)->sp = _sp; (vm)->frame_count = _fc; (vm)->handler_count = _hc; \
        (vm)->gc_pause = _gp; (vm)->run_depth = _rd; close_upvalues(vm, _sp); (vm)->has_exc = false; \
        mcs_report_error(vm, "fatal: %s\n", (vm)->error); return (mcs_result_t)_code; }
#define GUARD_END(vm) (vm)->panic = _prev

mcs_result_t mcs_call_value(mcs_vm_t* vm, mcs_value_t callee, int argc, const mcs_value_t* argv, mcs_value_t* result) {
    GUARD_BEGIN(vm);
    mcs_result_t r = mcs_call_internal(vm, callee, mcs_null(), argc, argv, result);
    GUARD_END(vm);
    if (vm->run_depth > 0) return r; /* nested: keep exception pending */
    return finish(vm, r);
}

mcs_result_t mcs_new_object(mcs_vm_t* vm, const char* class_name, int argc, const mcs_value_t* argv, mcs_value_t* result) {
    mcs_value_t cls = mcs_get_global(vm, class_name);
    if (!IS_KIND(cls, MCS_O_CLASS)) {
        mcs_raise(vm, "InvalidOperationException", "Could not load type '%s'", class_name);
        if (vm->run_depth > 0) return MCS_ERR_RUNTIME;
        return finish(vm, MCS_ERR_RUNTIME);
    }
    return mcs_call_value(vm, cls, argc, argv, result);
}

mcs_result_t mcs_call(mcs_vm_t* vm, const char* name, int argc, const mcs_value_t* argv, mcs_value_t* result) {
    const char* dot = strchr(name, '.');
    if (dot) {
        char cls[64]; size_t n = (size_t)(dot - name); if (n > 63) n = 63;
        memcpy(cls, name, n); cls[n] = 0;
        mcs_value_t c = mcs_get_global(vm, cls);
        if (!IS_KIND(c, MCS_O_CLASS)) { snprintf(vm->error, sizeof vm->error, "class '%s' not found", cls); return MCS_ERR_RUNTIME; }
        return mcs_invoke(vm, c, dot + 1, argc, argv, result);
    }
    mcs_value_t f = mcs_get_global(vm, name);
    if (f.type == MCS_T_NULL) { snprintf(vm->error, sizeof vm->error, "function '%s' not found", name); return MCS_ERR_RUNTIME; }
    return mcs_call_value(vm, f, argc, argv, result);
}

mcs_result_t mcs_invoke(mcs_vm_t* vm, mcs_value_t recv, const char* name, int argc, const mcs_value_t* argv, mcs_value_t* result) {
    GUARD_BEGIN(vm);
    mcs_string_t* nm = mcs_intern_c(vm, name);
    mcs_value_t* base = vm->sp;
    int base_frame = vm->frame_count;
    PUSH(recv);
    for (int i = 0; i < argc; i++) PUSH(argv[i]);
    int st = invoke_op(vm, nm, argc);
    mcs_result_t r = MCS_OK;
    if (st == CALL_FRAME) r = run(vm, base_frame);
    else if (st == CALL_ERR) r = MCS_ERR_RUNTIME;
    if (result) *result = r == MCS_OK ? vm->sp[-1] : mcs_null();
    if (r != MCS_OK) { close_upvalues(vm, base); vm->frame_count = base_frame; }
    vm->sp = base;
    GUARD_END(vm);
    if (vm->run_depth > 0) return r;
    return finish(vm, r);
}

static mcs_result_t exec_function(mcs_vm_t* vm, mcs_function_t* fn) {
    vm->gc_pause++;
    mcs_closure_t* cl = mcs_new_closure(vm, fn);
    vm->gc_pause--;
    mcs_value_t res;
    mcs_result_t r = mcs_call_internal(vm, OBJ_VAL(cl), mcs_null(), 0, NULL, &res);
    return finish(vm, r);
}

mcs_result_t mcs_run_closure(mcs_vm_t* vm, mcs_closure_t* cl) {
    mcs_value_t res;
    return finish(vm, mcs_call_internal(vm, OBJ_VAL(cl), mcs_null(), 0, NULL, &res));
}

#if MCS_ENABLE_COMPILER
mcs_result_t mcs_exec_source(mcs_vm_t* vm, const char* name, const char* src) {
    GUARD_BEGIN(vm);
    vm->error[0] = 0;
    mcs_function_t* fn = mcs_compile(vm, name, src);
    if (!fn) { GUARD_END(vm); return MCS_ERR_COMPILE; }
    vm->gc_pause++;
    mcs_push_root(vm, OBJ_VAL(fn));
    vm->gc_pause--;
    mcs_result_t r = exec_function(vm, fn);
    mcs_pop_root(vm, 1);
    GUARD_END(vm);
    return r;
}
#if MCS_ENABLE_DISASM
mcs_result_t mcs_disassemble_source(mcs_vm_t* vm, const char* name, const char* src) {
    GUARD_BEGIN(vm);
    mcs_function_t* fn = mcs_compile(vm, name, src);
    if (!fn) { GUARD_END(vm); return MCS_ERR_COMPILE; }
    vm->gc_pause++;
    mcs_disassemble(vm, fn, 0);
    vm->gc_pause--;
    GUARD_END(vm);
    return MCS_OK;
}
#endif
#endif

#if MCS_ENABLE_BYTECODE_LOAD
mcs_function_t* mcs_load_image(mcs_vm_t* vm, const uint8_t* img, size_t len);
mcs_function_t* mcs_load_image_ex(mcs_vm_t* vm, const uint8_t* img, size_t len, bool xip);
static mcs_result_t exec_image(mcs_vm_t* vm, const uint8_t* image, size_t len, bool xip) {
    GUARD_BEGIN(vm);
    vm->error[0] = 0;
    mcs_function_t* fn = mcs_load_image_ex(vm, image, len, xip);
    if (!fn) { GUARD_END(vm); mcs_report_error(vm, "%s\n", vm->error); return MCS_ERR_BYTECODE; }
    mcs_push_root(vm, OBJ_VAL(fn));
    mcs_result_t r = exec_function(vm, fn);
    mcs_pop_root(vm, 1);
    GUARD_END(vm);
    return r;
}
mcs_result_t mcs_exec_image(mcs_vm_t* vm, const uint8_t* image, size_t len) { return exec_image(vm, image, len, false); }
mcs_result_t mcs_exec_image_xip(mcs_vm_t* vm, const uint8_t* image, size_t len) { return exec_image(vm, image, len, MCS_ENABLE_XIP != 0); }
#endif
#if MCS_ENABLE_DISASM
mcs_result_t mcs_disassemble_image(mcs_vm_t* vm, const uint8_t* image, size_t len) {
    GUARD_BEGIN(vm);
    vm->error[0] = 0;
    mcs_function_t* fn = mcs_load_image(vm, image, len);
    if (!fn) { GUARD_END(vm); mcs_report_error(vm, "%s\n", vm->error); return MCS_ERR_BYTECODE; }
    vm->gc_pause++;
    mcs_disassemble(vm, fn, 0);
    vm->gc_pause--;
    GUARD_END(vm);
    return MCS_OK;
}
#endif

const char* mcs_last_error(mcs_vm_t* vm) { return vm->error; }
void mcs_request_abort(mcs_vm_t* vm) { vm->abort_req = true; vm->hook_counter = 1; }
void mcs_set_limits(mcs_vm_t* vm, const mcs_limits_t* l) {
    if (l) vm->limits = *l; else memset(&vm->limits, 0, sizeof vm->limits);
}
mcs_abort_reason_t mcs_abort_reason(mcs_vm_t* vm) { return (mcs_abort_reason_t)vm->abort_reason; }
uint32_t mcs_steps_used(mcs_vm_t* vm) { return vm->steps_used + (vm->hook_reload - vm->hook_counter); }
void mcs_set_ext(mcs_vm_t* vm, int slot, void* p) { if (slot >= 0 && slot < MCS_EXT__COUNT) vm->ext[slot] = p; }
void* mcs_get_ext(mcs_vm_t* vm, int slot) { return slot >= 0 && slot < MCS_EXT__COUNT ? vm->ext[slot] : NULL; }

uint32_t mcs_features(void) {
    return (MCS_ENABLE_COMPILER ? MCS_FEAT_COMPILER : 0) | (MCS_ENABLE_BYTECODE_LOAD ? MCS_FEAT_IMAGE_LOAD : 0)
         | (MCS_ENABLE_BYTECODE_SAVE ? MCS_FEAT_IMAGE_SAVE : 0) | (MCS_ENABLE_FLOAT ? MCS_FEAT_FLOAT : 0)
         | (MCS_ENABLE_FLOAT && MCS_FLOAT_DOUBLE ? MCS_FEAT_DOUBLE : 0) | (MCS_INT64 ? MCS_FEAT_INT64 : 0)
         | (MCS_ENABLE_LINES ? MCS_FEAT_LINES : 0) | (MCS_ENABLE_DISASM ? MCS_FEAT_DISASM : 0)
         | (MCS_ENABLE_LIST ? MCS_FEAT_LIST : 0) | (MCS_ENABLE_DICT ? MCS_FEAT_DICT : 0)
         | (MCS_ENABLE_FS ? MCS_FEAT_FS : 0) | (MCS_ENABLE_HAL ? MCS_FEAT_HAL : 0)
         | (MCS_ENABLE_SCHED ? MCS_FEAT_SCHED : 0) | (MCS_ENABLE_SHELL ? MCS_FEAT_SHELL : 0);
}

mcs_result_t mcs_fail(mcs_vm_t* vm, mcs_result_t code, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(vm->error, sizeof vm->error, fmt, ap); va_end(ap);
    mcs_report_error(vm, "%s\n", vm->error);
    return code;
}

bool mcs_is_image(const void* data, size_t len) { return len >= 4 && !memcmp(data, "MCSB", 4); }

mcs_result_t mcs_exec_auto(mcs_vm_t* vm, const char* name, const void* data, size_t len) {
    if (mcs_is_image(data, len)) {
#if MCS_ENABLE_BYTECODE_LOAD
        return mcs_exec_image(vm, (const uint8_t*)data, len);
#else
        return mcs_fail(vm, MCS_ERR_BYTECODE, "image loading not supported by this build");
#endif
    }
#if MCS_ENABLE_COMPILER
    if (len && ((const char*)data)[len - 1] == 0) return mcs_exec_source(vm, name, (const char*)data);
    /* raw allocator: mcs_realloc would panic outside a guarded region */
    char* copy = vm->cfg.realloc_fn ? (char*)vm->cfg.realloc_fn(vm->cfg.alloc_ud, NULL, 0, len + 1) : (char*)malloc(len + 1);
    if (!copy) { snprintf(vm->error, sizeof vm->error, "out of memory"); return MCS_ERR_MEMORY; }
    memcpy(copy, data, len); copy[len] = 0;
    mcs_result_t r = mcs_exec_source(vm, name, copy);
    if (vm->cfg.realloc_fn) vm->cfg.realloc_fn(vm->cfg.alloc_ud, copy, len + 1, 0); else free(copy);
    return r;
#else
    (void)name;
    return mcs_fail(vm, MCS_ERR_COMPILE, "source given but this build has no compiler");
#endif
}

/* ================================================================ roots */
void mcs_push_root(mcs_vm_t* vm, mcs_value_t v) {
    if (vm->root_count >= MCS_MAX_ROOTS) mcs_panic(vm, MCS_ERR_MEMORY, "too many temporary roots");
    vm->roots[vm->root_count++] = v;
}
void mcs_pop_root(mcs_vm_t* vm, int n) { vm->root_count -= n; if (vm->root_count < 0) vm->root_count = 0; }
int mcs_pin(mcs_vm_t* vm, mcs_value_t v) {
    for (int i = 0; i < MCS_MAX_PINS; i++) if (vm->pins[i].type == MCS_T_NULL) { vm->pins[i] = v; return i; }
    return -1;
}
mcs_value_t mcs_pinned(mcs_vm_t* vm, int h) { return h >= 0 && h < MCS_MAX_PINS ? vm->pins[h] : mcs_null(); }
void mcs_unpin(mcs_vm_t* vm, int h) { if (h >= 0 && h < MCS_MAX_PINS) vm->pins[h] = mcs_null(); }
void mcs_gc(mcs_vm_t* vm) { mcs_collect(vm); }
void mcs_mem_stats(mcs_vm_t* vm, mcs_mem_stats_t* st) {
    st->bytes_in_use = vm->bytes_allocated; st->peak_bytes = vm->peak_bytes; st->next_gc = vm->next_gc;
    st->objects = vm->object_count; st->collections = vm->gc_count;
}

mcs_value_t mcs_get_field(mcs_vm_t* vm, mcs_value_t obj, const char* name) {
    mcs_value_t r = mcs_null();
    mcs_string_t* nm = mcs_intern_c(vm, name);
    if (IS_KIND(obj, MCS_O_INSTANCE)) {
        mcs_value_t slot;
        if (mcs_table_get_s(&AS_INSTANCE(obj)->cls->fields, nm, &slot)) return AS_INSTANCE(obj)->fields[slot.as.i];
    }
    if (IS_KIND(obj, MCS_O_CLASS)) { if (mcs_table_get_s(&AS_CLASS(obj)->statics, nm, &r)) return r; }
    mcs_class_t* cls = IS_KIND(obj, MCS_O_CLASS) ? AS_CLASS(obj) : mcs_class_of(vm, obj);
    mcs_value_t g;
    if (cls && mcs_cls_get(vm, cls, MCS_TAB_GETTERS, nm, &g)) mcs_call_internal(vm, g, obj, 0, NULL, &r);
    return r;
}
void mcs_set_field(mcs_vm_t* vm, mcs_value_t obj, const char* name, mcs_value_t v) {
    mcs_string_t* nm = mcs_intern_c(vm, name);
    if (IS_KIND(obj, MCS_O_INSTANCE)) {
        mcs_value_t slot;
        if (mcs_table_get_s(&AS_INSTANCE(obj)->cls->fields, nm, &slot)) { AS_INSTANCE(obj)->fields[slot.as.i] = v; return; }
    }
    if (IS_KIND(obj, MCS_O_CLASS)) { mcs_table_set(vm, &AS_CLASS(obj)->statics, OBJ_VAL(nm), v); return; }
    mcs_class_t* cls = mcs_class_of(vm, obj);
    mcs_value_t s;
    if (cls && mcs_cls_get(vm, cls, MCS_TAB_SETTERS, nm, &s)) mcs_call_internal(vm, s, obj, 1, &v, NULL);
}

/* ================================================================ lifecycle */
void mcs_config_default(mcs_config_t* cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->stack_slots = MCS_DEFAULT_STACK;
    cfg->max_frames = MCS_DEFAULT_FRAMES;
    cfg->stdlib = MCS_LIB_ALL;
}

mcs_vm_t* mcs_new(const mcs_config_t* cfg_in) {
    mcs_config_t cfg;
    if (cfg_in) cfg = *cfg_in; else mcs_config_default(&cfg);
    if (!cfg.stack_slots) cfg.stack_slots = MCS_DEFAULT_STACK;
    if (!cfg.max_frames) cfg.max_frames = MCS_DEFAULT_FRAMES;
    mcs_vm_t* vm = cfg.realloc_fn ? (mcs_vm_t*)cfg.realloc_fn(cfg.alloc_ud, NULL, 0, sizeof(mcs_vm_t)) : (mcs_vm_t*)malloc(sizeof(mcs_vm_t));
    if (!vm) return NULL;
    memset(vm, 0, sizeof *vm);
    vm->cfg = cfg;
    vm->next_gc = MCS_GC_INITIAL;
    if (cfg.heap_limit && vm->next_gc > cfg.heap_limit / 2) vm->next_gc = cfg.heap_limit / 2;
    vm->hook_counter = MCS_HOOK_INTERVAL;
    vm->exc_value = mcs_null();
    for (int i = 0; i < MCS_MAX_PINS; i++) vm->pins[i] = mcs_null();
    memset(&vm->strings, 0, sizeof vm->strings);
    jmp_buf jb;
    vm->panic = &jb;
    if (setjmp(jb)) { vm->panic = NULL; mcs_free(vm); return NULL; }
    vm->gc_pause++;
    vm->stack = MCS_ALLOC(vm, mcs_value_t, cfg.stack_slots);
    vm->sp = vm->stack; vm->stack_end = vm->stack + cfg.stack_slots;
    vm->frames = MCS_ALLOC(vm, mcs_frame_t, cfg.max_frames);
    vm->s_ctor = mcs_intern_c(vm, ".ctor");
    vm->s_init = mcs_intern_c(vm, "$init");
    vm->s_tostring = mcs_intern_c(vm, "ToString");
    vm->s_message = mcs_intern_c(vm, "Message");
    vm->s_item = mcs_intern_c(vm, "Item");
    vm->s_main = mcs_intern_c(vm, "Main");
    vm->s_key = mcs_intern_c(vm, "Key");
    vm->s_value = mcs_intern_c(vm, "Value");
    vm->s_equals = mcs_intern_c(vm, "Equals");
    mcs_open_libs(vm, cfg.stdlib);
    vm->gc_pause--;
    vm->panic = NULL;
    return vm;
}

void mcs_free(mcs_vm_t* vm) {
    if (!vm) return;
    vm->gc_pause++;
    mcs_free_objects(vm);
    mcs_strset_free(vm, &vm->strings);
    mcs_table_free(vm, &vm->tuple_classes);
    MCS_FREE(vm, mcs_value_t, vm->globals, vm->global_cap);
    MCS_FREE(vm, mcs_string_t*, vm->global_names, vm->global_cap);
    if (vm->stack) MCS_FREE(vm, mcs_value_t, vm->stack, vm->cfg.stack_slots);
    if (vm->frames) MCS_FREE(vm, mcs_frame_t, vm->frames, vm->cfg.max_frames);
    if (vm->cfg.realloc_fn) vm->cfg.realloc_fn(vm->cfg.alloc_ud, vm, sizeof(mcs_vm_t), 0);
    else free(vm);
}
void* mcs_user_data(mcs_vm_t* vm) { return vm->cfg.user_data; }
