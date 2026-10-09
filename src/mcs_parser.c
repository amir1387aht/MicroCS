/* MicroCS parser: tokens -> AST (recursive descent, speculative lookahead). */
#include "mcs_front.h"
#if MCS_ENABLE_COMPILER
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    front_ctx_t* ctx;
    arena_t* A;
    token_t* t;
    uint32_t n, pos;
    program_t* prog;
    jmp_buf jb;
    typeref_t* target_type;   /* for target-typed `new()` */
    const char* cur_class; uint32_t cur_class_len;
    typeref_t* cur_ret;       /* declared return type of the function being parsed (tuple names) */
    uint32_t tmp_id;          /* desugaring temporaries ($t0, $t1, ...) */
    /* variables declared with a named tuple type in the current function, so
       later `v = (1, 2)` assignments keep the element names (types are erased) */
    struct { const char* name; uint32_t len; typeref_t* ty; } tvars[24];
    int ntvars;
    /* fields/properties/variables declared anywhere with a named tuple type, found
       by a token pre-scan so `obj.P = (1, 2)` works before the class is parsed */
    struct { const char* name; uint32_t len; typeref_t* ty; } tmems[32];
    int ntmems;
} parser_t;

uint32_t mcs_decode_escape(const char** pp, const char* end);

/* ------------------------------------------------------------ utils */
#define CUR (&P->t[P->pos])
#define PEEK(k) (&P->t[P->pos + (k) < P->n ? P->pos + (k) : P->n - 1])
#define TT (CUR->type)

static void perr(parser_t* P, token_t* t, const char* fmt, ...) {
    char msg[160];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    mcs_front_error(P->ctx, t->line, t->col, "%s", msg);
    longjmp(P->jb, 1);
}
static token_t* adv(parser_t* P) { token_t* t = CUR; if (P->pos < P->n - 1) P->pos++; return t; }
static bool check(parser_t* P, int type) { return TT == type; }
static bool match(parser_t* P, int type) { if (TT == type) { adv(P); return true; } return false; }
static const char* tokname(token_t* t) {
    static char buf[48];
    if (t->type == TK_EOF) return "end of file";
    size_t n = t->len < 40 ? t->len : 40;
    buf[0] = '\''; memcpy(buf + 1, t->start, n); buf[n + 1] = '\''; buf[n + 2] = 0;
    return buf;
}
static token_t* expect(parser_t* P, int type, const char* what) {
    if (TT != type) perr(P, CUR, "expected %s but found %s", what, tokname(CUR));
    return adv(P);
}
static bool is_word(token_t* t, const char* w) { size_t n = strlen(w); return t->type == TK_IDENT && t->len == n && memcmp(t->start, w, n) == 0; }
static bool adjacent(token_t* a, token_t* b) { return a->start + a->len == b->start; }

static node_t* mk(parser_t* P, int kind, token_t* at) {
    node_t* n = (node_t*)arena_alloc(P->A, sizeof(node_t));
    n->kind = (uint8_t)kind; n->line = at ? at->line : CUR->line;
    return n;
}

/* append to singly linked list via tail pointer */
#define LIST_APPEND(head, tail, item) do { if (!(head)) (head) = (item); else (tail)->next = (item); (tail) = (item); while ((tail)->next) (tail) = (tail)->next; } while (0)

