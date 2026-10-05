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
static const struct { const char* s; uint16_t t; } kw[] = {
    {"abstract", TK_ABSTRACT}, {"as", TK_AS}, {"base", TK_BASE}, {"bool", TK_BOOL}, {"break", TK_BREAK},
    {"byte", TK_BYTE}, {"case", TK_CASE}, {"catch", TK_CATCH}, {"char", TK_KCHAR}, {"class", TK_CLASS},
    {"const", TK_CONST}, {"continue", TK_CONTINUE}, {"decimal", TK_DECIMAL}, {"default", TK_DEFAULT},
    {"delegate", TK_DELEGATE}, {"do", TK_DO}, {"double", TK_DOUBLE}, {"else", TK_ELSE}, {"enum", TK_ENUM},
    {"event", TK_EVENT}, {"false", TK_FALSE}, {"finally", TK_FINALLY}, {"float", TK_KFLOAT}, {"for", TK_FOR},
    {"foreach", TK_FOREACH}, {"if", TK_IF}, {"in", TK_IN}, {"int", TK_KINT}, {"interface", TK_INTERFACE},
    {"internal", TK_INTERNAL}, {"is", TK_IS}, {"long", TK_LONG}, {"namespace", TK_NAMESPACE}, {"new", TK_NEW},
    {"null", TK_NULL}, {"object", TK_OBJECT}, {"operator", TK_OPERATOR}, {"out", TK_OUT}, {"override", TK_OVERRIDE},
    {"params", TK_PARAMS}, {"private", TK_PRIVATE}, {"protected", TK_PROTECTED}, {"public", TK_PUBLIC},
    {"readonly", TK_READONLY}, {"ref", TK_REF}, {"return", TK_RETURN}, {"sbyte", TK_SBYTE}, {"sealed", TK_SEALED},
    {"short", TK_SHORT}, {"static", TK_STATIC}, {"string", TK_KSTRING}, {"struct", TK_STRUCT}, {"switch", TK_SWITCH},
    {"this", TK_THIS}, {"throw", TK_THROW}, {"true", TK_TRUE}, {"try", TK_TRY}, {"typeof", TK_TYPEOF},
    {"uint", TK_UINT}, {"ulong", TK_ULONG}, {"ushort", TK_USHORT}, {"using", TK_USING}, {"virtual", TK_VIRTUAL},
    {"void", TK_VOID}, {"while", TK_WHILE}, {"extern", TK_EXTERN}, {"unsafe", TK_UNSAFE}, {"volatile", TK_VOLATILE},
    {"implicit", TK_IMPLICIT}, {"explicit", TK_EXPLICIT}, {"sizeof", TK_SIZEOF}, {"lock", TK_LOCK},
    {"goto", TK_GOTO}, {"checked", TK_CHECKED}, {"unchecked", TK_UNCHECKED}, {"fixed", TK_FIXED},
    {NULL, 0}
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

static token_t* push_tok(lexer_t* L, uint16_t type, const char* start, size_t len) {
    toklist_t* t = L->out;
    if (t->count == t->cap) {
        /* first chunk sized from the remaining source (~1 token per 5 bytes) so the
         * arena rarely holds abandoned copies of a doubled token array */
        uint32_t nc = t->cap ? t->cap * 2 : (uint32_t)((L->end - start) / 5) + 32;
        token_t* nt = (token_t*)arena_alloc(L->ctx->arena, sizeof(token_t) * nc);
        if (t->count) memcpy(nt, t->toks, sizeof(token_t) * t->count);
        t->toks = nt; t->cap = nc;
    }
    token_t* k = &t->toks[t->count++];
    memset(k, 0, sizeof *k);
    k->type = type; k->start = start; k->len = (uint32_t)len;
    k->line = L->line; k->col = (uint16_t)(start - L->line_start + 1);
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
            if (!at) for (int i = 0; kw[i].s; i++) if (strlen(kw[i].s) == n && memcmp(kw[i].s, s, n) == 0) { type = kw[i].t; break; }
            push_tok(&L, type, s, n);
            continue;
        }
        if (c >= '0' && c <= '9') { lex_number(&L); continue; }
        if (c == '.' && L.p + 1 < L.end && L.p[1] >= '0' && L.p[1] <= '9') { lex_number(&L); continue; }
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
            t->line = line; t->col = (uint16_t)(s - ls + 1);
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
