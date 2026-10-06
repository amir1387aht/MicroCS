/* MicroCS front end: lexer, AST and parser shared definitions. */
#ifndef MCS_FRONT_H
#define MCS_FRONT_H
#include "mcs_internal.h"
#if MCS_ENABLE_COMPILER

/* ---------------------------------------------------------- arena */
typedef struct arena_chunk { struct arena_chunk* next; size_t used, cap; uint8_t data[1]; } arena_chunk_t;
typedef struct { mcs_vm_t* vm; arena_chunk_t* head; } arena_t;
void* arena_alloc(arena_t* a, size_t n);
void arena_free(arena_t* a);
char* arena_strdup(arena_t* a, const char* s, size_t n);

/* ---------------------------------------------------------- tokens */
typedef enum {
    TK_EOF, TK_ERROR, TK_IDENT, TK_INT, TK_FLOAT, TK_STRING, TK_CHAR, TK_INTERP,
    /* punctuation */
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACK, TK_RBRACK, TK_SEMI, TK_COMMA,
    TK_DOT, TK_QUESTION, TK_QQ, TK_QQ_ASSIGN, TK_QDOT, TK_QLBRACK, TK_COLON, TK_ARROW,
    TK_ASSIGN, TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT, TK_INC, TK_DEC,
    TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_STAR_ASSIGN, TK_SLASH_ASSIGN, TK_PERCENT_ASSIGN,
    TK_AMP, TK_PIPE, TK_CARET, TK_TILDE, TK_BANG, TK_ANDAND, TK_OROR,
    TK_AMP_ASSIGN, TK_PIPE_ASSIGN, TK_CARET_ASSIGN, TK_SHL, TK_SHL_ASSIGN,
    /* keywords */
    TK_KW_FIRST,
    TK_ABSTRACT = TK_KW_FIRST, TK_AS, TK_BASE, TK_BOOL, TK_BREAK, TK_BYTE, TK_CASE, TK_CATCH,
    TK_KCHAR, TK_CLASS, TK_CONST, TK_CONTINUE, TK_DECIMAL, TK_DEFAULT, TK_DELEGATE, TK_DO,
    TK_DOUBLE, TK_ELSE, TK_ENUM, TK_EVENT, TK_FALSE, TK_FINALLY, TK_KFLOAT, TK_FOR, TK_FOREACH,
    TK_IF, TK_IN, TK_KINT, TK_INTERFACE, TK_INTERNAL, TK_IS, TK_LONG, TK_NAMESPACE, TK_NEW,
    TK_NULL, TK_OBJECT, TK_OPERATOR, TK_OUT, TK_OVERRIDE, TK_PARAMS, TK_PRIVATE, TK_PROTECTED,
    TK_PUBLIC, TK_READONLY, TK_REF, TK_RETURN, TK_SBYTE, TK_SEALED, TK_SHORT, TK_STATIC,
    TK_KSTRING, TK_STRUCT, TK_SWITCH, TK_THIS, TK_THROW, TK_TRUE, TK_TRY, TK_TYPEOF, TK_UINT,
    TK_ULONG, TK_USHORT, TK_USING, TK_VIRTUAL, TK_VOID, TK_WHILE, TK_EXTERN, TK_UNSAFE,
    TK_VOLATILE, TK_IMPLICIT, TK_EXPLICIT, TK_SIZEOF, TK_LOCK, TK_GOTO, TK_CHECKED, TK_UNCHECKED,
    TK_FIXED,
    TK_KW_LAST
} tok_type_t;

/* 24 bytes on 32-bit targets (the whole token array is alive while parsing) */
typedef struct {
    uint8_t type;            /* tok_type_t (< 256) */
    uint8_t is_float32;      /* 'f' suffix */
    uint8_t verbatim;
    uint8_t spare;
    uint32_t line : 20;      /* diagnostics only: lines beyond 1M wrap */
    uint32_t col : 12;       /* clamped to 4095 */
    const char* start;
    uint32_t len;
    union { mcs_int_t i; mcs_float_t f; } v;
} token_t;

typedef struct {
    token_t* toks;
    uint32_t count, cap;
    uint8_t heap;            /* toks lives on the VM heap (freed after parsing), else in the arena */
} toklist_t;

/* ---------------------------------------------------------- types */
typedef struct typeref {
    const char* name; uint32_t len;  /* base name, e.g. "List", "int" */
    uint8_t prim;                    /* PT_* */
    uint8_t rank;                    /* array rank (number of []) */
    uint8_t conv;                    /* CV_* for numeric primitives, 0xff = none */
    uint8_t is_var;
    const char* tnames; uint32_t tnames_len; /* tuple types: element names "x,y" (NULL when unnamed) */
} typeref_t;

