/* MicroCS lexer: converts C# source text to a token array. */
#include "mcs_front.h"
#if MCS_ENABLE_COMPILER
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------ arena */
void* arena_alloc(arena_t* a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > 8192 ? n : 8192;
        arena_chunk_t* c = (arena_chunk_t*)mcs_realloc(a->vm, NULL, 0, sizeof(arena_chunk_t) + cap);
        c->next = a->head; c->used = 0; c->cap = cap; a->head = c;
    }
    void* p = a->head->data + a->head->used;
    a->head->used += n;
    memset(p, 0, n);
    return p;
}
void arena_free(arena_t* a) {
    arena_chunk_t* c = a->head;
    while (c) { arena_chunk_t* n = c->next; mcs_realloc(a->vm, c, sizeof(arena_chunk_t) + c->cap, 0); c = n; }
    a->head = NULL;
}
char* arena_strdup(arena_t* a, const char* s, size_t n) {
    char* p = (char*)arena_alloc(a, n + 1);
    memcpy(p, s, n); p[n] = 0;
    return p;
}

void mcs_front_error(front_ctx_t* ctx, uint32_t line, uint32_t col, const char* fmt, ...) {
    char msg[200];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (ctx->errors == 0)
        snprintf(ctx->vm->error, sizeof ctx->vm->error, "%s(%u,%u): error: %s", ctx->src_name, (unsigned)line, (unsigned)col, msg);
    if (ctx->errors < 10) mcs_report_error(ctx->vm, "%s(%u,%u): error: %s\n", ctx->src_name, line, col, msg);
    ctx->errors++;
}

/* ------------------------------------------------------------ keywords */
/* keyword length is stored so identifiers are rejected without strlen() */
#define KW(w, t) { w, (uint8_t)(sizeof(w) - 1), t }
static const struct { const char* s; uint8_t n; uint16_t t; } kw[] = {
    KW("abstract", TK_ABSTRACT), KW("as", TK_AS), KW("base", TK_BASE), KW("bool", TK_BOOL), KW("break", TK_BREAK),
    KW("byte", TK_BYTE), KW("case", TK_CASE), KW("catch", TK_CATCH), KW("char", TK_KCHAR), KW("class", TK_CLASS),
    KW("const", TK_CONST), KW("continue", TK_CONTINUE), KW("decimal", TK_DECIMAL), KW("default", TK_DEFAULT),
    KW("delegate", TK_DELEGATE), KW("do", TK_DO), KW("double", TK_DOUBLE), KW("else", TK_ELSE), KW("enum", TK_ENUM),
    KW("event", TK_EVENT), KW("false", TK_FALSE), KW("finally", TK_FINALLY), KW("float", TK_KFLOAT), KW("for", TK_FOR),
    KW("foreach", TK_FOREACH), KW("if", TK_IF), KW("in", TK_IN), KW("int", TK_KINT), KW("interface", TK_INTERFACE),
    KW("internal", TK_INTERNAL), KW("is", TK_IS), KW("long", TK_LONG), KW("namespace", TK_NAMESPACE), KW("new", TK_NEW),
    KW("null", TK_NULL), KW("object", TK_OBJECT), KW("operator", TK_OPERATOR), KW("out", TK_OUT), KW("override", TK_OVERRIDE),
    KW("params", TK_PARAMS), KW("private", TK_PRIVATE), KW("protected", TK_PROTECTED), KW("public", TK_PUBLIC),
    KW("readonly", TK_READONLY), KW("ref", TK_REF), KW("return", TK_RETURN), KW("sbyte", TK_SBYTE), KW("sealed", TK_SEALED),
    KW("short", TK_SHORT), KW("static", TK_STATIC), KW("string", TK_KSTRING), KW("struct", TK_STRUCT), KW("switch", TK_SWITCH),
    KW("this", TK_THIS), KW("throw", TK_THROW), KW("true", TK_TRUE), KW("try", TK_TRY), KW("typeof", TK_TYPEOF),
    KW("uint", TK_UINT), KW("ulong", TK_ULONG), KW("ushort", TK_USHORT), KW("using", TK_USING), KW("virtual", TK_VIRTUAL),
    KW("void", TK_VOID), KW("while", TK_WHILE), KW("extern", TK_EXTERN), KW("unsafe", TK_UNSAFE), KW("volatile", TK_VOLATILE),
    KW("implicit", TK_IMPLICIT), KW("explicit", TK_EXPLICIT), KW("sizeof", TK_SIZEOF), KW("lock", TK_LOCK),
    KW("goto", TK_GOTO), KW("checked", TK_CHECKED), KW("unchecked", TK_UNCHECKED), KW("fixed", TK_FIXED),
    {NULL, 0, 0}
};