static void utf8_put(char* out, size_t* n, uint32_t cp) {
    if (cp < 0x80) out[(*n)++] = (char)cp;
    else if (cp < 0x800) { out[(*n)++] = (char)(0xC0 | (cp >> 6)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out[(*n)++] = (char)(0xE0 | (cp >> 12)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
    else { out[(*n)++] = (char)(0xF0 | (cp >> 18)); out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
}

/* decode literal text (escapes, "" in verbatim, {{ }} in interpolated) */
static char* decode_str(parser_t* P, const char* s, size_t len, bool verbatim, bool interp, uint32_t* out_len) {
    char* out = (char*)arena_alloc(P->A, len * 4 + 1);
    size_t n = 0;
    const char* e = s + len;
    while (s < e) {
        char c = *s;
        if (!verbatim && c == '\\' && s + 1 < e) { s++; utf8_put(out, &n, mcs_decode_escape(&s, e)); continue; }
        if (verbatim && c == '"' && s + 1 < e && s[1] == '"') { out[n++] = '"'; s += 2; continue; }
        if (interp && (c == '{' || c == '}') && s + 1 < e && s[1] == c) { out[n++] = c; s += 2; continue; }
        out[n++] = c; s++;
    }
    out[n] = 0;
    *out_len = (uint32_t)n;
    return out;
}

uint8_t mcs_prim_of(const char* s, uint32_t n, uint8_t* conv) {
    if (n < 3 || n > 7) { if (conv) *conv = 0xff; return PT_ANY; }   /* all names are 3..7 chars */
    static const struct { const char* name; uint8_t pt, cv; } tab[] = {
        {"int", PT_INT, CV_INT}, {"Int32", PT_INT, CV_INT}, {"long", PT_INT, CV_INT}, {"Int64", PT_INT, CV_INT},
        {"uint", PT_INT, CV_UINT}, {"UInt32", PT_INT, CV_UINT}, {"ulong", PT_INT, CV_INT}, {"nint", PT_INT, CV_INT},
        {"short", PT_INT, CV_SHORT}, {"Int16", PT_INT, CV_SHORT}, {"ushort", PT_INT, CV_USHORT}, {"UInt16", PT_INT, CV_USHORT},
        {"byte", PT_INT, CV_BYTE}, {"Byte", PT_INT, CV_BYTE}, {"sbyte", PT_INT, CV_SBYTE}, {"SByte", PT_INT, CV_SBYTE},
        {"double", PT_FLOAT, CV_FLOAT}, {"Double", PT_FLOAT, CV_FLOAT}, {"float", PT_FLOAT, CV_FLOAT},
        {"Single", PT_FLOAT, CV_FLOAT}, {"decimal", PT_FLOAT, CV_FLOAT},
        {"bool", PT_BOOL, CV_BOOL}, {"Boolean", PT_BOOL, CV_BOOL}, {"string", PT_STRING, 0xff}, {"String", PT_STRING, 0xff},
        {"char", PT_CHAR, CV_CHAR}, {"Char", PT_CHAR, CV_CHAR}, {"void", PT_VOID, 0xff},
        {"object", PT_OBJECT, 0xff}, {"Object", PT_OBJECT, 0xff}, {"dynamic", PT_OBJECT, 0xff},
        {NULL, 0, 0}
    };
    for (int i = 0; tab[i].name; i++)
        if (tab[i].name[0] == s[0] && strncmp(tab[i].name, s, n) == 0 && tab[i].name[n] == 0) { if (conv) *conv = tab[i].cv; return tab[i].pt; }
    if (conv) *conv = 0xff;
    return PT_ANY;
}

static bool is_prim_kw(int t) {
    switch (t) {
    case TK_BOOL: case TK_BYTE: case TK_KCHAR: case TK_DECIMAL: case TK_DOUBLE: case TK_KFLOAT: case TK_KINT:
    case TK_LONG: case TK_OBJECT: case TK_SBYTE: case TK_SHORT: case TK_KSTRING: case TK_UINT: case TK_ULONG:
    case TK_USHORT: case TK_VOID: return true;
    default: return false;
    }
}

/* ------------------------------------------------------------ types */
static bool skip_type_args(parser_t* P);
/* Parse a type. When `spec` is true failures return NULL without error. */
static typeref_t* parse_type(parser_t* P, bool spec) {
    token_t* t = CUR;
    typeref_t* ty;
    if (is_prim_kw(t->type) || t->type == TK_IDENT) {
        adv(P);
        const char* name = t->start; uint32_t len = t->len;
        if (t->type == TK_IDENT) {
            /* qualified names: take the last component */
            while (check(P, TK_DOT) && PEEK(1)->type == TK_IDENT) { adv(P); token_t* q = adv(P); name = q->start; len = q->len; }
            if (check(P, TK_LT)) {
                uint32_t save = P->pos;
                if (!skip_type_args(P)) { P->pos = save; if (!spec) perr(P, CUR, "invalid generic type arguments"); return NULL; }
            }
        }
        ty = (typeref_t*)arena_alloc(P->A, sizeof(typeref_t));
        ty->name = name; ty->len = len;
        ty->prim = mcs_prim_of(name, len, &ty->conv);
        ty->is_var = (t->type == TK_IDENT && len == 3 && memcmp(name, "var", 3) == 0);
    } else if (t->type == TK_LPAREN) {
        /* tuple type: (T1 [name1], T2 [name2], ...) - erased to ValueTuple */
        adv(P);
        char* names = NULL; uint32_t nlen = 0, count = 0; bool any_name = false;
        char nb[256]; uint32_t np = 0;
        for (;;) {
            if (!parse_type(P, true)) { if (!spec) perr(P, CUR, "expected a tuple element type"); return NULL; }
            if (count) { if (np < sizeof nb) nb[np++] = ','; }
            if (TT == TK_IDENT && (PEEK(1)->type == TK_COMMA || PEEK(1)->type == TK_RPAREN)) {
                token_t* nm = adv(P); any_name = true;
                for (uint32_t i = 0; i < nm->len && np < sizeof nb; i++) nb[np++] = nm->start[i];
            }
            count++;
            if (match(P, TK_COMMA)) continue;
            if (match(P, TK_RPAREN)) break;
            if (!spec) perr(P, CUR, "expected ',' or ')' in tuple type");
            return NULL;
        }
        if (count < 2) { if (!spec) perr(P, t, "a tuple type needs at least two elements"); return NULL; }
        if (any_name) { names = arena_strdup(P->A, nb, np); nlen = np; }
        ty = (typeref_t*)arena_alloc(P->A, sizeof(typeref_t));
        ty->name = "ValueTuple"; ty->len = 10;
        ty->prim = mcs_prim_of(ty->name, ty->len, &ty->conv);
        ty->tnames = names; ty->tnames_len = nlen;
    } else {
        if (!spec) perr(P, t, "expected a type but found %s", tokname(t));
        return NULL;
    }
    for (;;) {
        if (check(P, TK_QUESTION)) {
            int nt = PEEK(1)->type;
            if (nt == TK_IDENT || nt == TK_LBRACK || nt == TK_GT || nt == TK_COMMA || nt == TK_RPAREN || nt == TK_THIS || nt == TK_OPERATOR) { adv(P); continue; }
            break;
        }
        if (check(P, TK_LBRACK) && PEEK(1)->type == TK_RBRACK) { adv(P); adv(P); ty->rank++; continue; }
        if (check(P, TK_LBRACK) && PEEK(1)->type == TK_COMMA) {
            if (spec) return NULL;
            perr(P, CUR, "multi-dimensional arrays are not supported; use jagged arrays (T[][])");
        }
        if (check(P, TK_STAR) && spec) return NULL;
        break;
    }
    return ty;
}

static bool skip_type_args(parser_t* P) {
    if (!match(P, TK_LT)) return false;
    if (check(P, TK_GT)) { adv(P); return true; }
    for (;;) {
        if (!parse_type(P, true)) return false;
        if (match(P, TK_COMMA)) continue;
        if (match(P, TK_GT)) return true;
        return false;
    }
}

/* speculative type parse; restores on failure */
static typeref_t* try_type(parser_t* P) {
    uint32_t save = P->pos;
    typeref_t* t = parse_type(P, true);
    if (!t) P->pos = save;
    return t;
}

/* ------------------------------------------------------------ expressions */
static node_t* parse_expr(parser_t* P);
static node_t* parse_unary(parser_t* P);
static node_t* parse_stmt(parser_t* P);
static node_t* parse_block(parser_t* P);
static node_t* parse_ternary(parser_t* P);
static param_t* parse_params(parser_t* P, int close, uint8_t* count);
static node_t* parse_binary(parser_t* P, int prec);

static node_t* parse_args(parser_t* P, int close) {
    node_t *h = NULL, *tl = NULL;
    if (match(P, close)) return NULL;
    for (;;) {
        if (TT == TK_IDENT && PEEK(1)->type == TK_COLON) perr(P, CUR, "named arguments are not supported");
        node_t* a;
        if (match(P, TK_IN)) a = parse_expr(P);          /* `in` = read-only by-reference: value semantics are equivalent */
        else if (check(P, TK_OUT) || check(P, TK_REF)) {
            token_t* kw = adv(P);
            a = mk(P, N_ARG_OUT, kw);
            a->flag = kw->type == TK_OUT ? 1 : 2;
            a->ival = -1;                                  /* cell slot, assigned by the compiler's predeclare pass */
            uint32_t save = P->pos;
            typeref_t* ty = NULL;
            if (a->flag == 1 && is_word(CUR, "_") && (PEEK(1)->type == TK_COMMA || PEEK(1)->type == close)) {
                adv(P);                                    /* out _  (discard) */
            } else if (a->flag == 1 && (ty = try_type(P)) != NULL && TT == TK_IDENT &&
                       (PEEK(1)->type == TK_COMMA || PEEK(1)->type == close)) {
                token_t* nm = adv(P);                      /* out var x  /  out int x */
                a->type = ty;
                if (!(nm->len == 1 && nm->start[0] == '_')) { a->name = nm->start; a->len = nm->len; }
            } else {
                P->pos = save;
                a->a = parse_expr(P);
            }
        } else a = parse_expr(P);
        LIST_APPEND(h, tl, a);
        if (match(P, TK_COMMA)) continue;
        expect(P, close, close == TK_RPAREN ? "')'" : "']'");
        break;
    }
    return h;
}

static funcdecl_t* new_fn(parser_t* P, uint32_t line) {
    funcdecl_t* f = (funcdecl_t*)arena_alloc(P->A, sizeof(funcdecl_t));
    f->line = line;
    return f;
}

static node_t* lambda_body(parser_t* P, node_t* lam) {
    typeref_t* sr = P->cur_ret; P->cur_ret = NULL;
    if (check(P, TK_LBRACE)) { lam->fn->body = parse_block(P); lam->fn->expr_body = 0; }
    else { lam->fn->body = parse_expr(P); lam->fn->expr_body = 1; }
    P->cur_ret = sr;
    return lam;
}

/* ---- desugaring helpers: runtime intrinsics live in the hidden `__rt` module */
static node_t* mk_name(parser_t* P, token_t* at, const char* nm) {
    node_t* n = mk(P, N_NAME, at); n->name = nm; n->len = (uint32_t)strlen(nm); return n;
}
static node_t* mk_str(parser_t* P, token_t* at, const char* s, uint32_t len) {
    node_t* n = mk(P, N_STR, at); n->name = s; n->len = len; return n;
}
static node_t* mk_int(parser_t* P, token_t* at, mcs_int_t v) { node_t* n = mk(P, N_INT, at); n->ival = v; return n; }
static node_t* rt_call(parser_t* P, token_t* at, const char* fn, node_t* args) {
    node_t* m = mk(P, N_MEMBER, at); m->a = mk_name(P, at, "__rt"); m->name = fn; m->len = (uint32_t)strlen(fn);
    node_t* c = mk(P, N_CALL, at); c->a = m; c->b = args;
    return c;
}
static node_t* args2(node_t* a, node_t* b) { a->next = b; return a; }
static const char* tmp_name(parser_t* P) {
    char b[16]; int n = snprintf(b, sizeof b, "$t%u", (unsigned)P->tmp_id++);
    return arena_strdup(P->A, b, (size_t)n);
}
/* re-label a tuple value with the element names of its declared type */
static node_t* tuple_relabel(parser_t* P, token_t* at, typeref_t* ty, node_t* e) {
    if (!e || !ty || !ty->tnames) return e;
    return rt_call(P, at, "Names", args2(e, mk_str(P, at, ty->tnames, ty->tnames_len)));
}
static void tvar_add(parser_t* P, const char* name, uint32_t len, typeref_t* ty) {
    if (!ty || !ty->tnames) return;
    int n = P->ntvars;
    if (n == (int)(sizeof P->tvars / sizeof P->tvars[0])) { /* full: drop the oldest */
        memmove(&P->tvars[0], &P->tvars[1], sizeof P->tvars[0] * (size_t)(n - 1)); n--;
    }
    P->tvars[n].name = name; P->tvars[n].len = len; P->tvars[n].ty = ty;
    P->ntvars = n + 1;
}
static typeref_t* tvar_find(parser_t* P, const char* name, uint32_t len) {
    for (int i = P->ntvars - 1; i >= 0; i--)
        if (P->tvars[i].len == len && memcmp(P->tvars[i].name, name, len) == 0) return P->tvars[i].ty;
    return NULL;
}
static typeref_t* tmem_find(parser_t* P, const char* name, uint32_t len) {
    for (int i = P->ntmems - 1; i >= 0; i--)
        if (P->tmems[i].len == len && memcmp(P->tmems[i].name, name, len) == 0) return P->tmems[i].ty;
    return NULL;
}
/* `( T a, U b ) Name` followed by ; = { => : record Name -> "a,b" */
static void prescan_tuple_members(parser_t* P) {
    for (uint32_t i = 0; i + 3 < P->n && P->ntmems < (int)(sizeof P->tmems / sizeof P->tmems[0]); i++) {
        if (P->t[i].type != TK_LPAREN) continue;
        char nb[160]; uint32_t np = 0; bool named = false, ok = true;
        int depth = 0, angle = 0; uint32_t el_tokens = 0, j = i + 1, el_index = 0;
        token_t* last = NULL;
        for (; j < P->n && ok; j++) {
            token_t* tk = &P->t[j];
            int ty = tk->type;
            if (ty == TK_LPAREN) depth++;
            else if (ty == TK_RPAREN && depth > 0) depth--;
            else if (ty == TK_LT) angle++;
            else if (ty == TK_GT && angle > 0) angle--;
            else if (ty == TK_SEMI || ty == TK_LBRACE || ty == TK_RBRACE || ty == TK_EOF || ty == TK_ASSIGN) ok = false;
            if (!ok) break;
            if (depth == 0 && angle == 0 && (ty == TK_COMMA || ty == TK_RPAREN)) {
                if (el_index++ && np < sizeof nb) nb[np++] = ',';
                if (el_tokens >= 2 && last && last->type == TK_IDENT) {
                    for (uint32_t k = 0; k < last->len && np < sizeof nb; k++) nb[np++] = last->start[k];
                    named = true;
                } else if (el_tokens < 1) ok = false;
                el_tokens = 0; last = NULL;
                if (ty == TK_RPAREN) break;
                continue;
            }
            el_tokens++; last = tk;
        }
        if (!ok || !named || el_index < 2 || j + 2 >= P->n) continue;   /* a tuple type has >= 2 elements */
        token_t* nm = &P->t[j + 1];
        int after = P->t[j + 2].type;
        if (nm->type != TK_IDENT || !(after == TK_SEMI || after == TK_ASSIGN || after == TK_LBRACE || after == TK_ARROW)) continue;
        typeref_t* tr = (typeref_t*)arena_alloc(P->A, sizeof(typeref_t));
        memset(tr, 0, sizeof *tr);
        tr->tnames = arena_strdup(P->A, nb, np); tr->tnames_len = np;
        P->tmems[P->ntmems].name = nm->start; P->tmems[P->ntmems].len = nm->len; P->tmems[P->ntmems].ty = tr;
        P->ntmems++;
    }
}
/* side-effect free expression that may be evaluated twice (x[^1] -> x[Len(x) - 1]) */
static bool pure_expr(node_t* e) {
    if (!e) return false;
    if (e->kind == N_NAME || e->kind == N_THIS) return true;
    if (e->kind == N_MEMBER) return !e->flag && pure_expr(e->a);
    return false;
}

/* is the '(' at the cursor the start of a lambda parameter list? */
static bool paren_is_lambda(parser_t* P) {
    uint32_t i = P->pos; int depth = 0;
    for (; i < P->n; i++) {
        int t = P->t[i].type;
        if (t == TK_LPAREN) depth++;
        else if (t == TK_RPAREN) { if (--depth == 0) break; }
        else if (t == TK_SEMI || t == TK_LBRACE || t == TK_EOF) return false;
    }
    return i + 1 < P->n && P->t[i + 1].type == TK_ARROW;
}

static node_t* parse_initializer(parser_t* P, node_t* target) {
    /* { a, b }  { X = 1 }  { [k] = v }  { {k, v} } */
    expect(P, TK_LBRACE, "'{'");
    node_t *h = NULL, *tl = NULL;
    while (!check(P, TK_RBRACE)) {
        node_t* it;
        if (TT == TK_IDENT && PEEK(1)->type == TK_ASSIGN) {
            token_t* nm = adv(P); adv(P);
            it = mk(P, N_INIT_FIELD, nm); it->name = nm->start; it->len = nm->len;
            it->a = check(P, TK_LBRACE) ? parse_initializer(P, NULL) : tuple_relabel(P, nm, tmem_find(P, nm->start, nm->len), parse_expr(P));
        } else if (check(P, TK_LBRACK)) {
            token_t* at = adv(P);
            it = mk(P, N_INIT_INDEX, at);
            it->a = parse_expr(P);
            expect(P, TK_RBRACK, "']'"); expect(P, TK_ASSIGN, "'='");
            it->b = parse_expr(P);
        } else if (check(P, TK_LBRACE)) {
            token_t* at = adv(P);
            it = mk(P, N_INIT_ADD, at);
            it->a = parse_args(P, TK_RBRACE);
        } else {
            it = mk(P, N_INIT_ADD, CUR);
            it->a = parse_expr(P);
        }
        LIST_APPEND(h, tl, it);
        if (!match(P, TK_COMMA)) break;
    }
    expect(P, TK_RBRACE, "'}'");
    if (!target) { /* nested initializer used as a value: array literal */
        node_t* arr = mk(P, N_ARRAY_LIT, CUR);
        node_t *ah = NULL, *at = NULL;
        for (node_t* i = h; i; ) { node_t* nx = i->next; i->next = NULL; node_t* v = i->a; v->next = NULL; LIST_APPEND(ah, at, v); i = nx; }
        arr->a = ah;
        return arr;
    }
    target->c = h;
    return target;
}

static node_t* parse_array_lit(parser_t* P) {
    token_t* at = expect(P, TK_LBRACE, "'{'");
    node_t* arr = mk(P, N_ARRAY_LIT, at);
    node_t *h = NULL, *tl = NULL;
    while (!check(P, TK_RBRACE)) {
        node_t* e = check(P, TK_LBRACE) ? parse_array_lit(P) : parse_expr(P);
        LIST_APPEND(h, tl, e);
        if (!match(P, TK_COMMA)) break;
    }
    expect(P, TK_RBRACE, "'}'");
    arr->a = h;
    return arr;
}

static node_t* parse_new(parser_t* P) {
    token_t* at = adv(P); /* new */
    if (check(P, TK_LBRACK)) { /* new[] { ... } */
        adv(P); expect(P, TK_RBRACK, "']'");
        return parse_array_lit(P);
    }
    if (check(P, TK_LPAREN) || check(P, TK_LBRACE)) { /* target typed new() */
        if (!P->target_type) perr(P, at, "target-typed 'new()' needs an explicit declared type");
        node_t* n = mk(P, N_NEW, at);
        n->type = P->target_type;
        if (match(P, TK_LPAREN)) n->a = parse_args(P, TK_RPAREN);
        if (check(P, TK_LBRACE)) parse_initializer(P, n);
        return n;
    }
    /* parse type name without array suffix */
    token_t* t = CUR;
    if (!(is_prim_kw(t->type) || t->type == TK_IDENT)) perr(P, t, "expected type after 'new'");
    adv(P);
    const char* name = t->start; uint32_t len = t->len;
    while (check(P, TK_DOT) && PEEK(1)->type == TK_IDENT) { adv(P); token_t* q = adv(P); name = q->start; len = q->len; }
    if (check(P, TK_LT)) { if (!skip_type_args(P)) perr(P, CUR, "invalid generic arguments"); }
    match(P, TK_QUESTION);
    typeref_t* ty = (typeref_t*)arena_alloc(P->A, sizeof(typeref_t));
    ty->name = name; ty->len = len; ty->prim = mcs_prim_of(name, len, &ty->conv);
    if (check(P, TK_LBRACK)) {
        adv(P);
        node_t* n = mk(P, N_NEW_ARRAY, at);
        n->type = ty;
        if (check(P, TK_RBRACK)) { /* new T[] { ... } */
            adv(P);
            while (check(P, TK_LBRACK)) { adv(P); expect(P, TK_RBRACK, "']'"); }
            node_t* lit = parse_array_lit(P);
            return lit;
        }
        if (check(P, TK_COMMA)) perr(P, CUR, "multi-dimensional arrays are not supported");
        n->a = parse_expr(P);
        expect(P, TK_RBRACK, "']'");
        while (check(P, TK_LBRACK)) { adv(P); expect(P, TK_RBRACK, "']'"); n->flag = 1; /* jagged: elements null */ }
        if (check(P, TK_LBRACE)) { node_t* lit = parse_array_lit(P); return lit; }
        return n;
    }
    node_t* n = mk(P, N_NEW, at);
    n->type = ty;
    if (match(P, TK_LPAREN)) n->a = parse_args(P, TK_RPAREN);
    else if (!check(P, TK_LBRACE)) perr(P, CUR, "expected '(' or '{' after type in 'new'");
    if (check(P, TK_LBRACE)) parse_initializer(P, n);
    return n;
}

/* split an interpolated string into parts */
static node_t* parse_interp(parser_t* P, token_t* tk) {
    node_t* res = mk(P, N_INTERP, tk);
    node_t *h = NULL, *tl = NULL;
    const char* s = tk->start; const char* e = s + tk->len;
    const char* lit = s;
    while (s < e) {
        if (*s == '{' && s + 1 < e && s[1] == '{') { s += 2; continue; }
        if (*s == '}' && s + 1 < e && s[1] == '}') { s += 2; continue; }
        if (!tk->verbatim && *s == '\\') { s += 2; continue; }
        if (*s != '{') { s++; continue; }
        if (s > lit) {
            node_t* ln = mk(P, N_STR, tk);
            ln->name = decode_str(P, lit, (size_t)(s - lit), tk->verbatim, true, &ln->len);
            LIST_APPEND(h, tl, ln);
        }
        /* find end of hole */
        const char* hs = ++s; int depth = 0; const char* colon = NULL; const char* comma = NULL;
        while (s < e) {
            char c = *s;
            if (c == '"' || c == '\'') { char q = c; s++; while (s < e && *s != q) { if (*s == '\\') s++; s++; } s++; continue; }
            if (c == '(' || c == '[' || c == '{') depth++;
            else if (c == ')' || c == ']') depth--;
            else if (c == '}') { if (depth == 0) break; depth--; }
            else if (c == ':' && depth == 0 && !colon) colon = s;
            else if (c == ',' && depth == 0 && !colon && !comma) comma = s;
            else if (c == '?' && depth == 0 && !colon) { /* allow ?: inside parens only; ?. and ?? fine */ }
            s++;
        }
        const char* he = comma ? comma : colon ? colon : s;
        node_t* part = mk(P, N_INTERP, tk);
        part->flag = 1;
        if (comma) {
            const char* ae = colon ? colon : s;
            char tmp[16]; size_t an = (size_t)(ae - comma - 1); if (an > 15) an = 15;
            memcpy(tmp, comma + 1, an); tmp[an] = 0;
            part->ival = (mcs_int_t)strtol(tmp, NULL, 10);
        }
        if (colon) { part->name = arena_strdup(P->A, colon + 1, (size_t)(s - colon - 1)); part->len = (uint32_t)(s - colon - 1); }
        /* sub-parse hole expression */
        toklist_t tl2 = {0};
        if (!mcs_lex(P->ctx, hs, (size_t)(he - hs), tk->line, &tl2)) longjmp(P->jb, 1);
        token_t* st = P->t; uint32_t sn = P->n, sp = P->pos;
        P->t = tl2.toks; P->n = tl2.count; P->pos = 0;
        part->a = parse_expr(P);
        if (TT != TK_EOF) perr(P, CUR, "invalid expression in interpolated string");
        P->t = st; P->n = sn; P->pos = sp;
        LIST_APPEND(h, tl, part);
        s++; lit = s;
    }
    if (s > lit) {
        node_t* ln = mk(P, N_STR, tk);
        ln->name = decode_str(P, lit, (size_t)(s - lit), tk->verbatim, true, &ln->len);
        LIST_APPEND(h, tl, ln);
    }
    res->a = h;
    return res;
}

static const char* prim_class_name(int t) {
    switch (t) {
    case TK_KINT: case TK_UINT: case TK_LONG: case TK_ULONG: case TK_SHORT: case TK_USHORT: case TK_BYTE: case TK_SBYTE: return "Int32";
    case TK_DOUBLE: case TK_KFLOAT: case TK_DECIMAL: return "Double";
    case TK_KSTRING: return "String";
    case TK_KCHAR: return "Char";
    case TK_BOOL: return "Boolean";
    case TK_OBJECT: return "Object";
    default: return NULL;
    }
}

static node_t* parse_primary(parser_t* P) {
    token_t* t = CUR;
    node_t* n;
    switch (t->type) {
    case TK_INT: adv(P); n = mk(P, N_INT, t); n->ival = t->v.i; return n;
#if MCS_ENABLE_FLOAT
    case TK_FLOAT: adv(P); n = mk(P, N_FLOAT, t); n->fval = t->v.f; return n;
#endif
    case TK_CHAR: adv(P); n = mk(P, N_CHAR, t); n->ival = t->v.i; return n;
    case TK_STRING: adv(P); n = mk(P, N_STR, t); n->name = decode_str(P, t->start, t->len, t->verbatim, false, &n->len); return n;
    case TK_INTERP: adv(P); return parse_interp(P, t);
    case TK_TRUE: case TK_FALSE: adv(P); n = mk(P, N_BOOL, t); n->ival = t->type == TK_TRUE; return n;
    case TK_NULL: adv(P); return mk(P, N_NULL, t);
    case TK_THIS: adv(P); return mk(P, N_THIS, t);
    case TK_BASE: adv(P); return mk(P, N_BASE, t);
    case TK_NEW: return parse_new(P);
    case TK_TYPEOF: {
        adv(P); expect(P, TK_LPAREN, "'('");
        n = mk(P, N_TYPEOF, t); n->type = parse_type(P, false);
        expect(P, TK_RPAREN, "')'");
        return n;
    }
    case TK_DEFAULT: {
        adv(P); n = mk(P, N_DEFAULT, t);
        if (match(P, TK_LPAREN)) { n->type = parse_type(P, false); expect(P, TK_RPAREN, "')'"); }
        else n->type = P->target_type;
        return n;
    }
    case TK_CHECKED: case TK_UNCHECKED:
        if (PEEK(1)->type == TK_LPAREN) { adv(P); adv(P); n = parse_expr(P); expect(P, TK_RPAREN, "')'"); return n; }
        perr(P, t, "unexpected %s in expression", tokname(t));
        return NULL;
    case TK_SIZEOF: {
        adv(P); expect(P, TK_LPAREN, "'('");
        token_t* tt = CUR;
        typeref_t* ty = parse_type(P, false);
        expect(P, TK_RPAREN, "')'");
        static const struct { const char* n; int sz; } sizes[] = {
            {"byte",1},{"sbyte",1},{"bool",1},{"short",2},{"ushort",2},{"char",2},{"int",4},{"uint",4},
            {"float",4},{"long",8},{"ulong",8},{"double",8},{"decimal",16},
            {"Byte",1},{"SByte",1},{"Boolean",1},{"Int16",2},{"UInt16",2},{"Char",2},{"Int32",4},{"UInt32",4},
            {"Single",4},{"Int64",8},{"UInt64",8},{"Double",8},{"Decimal",16},{NULL,0}};
        for (int i = 0; ty && sizes[i].n; i++)
            if (strlen(sizes[i].n) == ty->len && memcmp(sizes[i].n, ty->name, ty->len) == 0 && ty->rank == 0) return mk_int(P, tt, sizes[i].sz);
        perr(P, tt, "sizeof is only supported for built-in numeric types");
        return NULL;
    }
    case TK_DELEGATE: {
        adv(P);
        n = mk(P, N_LAMBDA, t); n->fn = new_fn(P, t->line);
        if (match(P, TK_LPAREN)) n->fn->params = parse_params(P, TK_RPAREN, &n->fn->nparams);
        n->fn->body = parse_block(P);
        return n;
    }
    case TK_LPAREN: {
        if (paren_is_lambda(P)) {
            adv(P);
            n = mk(P, N_LAMBDA, t); n->fn = new_fn(P, t->line);
            n->fn->params = parse_params(P, TK_RPAREN, &n->fn->nparams);
            expect(P, TK_ARROW, "'=>'");
            return lambda_body(P, n);
        }
        /* cast? */
        uint32_t save = P->pos;
        adv(P);
        typeref_t* ty = try_type(P);
        if (ty && check(P, TK_RPAREN)) {
            token_t* nx = PEEK(1);
            bool prim = ty->prim != PT_ANY;
            int k = nx->type;
            bool operand_start = k == TK_IDENT || k == TK_INT || k == TK_FLOAT || k == TK_STRING || k == TK_CHAR ||
                                 k == TK_INTERP || k == TK_LPAREN || k == TK_THIS || k == TK_NEW || k == TK_BASE ||
                                 k == TK_BANG || k == TK_TILDE || k == TK_TRUE || k == TK_FALSE || k == TK_NULL ||
                                 is_prim_kw(k) || k == TK_TYPEOF || k == TK_DEFAULT;
            bool sign_ok = prim && (k == TK_MINUS || k == TK_PLUS || k == TK_INC || k == TK_DEC);
            if (operand_start || sign_ok) {
                adv(P);
                n = mk(P, N_CAST, t); n->type = ty;
                n->a = parse_unary(P);
                return n;
            }
        }
        P->pos = save + 1;
        token_t* el_name = NULL;
        if (TT == TK_IDENT && PEEK(1)->type == TK_COLON) { el_name = adv(P); adv(P); }
        n = parse_expr(P);
        if (check(P, TK_COMMA) || el_name) {
            /* tuple literal (a, b, ...) / (x: a, y: b) -> __rt.Tuple(names, a, b, ...).
               Unnamed elements that are a variable or member access get its name
               (C# 7.1 tuple projection), unless that name is duplicated or reserved. */
            node_t *h = n, *tl = n; uint32_t count = 1;
            const char* en[8] = {0}; uint32_t el[8] = {0}; bool ex[8] = {0};
            if (el_name) { en[0] = el_name->start; el[0] = el_name->len; ex[0] = true; }
            for (;;) {
                node_t* e = tl;
                uint32_t i = count - 1;
                if (i < 8 && !en[i] && (e->kind == N_NAME || (e->kind == N_MEMBER && !e->flag)) && e->name && e->name[0] != '$') {
                    en[i] = e->name; el[i] = e->len;
                }
                if (!match(P, TK_COMMA)) break;
                token_t* nm = NULL;
                if (TT == TK_IDENT && PEEK(1)->type == TK_COLON) { nm = adv(P); adv(P); }
                node_t* e2 = parse_expr(P);
                tl->next = e2; tl = e2;
                if (count < 8 && nm) { en[count] = nm->start; el[count] = nm->len; ex[count] = true; }
                count++;
            }
            expect(P, TK_RPAREN, "')'");
            if (count < 2) perr(P, t, "a tuple needs at least two elements");
            if (count > 7) perr(P, t, "tuples with more than 7 elements are not supported");
            static const char* const reserved[] = { "CompareTo", "Deconstruct", "Equals", "GetHashCode", "Rest", "ToString" };
            for (uint32_t i = 0; i < count && i < 8; i++) {
                if (!en[i] || ex[i]) continue;
                bool drop = false;
                for (size_t r = 0; r < sizeof reserved / sizeof reserved[0]; r++)
                    if (strlen(reserved[r]) == el[i] && !memcmp(reserved[r], en[i], el[i])) drop = true;
                if (el[i] > 4 && !memcmp(en[i], "Item", 4)) drop = true;
                for (uint32_t j = 0; j < count && j < 8; j++)
                    if (j != i && en[j] && el[j] == el[i] && !memcmp(en[j], en[i], el[i])) drop = true;
                if (drop) en[i] = NULL;
            }
            char nb[256]; uint32_t np = 0; bool any = false;
            for (uint32_t i = 0; i < count && i < 8; i++) {
                if (i && np < sizeof nb) nb[np++] = ',';
                if (en[i]) { any = true; for (uint32_t k2 = 0; k2 < el[i] && np < sizeof nb; k2++) nb[np++] = en[i][k2]; }
            }
            node_t* names = any ? mk_str(P, t, arena_strdup(P->A, nb, np), np) : mk(P, N_NULL, t);
            names->next = h;
            return rt_call(P, t, "Tuple", names);
        }
        expect(P, TK_RPAREN, "')'");
        return n;
    }
    case TK_IDENT: {
        if (PEEK(1)->type == TK_ARROW) { /* x => ... */
            adv(P); adv(P);
            n = mk(P, N_LAMBDA, t); n->fn = new_fn(P, t->line);
            param_t* p = (param_t*)arena_alloc(P->A, sizeof(param_t));
            p->name = t->start; p->len = t->len;
            n->fn->params = p; n->fn->nparams = 1;
            return lambda_body(P, n);
        }
        if (is_word(t, "nameof") && PEEK(1)->type == TK_LPAREN) {
            adv(P); adv(P);
            token_t* last = CUR;
            int depth = 1;
            while (TT != TK_EOF) { if (TT == TK_LPAREN) depth++; if (TT == TK_RPAREN && --depth == 0) break; if (TT == TK_IDENT) last = CUR; adv(P); }
            expect(P, TK_RPAREN, "')'");
            n = mk(P, N_STR, t); n->name = last->start; n->len = last->len;
            return n;
        }
        if ((is_word(t, "async") || is_word(t, "await")) && (PEEK(1)->type == TK_IDENT || PEEK(1)->type == TK_LPAREN))
            perr(P, t, "async/await is not supported");
        adv(P);
        n = mk(P, N_NAME, t); n->name = t->start; n->len = t->len;
        /* generic method call Foo<int>(...) */
        if (check(P, TK_LT)) {
            uint32_t save = P->pos;
            if (skip_type_args(P) && (check(P, TK_LPAREN) || check(P, TK_DOT))) return n;
            P->pos = save;
        }
        return n;
    }
    default:
        if (is_prim_kw(t->type) && PEEK(1)->type == TK_DOT) {
            adv(P);
            n = mk(P, N_NAME, t);
            n->name = prim_class_name(t->type); n->len = (uint32_t)strlen(n->name);
            return n;
        }
        perr(P, t, "unexpected %s in expression", tokname(t));
        return NULL;
    }
}

static node_t* parse_postfix(parser_t* P, node_t* e) {
    for (;;) {
        token_t* t = CUR;
        if (t->type == TK_DOT && PEEK(1)->type == TK_DOT) break;   /* range operator a..b */
        if (t->type == TK_DOT || t->type == TK_QDOT) {
            adv(P);
            token_t* nm = CUR;
            if (nm->type != TK_IDENT && !(nm->type >= TK_KW_FIRST && nm->type < TK_KW_LAST)) perr(P, nm, "expected member name");
            adv(P);
            node_t* m = mk(P, N_MEMBER, nm);
            m->a = e; m->name = nm->start; m->len = nm->len; m->flag = t->type == TK_QDOT;
            if (check(P, TK_LT)) { uint32_t save = P->pos; if (!(skip_type_args(P) && check(P, TK_LPAREN))) P->pos = save; }
            e = m;
        } else if (t->type == TK_LPAREN) {
            adv(P);
            node_t* c = mk(P, N_CALL, t);
            c->a = e; c->b = parse_args(P, TK_RPAREN);
            e = c;
        } else if (t->type == TK_LBRACK || t->type == TK_QLBRACK) {
            adv(P);
            node_t* ix = mk(P, N_INDEX, t);
            ix->a = e;
            /* [^n]  [a..b]  [..b]  [a..]  [^a..^b] */
            node_t* lo = NULL; bool lo_end = false;
            if (match(P, TK_CARET)) { lo_end = true; lo = parse_binary(P, 1); }
            else if (!(check(P, TK_DOT) && PEEK(1)->type == TK_DOT)) lo = parse_expr(P);
            if (check(P, TK_DOT) && PEEK(1)->type == TK_DOT) {
                adv(P); adv(P);
                node_t* hi = NULL; bool hi_end = false;
                if (match(P, TK_CARET)) { hi_end = true; hi = parse_binary(P, 1); }
                else if (!check(P, TK_RBRACK)) hi = parse_expr(P);
                expect(P, TK_RBRACK, "']'");
                if (t->type == TK_QLBRACK) perr(P, t, "?[..] is not supported");
                if (!lo) lo = mk_int(P, t, 0);
                if (!hi) { hi = mk_int(P, t, 0); hi_end = true; }
                node_t* a1 = e; a1->next = NULL;
                node_t* lf = mk(P, N_BOOL, t); lf->ival = lo_end;
                node_t* hf = mk(P, N_BOOL, t); hf->ival = hi_end;
                a1->next = lo; lo->next = lf; lf->next = hi; hi->next = hf;
                e = rt_call(P, t, "Slice", a1);
                continue;
            }
            if (lo_end) {
                if (pure_expr(e) && t->type != TK_QLBRACK) {
                    node_t* len = rt_call(P, t, "Len", e);
                    node_t* sub = mk(P, N_BINARY, t); sub->op = TK_MINUS; sub->a = len; sub->b = lo;
                    lo = sub;
                } else {
                    expect(P, TK_RBRACK, "']'");
                    e = rt_call(P, t, "AtEnd", args2(e, lo));
                    continue;
                }
            }
            ix->b = lo;
            if (check(P, TK_COMMA)) perr(P, CUR, "multi-dimensional indexing is not supported");
            expect(P, TK_RBRACK, "']'");
            ix->flag = t->type == TK_QLBRACK;
            e = ix;
        } else if (t->type == TK_INC || t->type == TK_DEC) {
            adv(P);
            node_t* pi = mk(P, N_POSTINC, t);
            pi->a = e; pi->op = (uint8_t)t->type;
            e = pi;
        } else if (t->type == TK_BANG && (PEEK(1)->type == TK_DOT || PEEK(1)->type == TK_RPAREN || PEEK(1)->type == TK_SEMI || PEEK(1)->type == TK_COMMA)) {
            adv(P); /* null-forgiving operator */
        } else break;
    }
    return e;
}

/* in a switch arm: `Type name` (binding) => type pattern */
static bool arm_type_pattern(parser_t* P) {
    uint32_t save = P->pos;
    typeref_t* t = try_type(P);
    bool yes = t && TT == TK_IDENT && !is_word(CUR, "when") && !is_word(CUR, "or") && !is_word(CUR, "and");
    P->pos = save;
    return yes;
}

/* one pattern: `_`, relational `< x`, `not null`, type pattern, or constant */
static node_t* parse_pattern_one(parser_t* P) {
    node_t* pat;
    if (is_word(CUR, "_")) { adv(P); pat = mk(P, N_EMPTY, CUR); }
    else if (check(P, TK_LT) || check(P, TK_GT) || check(P, TK_LE) || check(P, TK_GE)) {
        token_t* op = adv(P);
        pat = mk(P, N_BINARY, op); pat->op = (uint8_t)op->type; pat->a = parse_binary(P, 9);
    } else if (is_word(CUR, "not") && PEEK(1)->type == TK_NULL) {
        adv(P); adv(P); pat = mk(P, N_BINARY, CUR); pat->op = TK_NE; pat->a = mk(P, N_NULL, CUR);
    } else if (is_prim_kw(TT) || (TT == TK_IDENT && arm_type_pattern(P))) {
        /* type pattern:  int i  /  Circle c  /  string  */
        token_t* tt = CUR;
        pat = mk(P, N_IS, tt);
        pat->type = try_type(P);
        if (!pat->type) perr(P, tt, "expected type pattern");
        if (TT == TK_IDENT && !is_word(CUR, "when") && !is_word(CUR, "or") && !is_word(CUR, "and")) {
            token_t* nm = adv(P);
            if (!(nm->len == 1 && nm->start[0] == '_')) { pat->name = nm->start; pat->len = nm->len; }
        }
    } else pat = parse_binary(P, 9);
    return pat;
}

/* pattern list: p1 or p2 or ...  where each p may be `a and b and ...`
   (switch-expression arms, case labels and `is` patterns) */
static node_t* parse_pattern_list(parser_t* P) {
    node_t *ph = NULL, *pt = NULL;
    for (;;) {
        node_t* pat = parse_pattern_one(P);
        if (is_word(CUR, "and")) {
            node_t* conj = mk(P, N_AND, CUR);
            conj->flag = 1; /* pattern conjunction: sub-patterns in a->next chain */
            node_t *ch = NULL, *ct = NULL;
            LIST_APPEND(ch, ct, pat);
            while (is_word(CUR, "and")) { adv(P); node_t* q = parse_pattern_one(P); LIST_APPEND(ch, ct, q); }
            conj->a = ch;
            pat = conj;
        }
        LIST_APPEND(ph, pt, pat);
        if (is_word(CUR, "or")) { adv(P); continue; }
        break;
    }
    return ph;
}

/* `e is <pattern list>` for the forms N_IS cannot express (relational, or, and):
   lowered to `e switch { <patterns> => true, _ => false }` */
static node_t* is_as_switch(parser_t* P, node_t* left, token_t* t, bool negate) {
    node_t* sw = mk(P, N_SWITCH_EXPR, t);
    sw->a = left;
    node_t* a1 = mk(P, N_ARM, t);
    a1->a = parse_pattern_list(P);
    a1->b = mk(P, N_BOOL, t); a1->b->ival = !negate;
    node_t* a2 = mk(P, N_ARM, t);
    a2->a = mk(P, N_EMPTY, t);
    a2->b = mk(P, N_BOOL, t); a2->b->ival = negate;
    a1->next = a2;
    sw->b = a1;
    return sw;
}

static node_t* parse_unary(parser_t* P) {
    token_t* t = CUR;
    switch (t->type) {
    case TK_MINUS: case TK_PLUS: case TK_BANG: case TK_TILDE: {
        adv(P);
        node_t* operand = parse_unary(P);
        if (t->type == TK_PLUS) return operand;
        if (t->type == TK_MINUS && operand->kind == N_INT) { operand->ival = (mcs_int_t)(0 - (mcs_uint_t)operand->ival); return operand; }
#if MCS_ENABLE_FLOAT
        if (t->type == TK_MINUS && operand->kind == N_FLOAT) { operand->fval = -operand->fval; return operand; }
#endif
        node_t* n = mk(P, N_UNARY, t); n->op = (uint8_t)t->type; n->a = operand;
        return n;
    }
    case TK_INC: case TK_DEC: {
        adv(P);
        node_t* n = mk(P, N_PREINC, t); n->op = (uint8_t)t->type; n->a = parse_unary(P);
        return n;
    }
    default: break;
    }
    node_t* e = parse_postfix(P, parse_primary(P));
    /* switch expression */
    while (check(P, TK_SWITCH)) {
        token_t* st = adv(P);
        node_t* sw = mk(P, N_SWITCH_EXPR, st);
        sw->a = e;
        expect(P, TK_LBRACE, "'{'");
        node_t *h = NULL, *tl = NULL;
        while (!check(P, TK_RBRACE)) {
            node_t* arm = mk(P, N_ARM, CUR);
            arm->a = parse_pattern_list(P);
            if (is_word(CUR, "when")) { adv(P); arm->c = parse_expr(P); }
            expect(P, TK_ARROW, "'=>'");
            if (check(P, TK_THROW)) { token_t* tt = adv(P); arm->b = mk(P, N_THROW, tt); arm->b->a = parse_expr(P); }
            else arm->b = parse_expr(P);
            LIST_APPEND(h, tl, arm);
            if (!match(P, TK_COMMA)) break;
        }
        expect(P, TK_RBRACE, "'}'");
        sw->b = h;
        e = sw;
    }
    return e;
}

/* binary precedence levels (higher binds tighter) */
static int binprec(parser_t* P, int* op) {
    token_t* t = CUR;
    *op = t->type;
    switch (t->type) {
    case TK_OROR: return 1;
    case TK_ANDAND: return 2;
    case TK_PIPE: return 3;
    case TK_CARET: return 4;
    case TK_AMP: return 5;
    case TK_EQ: case TK_NE: return 6;
    case TK_LT: case TK_LE: case TK_GE: case TK_IS: case TK_AS: return 7;
    case TK_GT: {
        token_t* n = PEEK(1);
        if (n->type == TK_GT && adjacent(t, n)) {
            token_t* n2 = PEEK(2);
            if (n2->type == TK_GE && adjacent(n, n2)) return 0; /* >>>= */
            if (n2->type == TK_GT && adjacent(n, n2)) { *op = 1001; return 8; } /* >>> */
            *op = 1000; return 8; /* >> */
        }
        if (n->type == TK_GE && adjacent(t, n)) return 0; /* >>= assignment */
        return 7;
    }
    case TK_SHL: return 8;
    case TK_PLUS: case TK_MINUS: return 9;
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 10;
    default: return 0;
    }
}

static node_t* parse_is(parser_t* P, node_t* left, token_t* t) {
    node_t* n = mk(P, N_IS, t);
    n->a = left;
    if (is_word(CUR, "not")) { adv(P); n->flag = 1; }
    uint32_t save = P->pos;
    if (check(P, TK_LT) || check(P, TK_GT) || check(P, TK_LE) || check(P, TK_GE))
        return is_as_switch(P, left, t, n->flag);
    if (check(P, TK_NULL) && !is_word(PEEK(1), "or") && !is_word(PEEK(1), "and")) { adv(P); n->op = 1; return n; } /* is null */
    typeref_t* ty = NULL;
    if (TT == TK_IDENT || is_prim_kw(TT)) ty = try_type(P);
    if (ty) {
        n->type = ty;
        if (TT == TK_IDENT && !is_word(CUR, "and") && !is_word(CUR, "or")) { token_t* nm = adv(P); n->name = nm->start; n->len = nm->len; }
        if (is_word(CUR, "or") || is_word(CUR, "and")) { P->pos = save; return is_as_switch(P, left, t, n->flag); }
        return n;
    }
    n->op = 2; /* constant pattern */
    n->b = parse_binary(P, 8);
    if (is_word(CUR, "or") || is_word(CUR, "and")) { P->pos = save; return is_as_switch(P, left, t, n->flag); }
    return n;
}

static node_t* parse_binary(parser_t* P, int minprec) {
    node_t* left = parse_unary(P);
    for (;;) {
        int op;
        int prec = binprec(P, &op);
        if (prec == 0 || prec < minprec) break;
        token_t* t = adv(P);
        if (op == 1000) adv(P);
        if (op == 1001) { adv(P); adv(P); }
        if (op == TK_IS) { left = parse_is(P, left, t); continue; }
        if (op == TK_AS) { node_t* n = mk(P, N_AS, t); n->a = left; n->type = parse_type(P, false); left = n; continue; }
        node_t* right = parse_binary(P, prec + 1);
        node_t* n;
        if (op == TK_ANDAND) n = mk(P, N_AND, t);
        else if (op == TK_OROR) n = mk(P, N_OR, t);
        else { n = mk(P, N_BINARY, t); n->op = (uint8_t)(op == 1000 ? 250 : op == 1001 ? 251 : op); }
        n->a = left; n->b = right;
        left = n;
    }
    return left;
}

static node_t* parse_coalesce(parser_t* P) {
    node_t* left = parse_binary(P, 1);
    if (check(P, TK_QQ)) {
        token_t* t = adv(P);
        node_t* n = mk(P, N_COALESCE, t);
        n->a = left;
        if (check(P, TK_THROW)) { token_t* th = adv(P); n->b = mk(P, N_THROW, th); n->b->a = parse_expr(P); }
        else n->b = parse_coalesce(P);
        return n;
    }
    return left;
}

static node_t* parse_ternary(parser_t* P) {
    node_t* c = parse_coalesce(P);
    if (check(P, TK_QUESTION)) {
        token_t* t = adv(P);
        node_t* n = mk(P, N_COND, t);
        n->a = c;
        n->b = parse_expr(P);
        expect(P, TK_COLON, "':'");
        n->c = parse_expr(P);
        return n;
    }
    return c;
}

static int assign_op(parser_t* P, int* consumed) {
    token_t* t = CUR;
    *consumed = 1;
    switch (t->type) {
    case TK_ASSIGN: return TK_ASSIGN;
    case TK_PLUS_ASSIGN: return TK_PLUS; case TK_MINUS_ASSIGN: return TK_MINUS;
    case TK_STAR_ASSIGN: return TK_STAR; case TK_SLASH_ASSIGN: return TK_SLASH;
    case TK_PERCENT_ASSIGN: return TK_PERCENT; case TK_AMP_ASSIGN: return TK_AMP;
    case TK_PIPE_ASSIGN: return TK_PIPE; case TK_CARET_ASSIGN: return TK_CARET;
    case TK_SHL_ASSIGN: return TK_SHL; case TK_QQ_ASSIGN: return TK_QQ;
    case TK_GT: {
        token_t* n = PEEK(1);
        if (n->type == TK_GE && adjacent(t, n)) { *consumed = 2; return 250; }
        if (n->type == TK_GT && adjacent(t, n) && PEEK(2)->type == TK_GE && adjacent(n, PEEK(2))) { *consumed = 3; return 251; }
        return 0;
    }
    default: return 0;
    }
}

static node_t* parse_expr(parser_t* P) {
    if (check(P, TK_THROW)) { token_t* t = adv(P); node_t* n = mk(P, N_THROW, t); n->a = parse_expr(P); return n; }
    node_t* left = parse_ternary(P);
    int consumed;
    int op = assign_op(P, &consumed);
    if (op) {
        token_t* t = CUR;
        while (consumed--) adv(P);
        int k = left->kind;
        if (k != N_NAME && k != N_MEMBER && k != N_INDEX) perr(P, t, "invalid assignment target");
        node_t* n = mk(P, N_ASSIGN, t);
        n->op = (uint8_t)op; n->a = left;
        n->b = parse_expr(P);
        if (op == TK_ASSIGN && (k == N_NAME || k == N_MEMBER) && left->name) {
            typeref_t* tt = k == N_NAME ? tvar_find(P, left->name, left->len) : NULL;
            if (!tt) tt = tmem_find(P, left->name, left->len);
            n->b = tuple_relabel(P, t, tt, n->b);
        }
        return n;
    }
    return left;
}

/* ------------------------------------------------------------ statements */
static param_t* parse_params(parser_t* P, int close, uint8_t* count) {
    param_t *h = NULL, *tl = NULL;
    *count = 0;
    if (match(P, close)) return NULL;
    for (;;) {
        while (check(P, TK_LBRACK)) { while (!check(P, TK_RBRACK) && !check(P, TK_EOF)) adv(P); adv(P); }
        param_t* p = (param_t*)arena_alloc(P->A, sizeof(param_t));
        if (match(P, TK_OUT)) p->ref_kind = 1;
        else if (match(P, TK_REF)) p->ref_kind = 2;
        else match(P, TK_IN);
        if (check(P, TK_THIS)) adv(P); /* extension method: treat as normal */
        if (match(P, TK_PARAMS)) p->is_params = 1;
        if (TT == TK_IDENT && (PEEK(1)->type == TK_COMMA || PEEK(1)->type == close)) {
            /* untyped lambda parameter */
        } else {
            p->type = parse_type(P, false);
        }
        token_t* nm = expect(P, TK_IDENT, "parameter name");
        p->name = nm->start; p->len = nm->len;
        if (match(P, TK_ASSIGN)) {
            typeref_t* save = P->target_type; P->target_type = p->type;
            p->def = parse_expr(P);
            P->target_type = save;
        }
        if (!h) h = p; else tl->next = p;
        tl = p;
        (*count)++;
        if (match(P, TK_COMMA)) continue;
        expect(P, close, "')'");
        break;
    }
    return h;
}

static void skip_where(parser_t* P) {
    while (is_word(CUR, "where")) {
        while (!check(P, TK_LBRACE) && !check(P, TK_ARROW) && !check(P, TK_SEMI) && !check(P, TK_EOF)) adv(P);
    }
}

static void parse_func_body(parser_t* P, funcdecl_t* f) {
    skip_where(P);
    typeref_t* sr = P->cur_ret; P->cur_ret = f->ret;
    int stv = P->ntvars;
    for (param_t* p = f->params; p; p = p->next) tvar_add(P, p->name, p->len, p->type);
    token_t* at = CUR;
    if (match(P, TK_ARROW)) { f->body = tuple_relabel(P, at, f->ret, parse_expr(P)); f->expr_body = 1; expect(P, TK_SEMI, "';'"); }
    else if (check(P, TK_LBRACE)) f->body = parse_block(P);
    else { expect(P, TK_SEMI, "'{' or ';'"); f->body = NULL; }
    P->cur_ret = sr;
    P->ntvars = stv;
    /* named tuple parameters: p = __rt.Names(p, "x,y") on entry */
    node_t *h = NULL, *tl = NULL;
    for (param_t* p = f->params; p; p = p->next) {
        if (!p->type || !p->type->tnames || p->ref_kind) continue;
        node_t* as = mk(P, N_ASSIGN, at); as->op = TK_ASSIGN;
        node_t* nm = mk(P, N_NAME, at); nm->name = p->name; nm->len = p->len;
        node_t* nm2 = mk(P, N_NAME, at); nm2->name = p->name; nm2->len = p->len;
        as->a = nm; as->b = tuple_relabel(P, at, p->type, nm2);
        node_t* es = mk(P, N_EXPR_STMT, at); es->a = as;
        LIST_APPEND(h, tl, es);
    }
    if (h && f->body) {
        node_t* body = f->body;
        if (f->expr_body) {
            node_t* r = mk(P, N_RETURN, at); r->a = body;
            body = mk(P, N_BLOCK, at); body->a = r; f->expr_body = 0;
        }
        tl->next = body->a; body->a = h; f->body = body;
    }
}

static node_t* parse_block(parser_t* P) {
    token_t* t = expect(P, TK_LBRACE, "'{'");
    node_t* b = mk(P, N_BLOCK, t);
    node_t *h = NULL, *tl = NULL;
    while (!check(P, TK_RBRACE)) {
        if (check(P, TK_EOF)) perr(P, CUR, "expected '}'");
        node_t* s = parse_stmt(P);
        if (s) LIST_APPEND(h, tl, s);
    }
    adv(P);
    b->a = h;
    return b;
}

/* declaration lookahead: Type Ident ( = | ; | , | ( | in ) */
static typeref_t* decl_type_ahead(parser_t* P, bool for_foreach) {
    if (!(TT == TK_IDENT || is_prim_kw(TT) || TT == TK_LPAREN)) return NULL;
    uint32_t save = P->pos;
    typeref_t* ty = parse_type(P, true);
    if (ty && TT == TK_IDENT) {
        int k = PEEK(1)->type;
        if (k == TK_ASSIGN || k == TK_SEMI || k == TK_COMMA || (k == TK_IN && for_foreach) || k == TK_LPAREN || k == TK_LT || (k == TK_RPAREN && for_foreach))
            return ty;
    }
    P->pos = save;
    return NULL;
}

static node_t* parse_var_decls(parser_t* P, typeref_t* ty, bool need_semi) {
    node_t *h = NULL, *tl = NULL;
    for (;;) {
        token_t* nm = expect(P, TK_IDENT, "variable name");
        node_t* v = mk(P, N_VAR, nm);
        v->name = nm->start; v->len = nm->len; v->type = ty;
        tvar_add(P, nm->start, nm->len, ty);
        if (match(P, TK_ASSIGN)) {
            typeref_t* save = P->target_type; P->target_type = ty->is_var ? NULL : ty;
            token_t* at = CUR;
            v->a = check(P, TK_LBRACE) ? parse_array_lit(P) : tuple_relabel(P, at, ty, parse_expr(P));
            P->target_type = save;
        } else if (ty->is_var) perr(P, nm, "implicitly-typed variables must be initialized");
        LIST_APPEND(h, tl, v);
        if (match(P, TK_COMMA)) continue;
        break;
    }
    if (need_semi) expect(P, TK_SEMI, "';'");
    if (h->next) { node_t* b = mk(P, N_BLOCK, CUR); b->flag = 1; b->a = h; return b; }
    return h;
}


/* ------------------------------------------------------------ deconstruction */
typedef struct { node_t* target; typeref_t* decl; const char* name; uint32_t len; uint8_t discard; token_t* at; } decon_el_t;
#define DECON_MAX 7

static typeref_t* var_type(parser_t* P) {
    typeref_t* vt = (typeref_t*)arena_alloc(P->A, sizeof(typeref_t));
    vt->name = "var"; vt->len = 3; vt->prim = PT_ANY; vt->conv = 0xff; vt->is_var = 1;
    return vt;
}
static bool is_rt_call(node_t* n, const char* fn) {
    return n && n->kind == N_CALL && n->a && n->a->kind == N_MEMBER && n->a->a && n->a->a->kind == N_NAME &&
           n->a->a->len == 4 && memcmp(n->a->a->name, "__rt", 4) == 0 && n->a->len == strlen(fn) && memcmp(n->a->name, fn, n->a->len) == 0;
}
/* at '(' : `( ... , ... ) =`  (a deconstructing declaration/assignment) */
static bool paren_is_decon(parser_t* P, uint32_t i) {
    int depth = 0; bool comma = false;
    for (; i < P->n; i++) {
        int t = P->t[i].type;
        if (t == TK_LPAREN) depth++;
        else if (t == TK_RPAREN) { if (--depth == 0) break; }
        else if (t == TK_COMMA && depth == 1) comma = true;
        else if (t == TK_SEMI || t == TK_LBRACE || t == TK_EOF || t == TK_ASSIGN || t == TK_ARROW) return false;
    }
    return comma && i + 1 < P->n && (P->t[i + 1].type == TK_ASSIGN || P->t[i + 1].type == TK_IN);
}
/* ( target, var x, int y, _ )   - all_var: `var (a, b)` form */
static int parse_decon_targets(parser_t* P, decon_el_t* els, bool all_var) {
    int n = 0;
    expect(P, TK_LPAREN, "'('");
    for (;;) {
        if (n >= DECON_MAX) perr(P, CUR, "too many deconstruction targets");
        decon_el_t* e = &els[n++];
        memset(e, 0, sizeof *e);
        e->at = CUR;
        if (is_word(CUR, "_") && (PEEK(1)->type == TK_COMMA || PEEK(1)->type == TK_RPAREN)) { adv(P); e->discard = 1; }
        else if (check(P, TK_LPAREN)) perr(P, CUR, "nested deconstruction is not supported");
        else if (all_var) { token_t* nm = expect(P, TK_IDENT, "variable name"); e->decl = var_type(P); e->name = nm->start; e->len = nm->len; }
        else {
            uint32_t save = P->pos;
            typeref_t* ty = try_type(P);
            if (ty && TT == TK_IDENT && (PEEK(1)->type == TK_COMMA || PEEK(1)->type == TK_RPAREN)) {
                token_t* nm = adv(P); e->decl = ty; e->name = nm->start; e->len = nm->len;
            } else {
                P->pos = save;
                e->target = parse_ternary(P);
                int k = e->target ? e->target->kind : 0;
                if (k != N_NAME && k != N_MEMBER && k != N_INDEX) perr(P, e->at, "invalid deconstruction target");
            }
        }
        if (match(P, TK_COMMA)) continue;
        expect(P, TK_RPAREN, "')'");
        break;
    }
    if (n < 2) perr(P, CUR, "deconstruction needs at least two elements");
    return n;
}
static node_t* decon_store(parser_t* P, decon_el_t* e, node_t* val) {
    if (e->decl) { node_t* v = mk(P, N_VAR, e->at); v->name = e->name; v->len = e->len; v->type = e->decl; v->a = val; return v; }
    node_t* as = mk(P, N_ASSIGN, e->at); as->op = TK_ASSIGN; as->a = e->target; as->b = val;
    node_t* es = mk(P, N_EXPR_STMT, e->at); es->a = as; return es;
}
static node_t* mk_tmp_var(parser_t* P, token_t* at, const char* nm, node_t* init) {
    node_t* v = mk(P, N_VAR, at); v->name = nm; v->len = (uint32_t)strlen(nm); v->type = var_type(P); v->a = init; return v;
}
/* desugar into a scope-less block:
 *   tuple literal rhs:  $t0 = e0; $t1 = e1; a = $t0; b = $t1;      (no allocation; swaps work)
 *   otherwise:          $t = rhs; a = __rt.Item($t, 0); b = __rt.Item($t, 1);                   */
static node_t* build_decon(parser_t* P, token_t* at, decon_el_t* els, int n, node_t* rhs) {
    node_t *h = NULL, *tl = NULL;
    node_t* b = mk(P, N_BLOCK, at); b->flag = 1;
    if (is_rt_call(rhs, "Tuple")) {
        node_t* el = rhs->b ? rhs->b->next : NULL;  /* skip names */
        int cnt = 0; for (node_t* x = el; x; x = x->next) cnt++;
        if (cnt != n) perr(P, at, "cannot deconstruct a tuple of %d elements into %d variables", cnt, n);
        const char* tn[DECON_MAX];
        int i = 0;
        for (node_t* x = el; x; i++) { node_t* nx = x->next; x->next = NULL; tn[i] = tmp_name(P); node_t* v = mk_tmp_var(P, at, tn[i], x); LIST_APPEND(h, tl, v); x = nx; }
        for (i = 0; i < n; i++) if (!els[i].discard) { node_t* s2 = decon_store(P, &els[i], mk_name(P, at, tn[i])); LIST_APPEND(h, tl, s2); }
    } else {
        const char* tn = tmp_name(P);
        node_t* v = mk_tmp_var(P, at, tn, rhs); LIST_APPEND(h, tl, v);
        for (int i = 0; i < n; i++) if (!els[i].discard) {
            node_t* val = rt_call(P, at, "Item", args2(mk_name(P, at, tn), mk_int(P, at, i)));
            node_t* s2 = decon_store(P, &els[i], val); LIST_APPEND(h, tl, s2);
        }
    }
    b->a = h;
    return b;
}

static node_t* parse_local_func(parser_t* P, typeref_t* ret) {
    token_t* nm = adv(P);
    node_t* n = mk(P, N_LOCAL_FUNC, nm);
    n->fn = new_fn(P, nm->line);
    n->fn->name = nm->start; n->fn->len = nm->len; n->fn->ret = ret;
    n->name = nm->start; n->len = nm->len;
    if (check(P, TK_LT)) skip_type_args(P);
    expect(P, TK_LPAREN, "'('");
    n->fn->params = parse_params(P, TK_RPAREN, &n->fn->nparams);
    parse_func_body(P, n->fn);
    if (!n->fn->body) perr(P, nm, "local function needs a body");
    return n;
}

/* if/else/loop bodies: C# rejects declarations here (CS1023) */
static node_t* parse_embedded(parser_t* P) {
    token_t* t = CUR;
    node_t* n = parse_stmt(P);
    if (n && (n->kind == N_VAR || n->kind == N_LOCAL_FUNC)) perr(P, t, "embedded statement cannot be a declaration");
    return n;
}

static node_t* parse_stmt(parser_t* P) {
    token_t* t = CUR;
    node_t* n;
    switch (t->type) {
    case TK_LBRACE: return parse_block(P);
    case TK_SEMI: adv(P); return mk(P, N_EMPTY, t);
    case TK_IF:
        adv(P); expect(P, TK_LPAREN, "'('");
        n = mk(P, N_IF, t); n->a = parse_expr(P); expect(P, TK_RPAREN, "')'");
        n->b = parse_embedded(P);
        if (match(P, TK_ELSE)) n->c = parse_embedded(P);
        return n;
    case TK_WHILE:
        adv(P); expect(P, TK_LPAREN, "'('");
        n = mk(P, N_WHILE, t); n->a = parse_expr(P); expect(P, TK_RPAREN, "')'");
        n->b = parse_embedded(P);
        return n;
    case TK_DO:
        adv(P); n = mk(P, N_DO, t); n->b = parse_embedded(P);
        expect(P, TK_WHILE, "'while'"); expect(P, TK_LPAREN, "'('");
        n->a = parse_expr(P); expect(P, TK_RPAREN, "')'"); expect(P, TK_SEMI, "';'");
        return n;
    case TK_FOR: {
        adv(P); expect(P, TK_LPAREN, "'('");
        n = mk(P, N_FOR, t);
        if (!check(P, TK_SEMI)) {
            typeref_t* ty = decl_type_ahead(P, false);
            if (ty) n->a = parse_var_decls(P, ty, false);
            else {
                node_t *h = NULL, *tl = NULL;
                for (;;) { node_t* e = mk(P, N_EXPR_STMT, CUR); e->a = parse_expr(P); LIST_APPEND(h, tl, e); if (!match(P, TK_COMMA)) break; }
                node_t* b = mk(P, N_BLOCK, t); b->flag = 1; b->a = h; n->a = b;
            }
        }
        expect(P, TK_SEMI, "';'");
        if (!check(P, TK_SEMI)) n->b = parse_expr(P);
        expect(P, TK_SEMI, "';'");
        if (!check(P, TK_RPAREN)) {
            node_t *h = NULL, *tl = NULL;
            for (;;) { node_t* e = mk(P, N_EXPR_STMT, CUR); e->a = parse_expr(P); LIST_APPEND(h, tl, e); if (!match(P, TK_COMMA)) break; }
            node_t* b = mk(P, N_BLOCK, t); b->flag = 1; b->a = h; n->c = b;
        }
        expect(P, TK_RPAREN, "')'");
        n->d = parse_embedded(P);
        return n;
    }
    case TK_FOREACH: {
        adv(P); expect(P, TK_LPAREN, "'('");
        n = mk(P, N_FOREACH, t);
        decon_el_t els[DECON_MAX]; int nel = 0;
        if (is_word(CUR, "var") && PEEK(1)->type == TK_LPAREN) { adv(P); nel = parse_decon_targets(P, els, true); }
        else if (check(P, TK_LPAREN) && paren_is_decon(P, P->pos)) nel = parse_decon_targets(P, els, false);
        if (nel) {
            /* foreach (var (k, v) in xs) body  ->  foreach (var $t in xs) { var (k, v) = $t; body } */
            n->type = var_type(P);
            n->name = tmp_name(P); n->len = (uint32_t)strlen(n->name);
            for (int i = 0; i < nel; i++) if (!els[i].decl && !els[i].discard) perr(P, els[i].at, "foreach deconstruction must declare variables");
        } else {
            n->type = parse_type(P, false);
            token_t* nm = expect(P, TK_IDENT, "loop variable");
            n->name = nm->start; n->len = nm->len;
        }
        expect(P, TK_IN, "'in'");
        n->a = parse_expr(P);
        expect(P, TK_RPAREN, "')'");
        n->b = parse_embedded(P);
        if (nel) {
            node_t* d = build_decon(P, t, els, nel, mk_name(P, t, n->name));
            node_t* blk = mk(P, N_BLOCK, t);
            d->next = n->b; n->b->next = NULL; blk->a = d;
            n->b = blk;
        }
        return n;
    }
    case TK_BREAK: adv(P); expect(P, TK_SEMI, "';'"); return mk(P, N_BREAK, t);
    case TK_CONTINUE: adv(P); expect(P, TK_SEMI, "';'"); return mk(P, N_CONTINUE, t);
    case TK_RETURN:
        adv(P); n = mk(P, N_RETURN, t);
        if (!check(P, TK_SEMI)) n->a = tuple_relabel(P, t, P->cur_ret, parse_expr(P));
        expect(P, TK_SEMI, "';'");
        return n;
    case TK_THROW:
        adv(P); n = mk(P, N_THROW, t);
        if (!check(P, TK_SEMI)) n->a = parse_expr(P);
        expect(P, TK_SEMI, "';'");
        return n;
    case TK_TRY: {
        adv(P); n = mk(P, N_TRY, t);
        n->a = parse_block(P);
        node_t *h = NULL, *tl = NULL;
        while (check(P, TK_CATCH)) {
            token_t* ct = adv(P);
            node_t* c = mk(P, N_CATCH, ct);
            if (match(P, TK_LPAREN)) {
                c->type = parse_type(P, false);
                if (TT == TK_IDENT) { token_t* nm = adv(P); c->name = nm->start; c->len = nm->len; }
                expect(P, TK_RPAREN, "')'");
            }
            if (is_word(CUR, "when")) { adv(P); expect(P, TK_LPAREN, "'('"); c->c = parse_expr(P); expect(P, TK_RPAREN, "')'"); }
            c->a = parse_block(P);
            LIST_APPEND(h, tl, c);
        }
        n->b = h;
        if (match(P, TK_FINALLY)) n->c = parse_block(P);
        if (!n->b && !n->c) perr(P, CUR, "try needs a catch or finally");
        return n;
    }
    case TK_SWITCH: {
        adv(P); expect(P, TK_LPAREN, "'('");
        n = mk(P, N_SWITCH, t);
        n->a = parse_expr(P);
        expect(P, TK_RPAREN, "')'");
        expect(P, TK_LBRACE, "'{'");
        node_t *h = NULL, *tl = NULL;
        while (!check(P, TK_RBRACE)) {
            node_t* sec = mk(P, N_CASE, CUR);
            node_t *lh = NULL, *lt = NULL;
            while (check(P, TK_CASE) || check(P, TK_DEFAULT)) {
                if (match(P, TK_DEFAULT)) { sec->flag = 1; expect(P, TK_COLON, "':'"); continue; }
                adv(P);
                node_t* lab = mk(P, N_ARM, CUR);
                lab->a = parse_pattern_list(P);
                if (is_word(CUR, "when")) { adv(P); lab->c = parse_expr(P); }
                expect(P, TK_COLON, "':'");
                LIST_APPEND(lh, lt, lab);
            }
            if (!lh && !sec->flag) perr(P, CUR, "expected 'case' or 'default'");
            sec->a = lh;
            node_t *sh = NULL, *st = NULL;
            while (!check(P, TK_CASE) && !check(P, TK_DEFAULT) && !check(P, TK_RBRACE)) {
                node_t* s = parse_stmt(P);
                LIST_APPEND(sh, st, s);
            }
            sec->b = sh;
            LIST_APPEND(h, tl, sec);
        }
        adv(P);
        n->b = h;
        return n;
    }
    case TK_CONST: {
        adv(P);
        typeref_t* ty = parse_type(P, false);
        return parse_var_decls(P, ty, true);
    }
    case TK_USING: {
        /* using (decl|expr) stmt   and   using var x = e; (rest of block)
         * desugar:  { decl; try { body } finally { x?.Dispose(); } } */
        bool paren = PEEK(1)->type == TK_LPAREN;
        adv(P); if (paren) adv(P);
        node_t* b = mk(P, N_BLOCK, t);
        typeref_t* ty = decl_type_ahead(P, false);
        node_t* d;
        if (ty) d = parse_var_decls(P, ty, !paren);
        else {
            if (!paren) perr(P, CUR, "expected declaration after 'using'");
            typeref_t* vt = (typeref_t*)arena_alloc(P->A, sizeof(typeref_t));
            vt->name = "var"; vt->len = 3; vt->prim = PT_ANY; vt->conv = 0xff; vt->is_var = 1;
            d = mk(P, N_VAR, CUR); d->name = "$using"; d->len = 6; d->type = vt; d->a = parse_expr(P);
        }
        node_t* body;
        if (paren) { expect(P, TK_RPAREN, "')'"); body = parse_embedded(P); }
        else {
            body = mk(P, N_BLOCK, CUR);
            node_t *h = NULL, *tl = NULL;
            while (!check(P, TK_RBRACE) && !check(P, TK_EOF) && !check(P, TK_CASE)) {
                node_t* s = parse_stmt(P);
                if (s) LIST_APPEND(h, tl, s);
            }
            body->a = h;
        }
        /* finally: dispose in reverse declaration order */
        node_t* fin = mk(P, N_BLOCK, t);
        for (node_t* v = d; v; v = v->next) {
            node_t* nm = mk(P, N_NAME, t); nm->name = v->name; nm->len = v->len;
            node_t* m = mk(P, N_MEMBER, t); m->a = nm; m->name = "Dispose"; m->len = 7; m->flag = 1;
            node_t* c = mk(P, N_CALL, t); c->a = m;
            node_t* es = mk(P, N_EXPR_STMT, t); es->a = c;
            es->next = fin->a; fin->a = es;
        }
        node_t* tr = mk(P, N_TRY, t); tr->a = body; tr->c = fin;
        node_t* last = d; while (last->next) last = last->next;
        last->next = tr;
        b->a = d;
        return b;
    }
    case TK_LOCK:
        adv(P); expect(P, TK_LPAREN, "'('"); parse_expr(P); expect(P, TK_RPAREN, "')'");
        return parse_embedded(P);
    case TK_CHECKED: case TK_UNCHECKED:
        if (PEEK(1)->type == TK_LBRACE) { adv(P); return parse_block(P); }
        break;
    case TK_GOTO: perr(P, t, "goto is not supported");
    default: break;
    }
    if (is_word(t, "yield")) perr(P, t, "iterators (yield) are not supported");
    if (check(P, TK_STATIC) && (PEEK(1)->type == TK_IDENT || is_prim_kw(PEEK(1)->type) || PEEK(1)->type == TK_LPAREN)) adv(P); /* static local function */
    {   /* var (a, b) = e;   (int a, var b) = e;   (x, y) = (y, x); */
        decon_el_t els[DECON_MAX]; int nel = 0;
        if (is_word(t, "var") && PEEK(1)->type == TK_LPAREN && paren_is_decon(P, P->pos + 1)) { adv(P); nel = parse_decon_targets(P, els, true); }
        else if (t->type == TK_LPAREN && paren_is_decon(P, P->pos)) nel = parse_decon_targets(P, els, false);
        if (nel) {
            token_t* eq = expect(P, TK_ASSIGN, "'='");
            node_t* rhs = parse_expr(P);
            expect(P, TK_SEMI, "';'");
            return build_decon(P, eq, els, nel, rhs);
        }
    }
    typeref_t* ty = decl_type_ahead(P, false);
    if (ty) {
        if (PEEK(1)->type == TK_LPAREN || PEEK(1)->type == TK_LT) return parse_local_func(P, ty);
        return parse_var_decls(P, ty, true);
    }
    n = mk(P, N_EXPR_STMT, t);
    n->a = parse_expr(P);
    expect(P, TK_SEMI, "';'");
    return n;
}

/* ------------------------------------------------------------ declarations */
static uint8_t parse_modifiers(parser_t* P) {
    uint8_t m = 0;
    for (;;) {
        switch (TT) {
        case TK_PUBLIC: case TK_PRIVATE: case TK_PROTECTED: case TK_INTERNAL: case TK_SEALED:
        case TK_READONLY: case TK_EXTERN: case TK_UNSAFE: case TK_VOLATILE: adv(P); continue;
        case TK_STATIC: m |= MOD_STATIC; adv(P); continue;
        case TK_ABSTRACT: m |= MOD_ABSTRACT; adv(P); continue;
        case TK_VIRTUAL: m |= MOD_VIRTUAL; adv(P); continue;
        case TK_OVERRIDE: m |= MOD_OVERRIDE; adv(P); continue;
        case TK_NEW: if (PEEK(1)->type != TK_LPAREN) { adv(P); continue; } return m;
        case TK_IDENT:
            if ((is_word(CUR, "partial") || is_word(CUR, "async") || is_word(CUR, "required")) &&
                (PEEK(1)->type == TK_IDENT || PEEK(1)->type >= TK_KW_FIRST)) { adv(P); continue; }
            return m;
        default: return m;
        }
    }
}

static void skip_attributes(parser_t* P) {
    while (check(P, TK_LBRACK)) {
        int depth = 0;
        do { if (TT == TK_LBRACK) depth++; else if (TT == TK_RBRACK) depth--; adv(P); } while (depth > 0 && !check(P, TK_EOF));
    }
}

static const char* operator_name(int tk, int nparams) {
    switch (tk) {
    case TK_PLUS: return nparams == 1 ? "op_UnaryPlus" : "op_Addition";
    case TK_MINUS: return nparams == 1 ? "op_UnaryNegation" : "op_Subtraction";
    case TK_STAR: return "op_Multiply"; case TK_SLASH: return "op_Division"; case TK_PERCENT: return "op_Modulus";
    case TK_EQ: return "op_Equality"; case TK_NE: return "op_Inequality";
    case TK_LT: return "op_LessThan"; case TK_GT: return "op_GreaterThan";
    case TK_LE: return "op_LessThanOrEqual"; case TK_GE: return "op_GreaterThanOrEqual";
    case TK_AMP: return "op_BitwiseAnd"; case TK_PIPE: return "op_BitwiseOr"; case TK_CARET: return "op_ExclusiveOr";
    case TK_BANG: return "op_LogicalNot"; case TK_TILDE: return "op_OnesComplement";
    case TK_SHL: return "op_LeftShift";
    default: return NULL;
    }
}

static void parse_type_decl(parser_t* P, uint8_t mods);

static funcdecl_t* parse_accessor_body(parser_t* P, uint32_t line) {
    funcdecl_t* f = new_fn(P, line);
    if (match(P, TK_ARROW)) { f->body = parse_expr(P); f->expr_body = 1; expect(P, TK_SEMI, "';'"); }
    else if (check(P, TK_LBRACE)) f->body = parse_block(P);
    else { expect(P, TK_SEMI, "';'"); return NULL; } /* auto accessor */
    return f;
}

static void parse_property_body(parser_t* P, member_t* m) {
    expect(P, TK_LBRACE, "'{'");
    bool any_auto = false, any = false;
    while (!check(P, TK_RBRACE)) {
        skip_attributes(P);
        parse_modifiers(P);
        token_t* acc = CUR;
        if (is_word(acc, "get")) {
            adv(P);
            typeref_t* sr = P->cur_ret; P->cur_ret = m->type;   /* tuple names on `return (a, b);` */
            token_t* gat = CUR;
            m->getter = parse_accessor_body(P, acc->line);
            if (m->getter && m->getter->expr_body) m->getter->body = tuple_relabel(P, gat, m->type, m->getter->body);
            P->cur_ret = sr;
            if (!m->getter) any_auto = true;
            any = true;
        }
        else if (is_word(acc, "set") || is_word(acc, "init")) {
            adv(P); m->setter = parse_accessor_body(P, acc->line); if (!m->setter) any_auto = true; any = true;
            if (m->setter) { /* implicit `value` parameter */
                param_t* p = (param_t*)arena_alloc(P->A, sizeof(param_t));
                p->name = "value"; p->len = 5; p->type = m->type;
                m->setter->params = p; m->setter->nparams = 1;
            }
        } else perr(P, acc, "expected 'get' or 'set'");
    }
    adv(P);
    if (!any) perr(P, CUR, "property needs an accessor");
    m->auto_prop = any_auto;
    if (match(P, TK_ASSIGN)) {
        typeref_t* save = P->target_type; P->target_type = m->type;
        token_t* iat = CUR;
        m->init = check(P, TK_LBRACE) ? parse_array_lit(P) : tuple_relabel(P, iat, m->type, parse_expr(P));
        P->target_type = save;
        expect(P, TK_SEMI, "';'");
    }
}

static member_t* new_member(parser_t* P, int kind, uint8_t mods, token_t* at) {
    member_t* m = (member_t*)arena_alloc(P->A, sizeof(member_t));
    m->kind = (uint8_t)kind; m->mods = mods; m->line = at->line;
    return m;
}

static void parse_class_body(parser_t* P, classdecl_t* c) {
    expect(P, TK_LBRACE, "'{'");
    member_t *h = NULL, *tl = NULL;
#define ADDM(m) do { if (!h) h = (m); else tl->next = (m); tl = (m); } while (0)
    while (!check(P, TK_RBRACE)) {
        if (check(P, TK_EOF)) perr(P, CUR, "expected '}'");
        if (match(P, TK_SEMI)) continue;
        skip_attributes(P);
        token_t* start = CUR;
        uint8_t mods = parse_modifiers(P);
        if (c->kind == C_INTERFACE) mods |= MOD_ABSTRACT;
        if (check(P, TK_CLASS) || check(P, TK_STRUCT) || check(P, TK_INTERFACE) || check(P, TK_ENUM) || is_word(CUR, "record")) {
            const char* sc = P->cur_class; uint32_t sl = P->cur_class_len;
            parse_type_decl(P, mods);
            P->cur_class = sc; P->cur_class_len = sl;
            continue;
        }
        if (check(P, TK_CONST)) {
            adv(P);
            typeref_t* ty = parse_type(P, false);
            for (;;) {
                token_t* nm = expect(P, TK_IDENT, "constant name");
                member_t* m = new_member(P, M_CONST, mods | MOD_STATIC | MOD_CONST, nm);
                m->name = nm->start; m->len = nm->len; m->type = ty;
                expect(P, TK_ASSIGN, "'='");
                m->init = parse_expr(P);
                ADDM(m);
                if (!match(P, TK_COMMA)) break;
            }
            expect(P, TK_SEMI, "';'");
            continue;
        }
        if (check(P, TK_EVENT)) adv(P);
        if (check(P, TK_TILDE)) { /* destructor: ignored */
            adv(P); adv(P); expect(P, TK_LPAREN, "'('"); expect(P, TK_RPAREN, "')'");
            funcdecl_t tmp; memset(&tmp, 0, sizeof tmp); parse_func_body(P, &tmp);
            continue;
        }
        /* constructor */
        if (TT == TK_IDENT && CUR->len == c->len && memcmp(CUR->start, c->name, c->len) == 0 && PEEK(1)->type == TK_LPAREN) {
            token_t* nm = adv(P); adv(P);
            member_t* m = new_member(P, M_CTOR, mods, nm);
            m->fn = new_fn(P, nm->line);
            m->fn->name = ".ctor"; m->fn->len = 5;
            m->fn->params = parse_params(P, TK_RPAREN, &m->fn->nparams);
            m->fn->is_static = (mods & MOD_STATIC) != 0;
            if (match(P, TK_COLON)) {
                if (match(P, TK_BASE)) m->ctor_call = 1;
                else if (match(P, TK_THIS)) m->ctor_call = 2;
                else perr(P, CUR, "expected 'base' or 'this'");
                expect(P, TK_LPAREN, "'('");
                m->ctor_args = parse_args(P, TK_RPAREN);
                m->has_ctor_call = 1;
            }
            parse_func_body(P, m->fn);
            if (!m->fn->body) perr(P, nm, "constructor needs a body");
            ADDM(m);
            continue;
        }
        if (check(P, TK_IMPLICIT) || check(P, TK_EXPLICIT)) perr(P, CUR, "conversion operators are not supported");
        typeref_t* ty = parse_type(P, false);
        if (check(P, TK_OPERATOR)) {
            token_t* ot = adv(P);
            token_t* opt = adv(P);
            int optype = opt->type;
            if (optype == TK_GT && check(P, TK_GT)) perr(P, ot, "operator >> is not supported");
            member_t* m = new_member(P, M_OPERATOR, mods | MOD_STATIC, ot);
            m->fn = new_fn(P, ot->line);
            expect(P, TK_LPAREN, "'('");
            m->fn->params = parse_params(P, TK_RPAREN, &m->fn->nparams);
            const char* nm = operator_name(optype, m->fn->nparams);
            if (!nm) perr(P, opt, "unsupported operator overload");
            m->name = nm; m->len = (uint32_t)strlen(nm);
            m->fn->name = nm; m->fn->len = m->len; m->fn->ret = ty; m->fn->is_static = 1;
            parse_func_body(P, m->fn);
            ADDM(m);
            continue;
        }
        if (check(P, TK_THIS)) { /* indexer */
            token_t* it = adv(P);
            member_t* m = new_member(P, M_INDEXER, mods, it);
            m->type = ty; m->name = "Item"; m->len = 4;
            expect(P, TK_LBRACK, "'['");
            uint8_t np; param_t* ps = parse_params(P, TK_RBRACK, &np);
            if (match(P, TK_ARROW)) {
                m->getter = new_fn(P, it->line); m->getter->body = parse_expr(P); m->getter->expr_body = 1;
                expect(P, TK_SEMI, "';'");
            } else parse_property_body(P, m);
            if (m->getter) { m->getter->params = ps; m->getter->nparams = np; }
            if (m->setter) {
                /* setter params: index params + value */
                param_t *ch = NULL, *ct = NULL;
                for (param_t* p = ps; p; p = p->next) {
                    param_t* cp = (param_t*)arena_alloc(P->A, sizeof(param_t)); *cp = *p; cp->next = NULL;
                    if (!ch) ch = cp; else ct->next = cp; ct = cp;
                }
                param_t* vp = m->setter->params;
                if (ct) ct->next = vp; else ch = vp;
                m->setter->params = ch; m->setter->nparams = (uint8_t)(np + 1);
            }
            ADDM(m);
            continue;
        }
        token_t* nm = expect(P, TK_IDENT, "member name");
        if (check(P, TK_LPAREN) || check(P, TK_LT)) {
            member_t* m = new_member(P, M_METHOD, mods, nm);
            m->name = nm->start; m->len = nm->len;
            m->fn = new_fn(P, nm->line);
            m->fn->name = nm->start; m->fn->len = nm->len; m->fn->ret = ty;
            m->fn->is_static = (mods & MOD_STATIC) != 0;
            if (check(P, TK_LT)) skip_type_args(P);
            expect(P, TK_LPAREN, "'('");
            m->fn->params = parse_params(P, TK_RPAREN, &m->fn->nparams);
            parse_func_body(P, m->fn);
            if (!m->fn->body && !(mods & (MOD_ABSTRACT)) && c->kind != C_INTERFACE) {
                if (!(mods & MOD_STATIC) || true) { /* extern/partial: treat as abstract */ }
            }
            ADDM(m);
            continue;
        }
        if (check(P, TK_LBRACE)) {
            member_t* m = new_member(P, M_PROP, mods, nm);
            m->name = nm->start; m->len = nm->len; m->type = ty;
            parse_property_body(P, m);
            ADDM(m);
            continue;
        }
        if (check(P, TK_ARROW)) { /* expression-bodied property */
            adv(P);
            member_t* m = new_member(P, M_PROP, mods, nm);
            m->name = nm->start; m->len = nm->len; m->type = ty;
            m->getter = new_fn(P, nm->line);
            { token_t* gat = CUR; m->getter->body = tuple_relabel(P, gat, ty, parse_expr(P)); }
            m->getter->expr_body = 1;
            expect(P, TK_SEMI, "';'");
            ADDM(m);
            continue;
        }
        /* fields */
        P->pos--; /* back to name */
        for (;;) {
            token_t* fn = expect(P, TK_IDENT, "field name");
            member_t* m = new_member(P, M_FIELD, mods, fn);
            m->name = fn->start; m->len = fn->len; m->type = ty;
            if (match(P, TK_ASSIGN)) {
                typeref_t* save = P->target_type; P->target_type = ty;
                token_t* iat = CUR;
                m->init = check(P, TK_LBRACE) ? parse_array_lit(P) : tuple_relabel(P, iat, ty, parse_expr(P));
                P->target_type = save;
            }
            ADDM(m);
            if (!match(P, TK_COMMA)) break;
        }
        expect(P, TK_SEMI, "';'");
        (void)start;
    }
    adv(P);
    /* append (supports partial classes declared twice) */
    if (!c->members) c->members = h;
    else { member_t* x = c->members; while (x->next) x = x->next; x->next = h; }
#undef ADDM
}

static classdecl_t* find_class(program_t* prog, const char* name, uint32_t len) {
    for (classdecl_t* c = prog->classes; c; c = c->next) if (c->len == len && memcmp(c->name, name, len) == 0) return c;
    return NULL;
}

static void parse_type_decl(parser_t* P, uint8_t mods) {
    token_t* kt = adv(P);
    int kind = kt->type == TK_CLASS ? C_CLASS : kt->type == TK_STRUCT ? C_STRUCT : kt->type == TK_INTERFACE ? C_INTERFACE :
               kt->type == TK_ENUM ? C_ENUM : C_CLASS;
    if (is_word(kt, "record")) { if (check(P, TK_CLASS) || check(P, TK_STRUCT)) adv(P); }
    token_t* nm = expect(P, TK_IDENT, "type name");
    classdecl_t* c = find_class(P->prog, nm->start, nm->len);
    bool is_new = c == NULL;
    if (c && (kind == C_ENUM || c->kind != (uint8_t)kind))   /* only partial class/struct/interface reopen */
        perr(P, nm, "the type '%.*s' is already defined", (int)nm->len, nm->start);
    if (!c) {
        c = (classdecl_t*)arena_alloc(P->A, sizeof(classdecl_t));
        c->kind = (uint8_t)kind; c->name = nm->start; c->len = nm->len; c->line = nm->line;
        c->is_static = (mods & MOD_STATIC) != 0;
    }
    P->cur_class = c->name; P->cur_class_len = c->len;
    if (check(P, TK_LT)) skip_type_args(P);
    if (check(P, TK_LPAREN)) perr(P, CUR, "primary constructors are not supported");
    if (match(P, TK_COLON)) {
        namelist_t* tl = c->bases;
        while (tl && tl->next) tl = tl->next;
        for (;;) {
            typeref_t* bt = parse_type(P, false);
            namelist_t* nl = (namelist_t*)arena_alloc(P->A, sizeof(namelist_t));
            nl->name = bt->name; nl->len = bt->len;
            if (!c->bases) c->bases = nl; else tl->next = nl;
            tl = nl;
            if (!match(P, TK_COMMA)) break;
        }
    }
    skip_where(P);
    if (kind == C_ENUM) {
        expect(P, TK_LBRACE, "'{'");
        enumval_t* tl = c->enums;
        while (tl && tl->next) tl = tl->next;
        while (!check(P, TK_RBRACE)) {
            skip_attributes(P);
            token_t* en = expect(P, TK_IDENT, "enum member");
            enumval_t* ev = (enumval_t*)arena_alloc(P->A, sizeof(enumval_t));
            ev->name = en->start; ev->len = en->len;
            if (match(P, TK_ASSIGN)) ev->value = parse_ternary(P);
            if (!c->enums) c->enums = ev; else tl->next = ev;
            tl = ev;
            if (!match(P, TK_COMMA)) break;
        }
        expect(P, TK_RBRACE, "'}'");
    } else {
        parse_class_body(P, c);
    }
    match(P, TK_SEMI);
    if (is_new) {
        if (!P->prog->classes) P->prog->classes = c;
        else { classdecl_t* x = P->prog->classes; while (x->next) x = x->next; x->next = c; }
    }
}

static void parse_namespace_members(parser_t* P, bool braced) {
    for (;;) {
        if (braced && check(P, TK_RBRACE)) { adv(P); return; }
        if (check(P, TK_EOF)) { if (braced) perr(P, CUR, "expected '}'"); return; }
        if (check(P, TK_USING) && PEEK(1)->type != TK_LPAREN && !is_word(PEEK(1), "var")) {
            while (!check(P, TK_SEMI) && !check(P, TK_EOF)) adv(P);
            adv(P);
            continue;
        }
        if (check(P, TK_NAMESPACE)) {
            adv(P);
            while (TT == TK_IDENT || TT == TK_DOT) adv(P);
            if (match(P, TK_SEMI)) continue;      /* file-scoped */
            expect(P, TK_LBRACE, "'{'");
            parse_namespace_members(P, true);
            continue;
        }
        skip_attributes(P);
        uint32_t save = P->pos;
        uint8_t mods = parse_modifiers(P);
        if (check(P, TK_CLASS) || check(P, TK_STRUCT) || check(P, TK_INTERFACE) || check(P, TK_ENUM) ||
            (is_word(CUR, "record") && (PEEK(1)->type == TK_IDENT || PEEK(1)->type == TK_CLASS || PEEK(1)->type == TK_STRUCT))) {
            parse_type_decl(P, mods);
            continue;
        }
        if (check(P, TK_DELEGATE) && PEEK(1)->type != TK_LPAREN) { /* delegate type declaration: ignore */
            while (!check(P, TK_SEMI) && !check(P, TK_EOF)) adv(P);
            adv(P);
            continue;
        }
        P->pos = save;
        node_t* s = parse_stmt(P);
        if (s) {
            if (!P->prog->stmts) P->prog->stmts = s; else P->prog->stmts_tail->next = s;
            P->prog->stmts_tail = s;
            while (P->prog->stmts_tail->next) P->prog->stmts_tail = P->prog->stmts_tail->next;
        }
    }
}

void mcs_front_free_tokens(front_ctx_t* ctx) {
    if (ctx->heap_toks) mcs_realloc(ctx->vm, ctx->heap_toks, sizeof(token_t) * ctx->heap_cap, 0);
    ctx->heap_toks = NULL; ctx->heap_cap = 0;
}

bool mcs_parse(front_ctx_t* ctx, const char* src, program_t* prog) {
    toklist_t tl = {0};
    tl.heap = 1;
    if (!mcs_lex(ctx, src, strlen(src), 1, &tl)) { mcs_front_free_tokens(ctx); return false; }
    parser_t P_;
    parser_t* P = &P_;
    memset(P, 0, sizeof *P);
    P->ctx = ctx; P->A = ctx->arena; P->t = tl.toks; P->n = tl.count; P->pos = 0; P->prog = prog;
    if (setjmp(P->jb)) { mcs_front_free_tokens(ctx); return false; }
    prescan_tuple_members(P);
    parse_namespace_members(P, false);
    mcs_front_free_tokens(ctx);   /* the AST points into the source text, not into tokens */
    return ctx->errors == 0;
}
#endif
