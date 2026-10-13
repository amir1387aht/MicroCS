/* Exhaustive keyword classification, independent of the lookup boundaries. */
#include "mcs_front.h"
#include <assert.h>
#include <stdio.h>
static const struct { const char* word; uint16_t type; } expected[] = {
    { "abstract", TK_ABSTRACT },
    { "as", TK_AS },
    { "base", TK_BASE },
    { "bool", TK_BOOL },
    { "break", TK_BREAK },
    { "byte", TK_BYTE },
    { "case", TK_CASE },
    { "catch", TK_CATCH },
    { "char", TK_KCHAR },
    { "checked", TK_CHECKED },
    { "class", TK_CLASS },
    { "const", TK_CONST },
    { "continue", TK_CONTINUE },
    { "decimal", TK_DECIMAL },
    { "default", TK_DEFAULT },
    { "delegate", TK_DELEGATE },
    { "do", TK_DO },
    { "double", TK_DOUBLE },
    { "else", TK_ELSE },
    { "enum", TK_ENUM },
    { "event", TK_EVENT },
    { "explicit", TK_EXPLICIT },
    { "extern", TK_EXTERN },
    { "false", TK_FALSE },
    { "finally", TK_FINALLY },
    { "fixed", TK_FIXED },
    { "float", TK_KFLOAT },
    { "for", TK_FOR },
    { "foreach", TK_FOREACH },
    { "goto", TK_GOTO },
    { "if", TK_IF },
    { "implicit", TK_IMPLICIT },
    { "in", TK_IN },
    { "int", TK_KINT },
    { "interface", TK_INTERFACE },
    { "internal", TK_INTERNAL },
    { "is", TK_IS },
    { "lock", TK_LOCK },
    { "long", TK_LONG },
    { "namespace", TK_NAMESPACE },
    { "new", TK_NEW },
    { "null", TK_NULL },
    { "object", TK_OBJECT },
    { "operator", TK_OPERATOR },
    { "out", TK_OUT },
    { "override", TK_OVERRIDE },
    { "params", TK_PARAMS },
    { "private", TK_PRIVATE },
    { "protected", TK_PROTECTED },
    { "public", TK_PUBLIC },
    { "readonly", TK_READONLY },
    { "ref", TK_REF },
    { "return", TK_RETURN },
    { "sbyte", TK_SBYTE },
    { "sealed", TK_SEALED },
    { "short", TK_SHORT },
    { "sizeof", TK_SIZEOF },
    { "static", TK_STATIC },
    { "string", TK_KSTRING },
    { "struct", TK_STRUCT },
    { "switch", TK_SWITCH },
    { "this", TK_THIS },
    { "throw", TK_THROW },
    { "true", TK_TRUE },
    { "try", TK_TRY },
    { "typeof", TK_TYPEOF },
    { "uint", TK_UINT },
    { "ulong", TK_ULONG },
    { "unchecked", TK_UNCHECKED },
    { "unsafe", TK_UNSAFE },
    { "ushort", TK_USHORT },
    { "using", TK_USING },
    { "virtual", TK_VIRTUAL },
    { "void", TK_VOID },
    { "volatile", TK_VOLATILE },
    { "while", TK_WHILE },
};
static void check(mcs_vm_t* vm, const char* source, uint16_t type, const char* spelling) {
    arena_t arena = { vm, NULL };
    front_ctx_t ctx = { vm, &arena, "keyword-test.cs", 0, NULL, 0 };
    toklist_t tokens; memset(&tokens, 0, sizeof tokens);
    assert(mcs_lex(&ctx, source, strlen(source), 1, &tokens));
    assert(tokens.count == 2);
    assert(tokens.toks[0].type == type);
    assert(tokens.toks[0].len == strlen(spelling));
    assert(!memcmp(tokens.toks[0].start, spelling, strlen(spelling)));
    assert(tokens.toks[0].line == 1 && tokens.toks[0].col >= 1);
    assert(tokens.toks[1].type == TK_EOF);
    mcs_front_free_tokens(&ctx); arena_free(&arena);
}
int main(void) {
    mcs_config_t config; mcs_config_default(&config);
    mcs_vm_t* vm = mcs_new(&config); assert(vm);
    unsigned seen[TK_KW_LAST - TK_KW_FIRST]; memset(seen, 0, sizeof seen);
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        const char* word = expected[i].word;
        assert(expected[i].type >= TK_KW_FIRST && expected[i].type < TK_KW_LAST);
        seen[expected[i].type - TK_KW_FIRST]++;
        check(vm, word, expected[i].type, word);
        char source[64];
        snprintf(source, sizeof source, "@%s", word);
        check(vm, source, TK_IDENT, word);
        snprintf(source, sizeof source, "%s_suffix", word);
        check(vm, source, TK_IDENT, source);
        snprintf(source, sizeof source, "x%s", word);
        check(vm, source, TK_IDENT, source);
        snprintf(source, sizeof source, "%s", word); source[0] -= 'a' - 'A';
        check(vm, source, TK_IDENT, source);
    }
    for (unsigned i = 0; i < TK_KW_LAST - TK_KW_FIRST; i++) assert(seen[i] == 1);
    const char* identifiers[] = { "a", "zz", "x", "z", "get", "set", "var", "await", "async", "int32", "_int", "total", "volatilex", "international", "éclair" };
    for (unsigned i = 0; i < sizeof identifiers / sizeof identifiers[0]; i++) check(vm, identifiers[i], TK_IDENT, identifiers[i]);
    mcs_free(vm);
    puts("PASS lexer keywords: all enum entries, escaped/case/prefix/suffix/contextual/Unicode identifiers");
    return 0;
}