/* ---------------------------------------------------------- AST */
typedef enum {
    /* expressions */
    N_INT, N_FLOAT, N_STR, N_CHAR, N_BOOL, N_NULL, N_INTERP, N_NAME, N_THIS, N_BASE,
    N_MEMBER, N_INDEX, N_CALL, N_NEW, N_NEW_ARRAY, N_ARRAY_LIT, N_UNARY, N_BINARY,
    N_AND, N_OR, N_COALESCE, N_ASSIGN, N_PREINC, N_POSTINC, N_COND, N_CAST, N_IS, N_AS,
    N_LAMBDA, N_SWITCH_EXPR, N_TYPEOF, N_NAMEOF, N_DEFAULT, N_INIT_FIELD, N_INIT_INDEX,
    N_INIT_ADD, N_ARG_OUT,
    /* statements */
    N_EXPR_STMT, N_VAR, N_BLOCK, N_IF, N_WHILE, N_DO, N_FOR, N_FOREACH, N_BREAK, N_CONTINUE,
    N_RETURN, N_THROW, N_TRY, N_CATCH, N_SWITCH, N_CASE, N_LOCAL_FUNC, N_EMPTY, N_ARM
} node_kind_t;

typedef struct param {
    struct param* next;
    typeref_t* type;
    const char* name; uint32_t len;
    struct node* def;        /* default value or NULL */
    uint8_t is_params;
    uint8_t ref_kind;        /* 0 = by value, 1 = out, 2 = ref (copy-in/copy-out via a 1-element cell) */
} param_t;

struct funcdecl;

typedef struct node {
    uint8_t kind;
    uint8_t op;              /* token type for operators */
    uint8_t flag;            /* misc: nullcond, prefix, etc. */
    uint32_t line;
    uint32_t len;            /* length of name */
    struct node* next;       /* sibling in lists */
    struct node *a, *b, *c, *d;
    const char* name;
    typeref_t* type;
    struct funcdecl* fn;     /* lambdas, local functions */
    union { mcs_int_t i; mcs_float_t f; } lit; /* N_FLOAT uses f, everything else i */
} node_t;
#define ival lit.i
#define fval lit.f

/* member kinds */
enum { M_FIELD, M_METHOD, M_CTOR, M_PROP, M_CONST, M_INDEXER, M_OPERATOR };
#define MOD_STATIC   0x01
#define MOD_ABSTRACT 0x02
#define MOD_VIRTUAL  0x04
#define MOD_OVERRIDE 0x08
#define MOD_CONST    0x10

typedef struct funcdecl {
    const char* name; uint32_t len;
    param_t* params;
    uint8_t nparams;
    typeref_t* ret;
    node_t* body;            /* block, or expression when expr_body */
    uint8_t expr_body;
    uint8_t is_static;
    uint32_t line;
} funcdecl_t;

typedef struct member {
    struct member* next;
    uint8_t kind;
    uint8_t mods;
    uint32_t line;
    const char* name; uint32_t len;
    typeref_t* type;
    node_t* init;            /* field/const/prop initializer */
    funcdecl_t* fn;          /* method, ctor, operator */
    funcdecl_t* getter;      /* properties/indexers */
    funcdecl_t* setter;
    uint8_t auto_prop;
    node_t* ctor_args;       /* : base(...) or : this(...) */
    uint8_t ctor_call;       /* 0 none, 1 base, 2 this */
    uint8_t has_ctor_call;
} member_t;

typedef struct enumval { struct enumval* next; const char* name; uint32_t len; node_t* value; mcs_int_t computed; } enumval_t;

typedef struct typename_list { struct typename_list* next; const char* name; uint32_t len; } namelist_t;

enum { C_CLASS, C_STRUCT, C_INTERFACE, C_ENUM };
typedef struct classdecl {
    struct classdecl* next;
    uint8_t kind;
    uint8_t is_static;
    uint32_t line;
    const char* name; uint32_t len;
    namelist_t* bases;        /* base class + interfaces as written */
    const char* base; uint32_t base_len; /* resolved base class (may be native) */
    struct classdecl* base_decl;          /* script base class or NULL */
    member_t* members;
    enumval_t* enums;
    uint8_t state;            /* topo sort */
} classdecl_t;

typedef struct {
    classdecl_t* classes;
    node_t* stmts;            /* top-level statements (incl. local funcs) */
    node_t* stmts_tail;
} program_t;

/* ---------------------------------------------------------- API */
typedef struct {
    mcs_vm_t* vm;
    arena_t* arena;
    const char* src_name;
    int errors;
    token_t* heap_toks; uint32_t heap_cap;   /* main token array, released as soon as parsing ends */
} front_ctx_t;

bool mcs_lex(front_ctx_t* ctx, const char* src, size_t len, uint32_t line0, toklist_t* out);
bool mcs_parse(front_ctx_t* ctx, const char* src, program_t* prog);
void mcs_front_free_tokens(front_ctx_t* ctx);
void mcs_front_error(front_ctx_t* ctx, uint32_t line, uint32_t col, const char* fmt, ...);
uint8_t mcs_prim_of(const char* name, uint32_t len, uint8_t* conv);

#endif
#endif