typedef struct {
    front_ctx_t* ctx;
    const char* src;
    const char* p;
    const char* end;
    const char* line_start;
    uint32_t line;
    toklist_t* out;
} lexer_t;

typedef char mcs_tok_type_fits_u8[(TK_KW_LAST < 256) ? 1 : -1]; /* token_t.type is a uint8_t */
static token_t* push_tok(lexer_t* L, uint16_t type, const char* start, size_t len) {
    toklist_t* t = L->out;
    if (t->count == t->cap) {
        /* first chunk sized from the remaining source (~1 token per 5 bytes) so the
         * arena rarely holds abandoned copies of a doubled token array */
        uint32_t nc = (uint32_t)((L->end - start) / 5) + 32;
        if (t->cap) {
            /* grow by the token density seen so far instead of doubling: the
             * token array is the largest single block while parsing */
            size_t done = (size_t)(start - L->src) + 1, left = (size_t)(L->end - start);
            size_t more = (size_t)t->count * left / done;
            nc = t->cap + (uint32_t)(more + more / 8) + 32;
        }
        if (t->heap) {   /* main token list: VM heap, so the compiler phase does not carry it */
            token_t* nt = (token_t*)mcs_realloc(L->ctx->vm, t->toks, sizeof(token_t) * t->cap, sizeof(token_t) * nc);
            t->toks = nt; t->cap = nc;
            L->ctx->heap_toks = nt; L->ctx->heap_cap = nc;
        } else {
            token_t* nt = (token_t*)arena_alloc(L->ctx->arena, sizeof(token_t) * nc);
            if (t->count) memcpy(nt, t->toks, sizeof(token_t) * t->count);
            t->toks = nt; t->cap = nc;
        }
    }
    token_t* k = &t->toks[t->count++];
    memset(k, 0, sizeof *k);
    k->start = start; k->len = (uint32_t)len;
    k->type = (uint8_t)type; k->line = L->line & 0xFFFFF; { size_t col = (size_t)(start - L->line_start + 1); k->col = col > 4095 ? 4095 : (uint32_t)col; }
    return k;
}

static void lex_err(lexer_t* L, const char* msg) {
    mcs_front_error(L->ctx, L->line, (uint32_t)(L->p - L->line_start + 1), "%s", msg);
}

