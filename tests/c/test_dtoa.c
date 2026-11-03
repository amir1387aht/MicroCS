/* Compare the optional exact double formatter against the host libc implementation.
 * Input includes fixed edge values and deterministic binary64 bit patterns. */
#include "mcs.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void mcs_format_float(char* out, mcs_float_t f); /* internal formatter */

int main(void) {
    if (sizeof(double) != 8 || DBL_MANT_DIG != 53 || FLT_RADIX != 2) {
        fprintf(stderr, "test_dtoa requires binary64 doubles\n"); return 1;
    }
    const double edge[] = { 0.0, -0.0, 1.0 / 3.0, -1.0 / 3.0,
        1.2345678901234567, DBL_MIN, DBL_MAX, 0x1p-1074, 1e-300, 1e300 };
    char out[64];
    for (size_t i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        mcs_format_float(out, edge[i]); puts(out);
    }
    uint64_t state = UINT64_C(0x243f6a8885a308d3);
    for (int i = 0; i < 20000; i++) {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        double f; memcpy(&f, &state, sizeof f);
        if (isfinite(f)) { mcs_format_float(out, f); puts(out); }
    }
    return 0;
}
