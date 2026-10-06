/* MicroCS - small snprintf/vsnprintf for builds without the C library's
 * formatted output (MCS_TINY_PRINTF=1, see mcs_config.h).
 *
 * Supports what the VM itself formats: %d %i %u %x %X %c %s %p %%, the flags
 * '-' and '0', a width and a precision (numbers or '*'), and the length
 * modifiers l, z and h. Floating-point conversions (%e %f %g) are forwarded to
 * the C library when MCS_ENABLE_FLOAT=1, so the full test suite runs with this
 * formatter too; float-free builds never need them. */
#define MCS_FMT_IMPL
#include "mcs_internal.h"

#if MCS_TINY_PRINTF
#include <stdarg.h>
#include <string.h>
#if MCS_ENABLE_FLOAT
#include <stdio.h>
#endif

typedef struct { char* p; size_t cap, n; } fout_t;
static void put(fout_t* o, char c) { if (o->n + 1 < o->cap) o->p[o->n] = c; o->n++; }
static void pad(fout_t* o, char c, int k) { while (k-- > 0) put(o, c); }
static void emit(fout_t* o, const char* s, size_t len, int width, bool left, char padc) {
    int k = width > (int)len ? width - (int)len : 0;
    if (!left) pad(o, padc, k);
    for (size_t i = 0; i < len; i++) put(o, s[i]);
    if (left) pad(o, ' ', k);
}

int mcs_vsnprintf(char* buf, size_t cap, const char* fmt, va_list ap) {
    fout_t o = { buf, cap, 0 };
    for (const char* f = fmt; *f; f++) {
        if (*f != '%') { put(&o, *f); continue; }
        const char* spec = f++;
        bool left = false, zero = false;
        for (;; f++) {
            if (*f == '-') left = true;
            else if (*f == '0') zero = true;
            else if (*f == '+' || *f == ' ' || *f == '#') { /* accepted, not used by the VM */ }
            else break;
        }
        int width = 0, prec = -1;
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = true; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        char len = 0;
        while (*f == 'l' || *f == 'z' || *f == 'h') len = *f++;
        char tmp[24];
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            unsigned long v; bool neg = false;
            if (*f == 'p') v = (unsigned long)(uintptr_t)va_arg(ap, void*);
            else if (*f == 'd' || *f == 'i') {
                long sv = len == 'l' ? va_arg(ap, long) : len == 'z' ? (long)va_arg(ap, size_t) : (long)va_arg(ap, int);
                neg = sv < 0; v = neg ? 0ul - (unsigned long)sv : (unsigned long)sv;
            } else v = len == 'l' ? va_arg(ap, unsigned long) : len == 'z' ? (unsigned long)va_arg(ap, size_t) : (unsigned long)va_arg(ap, unsigned);
            unsigned base = (*f == 'd' || *f == 'i' || *f == 'u') ? 10u : 16u;
            const char* dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            int i = (int)sizeof tmp;
            do { tmp[--i] = dig[v % base]; v /= base; } while (v);
            while (prec > (int)sizeof tmp - i && i > 2) tmp[--i] = '0';
            if (*f == 'p') { tmp[--i] = 'x'; tmp[--i] = '0'; }
            size_t n = sizeof tmp - (size_t)i;
            if (neg) {
                if (zero && !left && prec < 0) { put(&o, '-'); emit(&o, tmp + i, n, width - 1, false, '0'); break; }
                tmp[--i] = '-'; n++;
            }
            emit(&o, tmp + i, n, width, left, zero && !left && prec < 0 ? '0' : ' ');
            break;
        }
        case 'c': tmp[0] = (char)va_arg(ap, int); emit(&o, tmp, 1, width, left, ' '); break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            size_t n = 0;
            while ((prec < 0 || n < (size_t)prec) && s[n]) n++;
            emit(&o, s, n, width, left, ' ');
            break;
        }
        case '%': put(&o, '%'); break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
#if MCS_ENABLE_FLOAT
            char sub[12], fb[64];
            /* rebuild the spec with width/precision passed as arguments */
            int k = 0; sub[k++] = '%';
            if (left) sub[k++] = '-';
            if (zero) sub[k++] = '0';
            sub[k++] = '*'; sub[k++] = '.'; sub[k++] = '*'; sub[k++] = *f; sub[k] = 0;
            double d = va_arg(ap, double);
            int r = snprintf(fb, sizeof fb, sub, width, prec < 0 ? 6 : prec, d);
            if (r > 0) for (int j = 0; j < r && j < (int)sizeof fb - 1; j++) put(&o, fb[j]);
#else
            (void)va_arg(ap, double);
            put(&o, '?');
#endif
            break;
        }
        default:   /* unknown conversion: copy it literally */
            for (const char* q = spec; q <= f && *q; q++) put(&o, *q);
            if (!*f) f--;
            break;
        }
    }
    if (o.cap) o.p[o.n < o.cap ? o.n : o.cap - 1] = 0;
    return (int)o.n;
}

int mcs_snprintf(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = mcs_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
#else
typedef int mcs_fmt_unused_t; /* ISO C: no empty translation unit */
#endif