static bool is_ident_start(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
static bool is_ident(int c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

static void newline(lexer_t* L) { L->line++; L->line_start = L->p; }

static void skip_space(lexer_t* L) {
    for (;;) {
        if (L->p >= L->end) return;
        char c = *L->p;
        if (c == '\n') { L->p++; newline(L); }
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') L->p++;
        else if (c == '/' && L->p + 1 < L->end && L->p[1] == '/') { while (L->p < L->end && *L->p != '\n') L->p++; }
        else if (c == '/' && L->p + 1 < L->end && L->p[1] == '*') {
            L->p += 2;
            while (L->p < L->end && !(L->p[0] == '*' && L->p + 1 < L->end && L->p[1] == '/')) { if (*L->p == '\n') { L->p++; newline(L); } else L->p++; }
            if (L->p < L->end) L->p += 2; else lex_err(L, "unterminated comment");
        } else if (c == '#' ) {
            /* preprocessor lines (#region, #pragma, #if ...) are ignored */
            const char* q = L->p - 1;
            bool at_line_start = true;
            while (q >= L->line_start) { if (*q != ' ' && *q != '\t') { at_line_start = false; break; } q--; }
            if (!at_line_start) return;
            while (L->p < L->end && *L->p != '\n') L->p++;
        } else if ((uint8_t)c == 0xEF && L->p + 2 < L->end && (uint8_t)L->p[1] == 0xBB && (uint8_t)L->p[2] == 0xBF) L->p += 3;
        else return;
    }
}

static void lex_number(lexer_t* L) {
    const char* s = L->p;
    char buf[80]; size_t n = 0;
    bool is_float = false; int base = 10;
    if (*L->p == '0' && L->p + 1 < L->end && (L->p[1] == 'x' || L->p[1] == 'X')) { base = 16; L->p += 2; }
    else if (*L->p == '0' && L->p + 1 < L->end && (L->p[1] == 'b' || L->p[1] == 'B')) { base = 2; L->p += 2; }
    while (L->p < L->end) {
        char c = *L->p;
        if (c == '_') { L->p++; continue; }
        if (base == 16 ? ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                       : (c >= '0' && c <= '9')) { if (n < 78) buf[n++] = c; L->p++; continue; }
        if (base == 10 && c == '.' && L->p + 1 < L->end && L->p[1] >= '0' && L->p[1] <= '9' && !is_float) { is_float = true; buf[n++] = c; L->p++; continue; }
        if (base == 10 && (c == 'e' || c == 'E')) {
            const char* q = L->p + 1;
            if (q < L->end && (*q == '+' || *q == '-')) q++;
            if (q < L->end && *q >= '0' && *q <= '9') {
                is_float = true; buf[n++] = 'e'; L->p++;
                if (*L->p == '+' || *L->p == '-') buf[n++] = *L->p++;
                continue;
            }
        }
        break;
    }
    buf[n] = 0;
    uint8_t f32 = 0;
    /* suffixes */
    while (L->p < L->end) {
        char c = *L->p;
        if (base == 10 && (c == 'f' || c == 'F')) { is_float = true; f32 = 1; L->p++; }
        else if (base == 10 && (c == 'd' || c == 'D' || c == 'm' || c == 'M')) { is_float = true; L->p++; }
        else if (c == 'u' || c == 'U' || c == 'l' || c == 'L') L->p++;
        else break;
    }
    (void)f32;
    if (L->p < L->end && is_ident(*L->p)) { lex_err(L, "invalid numeric literal"); while (L->p < L->end && is_ident(*L->p)) L->p++; }
    token_t* t;
    if (is_float) {
#if MCS_ENABLE_FLOAT
        t = push_tok(L, TK_FLOAT, s, (size_t)(L->p - s));
        t->v.f = (mcs_float_t)strtod(buf, NULL);
        t->is_float32 = f32;
#else
        lex_err(L, "floating point support is disabled");
        t = push_tok(L, TK_INT, s, (size_t)(L->p - s));
#endif
    } else {
        t = push_tok(L, TK_INT, s, (size_t)(L->p - s));
        unsigned long long v = strtoull(buf, NULL, base);
        t->v.i = (mcs_int_t)(mcs_uint_t)v;
    }
}

/* scan a quoted literal; returns false on error */
static bool scan_string(lexer_t* L, bool verbatim, bool interp) {
    int depth = 0;
    while (L->p < L->end) {
        char c = *L->p;
        if (interp && depth == 0 && c == '{') {
            if (L->p + 1 < L->end && L->p[1] == '{') { L->p += 2; continue; }
            depth = 1; L->p++;
            /* skip the hole, honouring nested strings and braces */
            while (L->p < L->end && depth > 0) {
                char d = *L->p;
                if (d == '{') depth++;
                else if (d == '}') depth--;
                else if (d == '"') { L->p++; while (L->p < L->end && *L->p != '"') { if (*L->p == '\\') L->p++; L->p++; } }
                else if (d == '\'') { L->p++; while (L->p < L->end && *L->p != '\'') { if (*L->p == '\\') L->p++; L->p++; } }
                else if (d == '\n') { L->p++; newline(L); continue; }
                L->p++;
            }
            continue;
        }
        if (c == '"') {
            if (verbatim && L->p + 1 < L->end && L->p[1] == '"') { L->p += 2; continue; }
            return true;
        }
        if (c == '\\' && !verbatim) { L->p += 2; continue; }
        if (c == '\n') { if (!verbatim) { lex_err(L, "newline in string literal"); return false; } L->p++; newline(L); continue; }
        L->p++;
    }
    lex_err(L, "unterminated string literal");
    return false;
}

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* decode one escape at *pp (after the backslash); returns code point */
uint32_t mcs_decode_escape(const char** pp, const char* end) {
    const char* p = *pp;
    uint32_t cp = 0;
    char c = *p++;
    switch (c) {
    case 'n': cp = '\n'; break; case 't': cp = '\t'; break; case 'r': cp = '\r'; break;
    case '0': cp = 0; break; case 'a': cp = 7; break; case 'b': cp = 8; break; case 'f': cp = 12; break;
    case 'v': cp = 11; break; case 'e': cp = 27; break;
    case 'u': case 'x': case 'U': {
        int max = c == 'u' ? 4 : c == 'U' ? 8 : 4;
        for (int i = 0; i < max && p < end && hexval(*p) >= 0; i++) cp = cp * 16 + (uint32_t)hexval(*p++);
        break;
    }
    default: cp = (uint8_t)c; break;
    }
    *pp = p;
    return cp;
}

static void lex_char(lexer_t* L) {
    const char* s = L->p;
    L->p++;
    uint32_t cp = 0;
    if (L->p < L->end && *L->p == '\\') { L->p++; cp = mcs_decode_escape(&L->p, L->end); }
    else if (L->p < L->end) {
        uint8_t c = (uint8_t)*L->p++;
        if (c < 0x80) cp = c;
        else { /* UTF-8 decode */
            int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            cp = c & (0x3F >> n);
            while (n-- && L->p < L->end) cp = (cp << 6) | ((uint8_t)*L->p++ & 0x3F);
        }
    }
    if (L->p >= L->end || *L->p != '\'') { lex_err(L, "invalid character literal"); return; }
    L->p++;
    token_t* t = push_tok(L, TK_CHAR, s, (size_t)(L->p - s));
    t->v.i = (mcs_int_t)cp;
}

bool mcs_lex(front_ctx_t* ctx, const char* src, size_t len, uint32_t line0, toklist_t* out) {
    lexer_t L;
    L.ctx = ctx; L.src = src; L.p = src; L.end = src + len; L.line_start = src; L.line = line0; L.out = out;
    int errs = ctx->errors;
    for (;;) {
        skip_space(&L);
        if (L.p >= L.end) break;
        const char* s = L.p;
        char c = *L.p;
        if (is_ident_start((uint8_t)c) || (c == '@' && L.p + 1 < L.end && is_ident_start((uint8_t)L.p[1]))) {
            bool at = c == '@';
            if (at) { L.p++; s++; }
            while (L.p < L.end && is_ident((uint8_t)*L.p)) L.p++;
            size_t n = (size_t)(L.p - s);
            uint16_t type = TK_IDENT;
            if (!at && n >= 2 && n <= 9 && *s >= 'a' && *s <= 'z')
                for (int i = 0; kw[i].s; i++) if (kw[i].n == n && kw[i].s[0] == *s && memcmp(kw[i].s, s, n) == 0) { type = kw[i].t; break; }
            push_tok(&L, type, s, n);
            continue;
        }
        if (c >= '0' && c <= '9') { lex_number(&L); continue; }
        if (c == '.' && L.p + 1 < L.end && L.p[1] >= '0' && L.p[1] <= '9' && !(L.p > src && L.p[-1] == '.')) { lex_number(&L); continue; } /* not the 2nd dot of a range `1..3` */
        if (c == '\'') { lex_char(&L); continue; }
        if (c == '"' || ((c == '@' || c == '$') && L.p + 1 < L.end && (L.p[1] == '"' || ((L.p[1] == '@' || L.p[1] == '$') && L.p + 2 < L.end && L.p[2] == '"')))) {
            bool verbatim = false, interp = false;
            uint32_t line = L.line;
            while (*L.p != '"') { if (*L.p == '@') verbatim = true; if (*L.p == '$') interp = true; L.p++; }
            L.p++;
            const char* body = L.p;
            const char* ls = L.line_start;
            if (!scan_string(&L, verbatim, interp)) break;
            token_t* t = push_tok(&L, interp ? TK_INTERP : TK_STRING, body, (size_t)(L.p - body));
            t->line = line & 0xFFFFF; { size_t col = (size_t)(s - ls + 1); t->col = col > 4095 ? 4095 : (uint32_t)col; }
            t->verbatim = verbatim;
            L.p++; /* closing quote */
            continue;
        }
        L.p++;
        char n1 = L.p < L.end ? *L.p : 0;
        char n2 = L.p + 1 < L.end ? L.p[1] : 0;
        uint16_t t = TK_ERROR;
#define TWO(ch, tk) if (n1 == ch) { L.p++; t = tk; break; }
        switch (c) {
        case '(': t = TK_LPAREN; break; case ')': t = TK_RPAREN; break;
        case '{': t = TK_LBRACE; break; case '}': t = TK_RBRACE; break;
        case '[': t = TK_LBRACK; break; case ']': t = TK_RBRACK; break;
        case ';': t = TK_SEMI; break; case ',': t = TK_COMMA; break;
        case '.': t = TK_DOT; break; case '~': t = TK_TILDE; break;
        case ':': if (n1 == ':') { L.p++; t = TK_DOT; break; } t = TK_COLON; break;
        case '?':
            if (n1 == '?' && n2 == '=') { L.p += 2; t = TK_QQ_ASSIGN; break; }
            TWO('?', TK_QQ) TWO('.', TK_QDOT) TWO('[', TK_QLBRACK)
            t = TK_QUESTION; break;
        case '=': TWO('=', TK_EQ) TWO('>', TK_ARROW) t = TK_ASSIGN; break;
        case '!': TWO('=', TK_NE) t = TK_BANG; break;
        case '<':
            if (n1 == '<' && n2 == '=') { L.p += 2; t = TK_SHL_ASSIGN; break; }
            TWO('<', TK_SHL) TWO('=', TK_LE) t = TK_LT; break;
        case '>': TWO('=', TK_GE) t = TK_GT; break; /* >> handled by parser */
        case '+': TWO('+', TK_INC) TWO('=', TK_PLUS_ASSIGN) t = TK_PLUS; break;
        case '-': TWO('-', TK_DEC) TWO('=', TK_MINUS_ASSIGN) TWO('>', TK_DOT) t = TK_MINUS; break;
        case '*': TWO('=', TK_STAR_ASSIGN) t = TK_STAR; break;
        case '/': TWO('=', TK_SLASH_ASSIGN) t = TK_SLASH; break;
        case '%': TWO('=', TK_PERCENT_ASSIGN) t = TK_PERCENT; break;
        case '&': TWO('&', TK_ANDAND) TWO('=', TK_AMP_ASSIGN) t = TK_AMP; break;
        case '|': TWO('|', TK_OROR) TWO('=', TK_PIPE_ASSIGN) t = TK_PIPE; break;
        case '^': TWO('=', TK_CARET_ASSIGN) t = TK_CARET; break;
        default: break;
        }
#undef TWO
        if (t == TK_ERROR) { L.p = s; lex_err(&L, "unexpected character"); L.p = s + 1; continue; }
        push_tok(&L, t, s, (size_t)(L.p - s));
    }
    push_tok(&L, TK_EOF, L.p, 0);
    return ctx->errors == errs;
}
#endif
