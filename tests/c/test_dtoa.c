/* Compare the optional exact double formatter against the host libc implementation.
 * Input includes fixed edge values and deterministic binary64 bit patterns. */
#include "../../src/mcs_internal.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


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
    /* Compare explicit formats too, including rounding boundaries and values
     * whose exact decimal expansion has far more than 15 significant digits. */
    mcs_vm_t* vm = mcs_new(NULL);
    if (!vm) return 1;
    const char* fmt[] = { "F0", "F2", "F16", "F20", "N16", "P16", "E0", "E2", "E16", "E20", "e16", "0.####################" };
    const double corners[] = { 0, -0.0, 0.5, 1.5, 2.5, -0.5, 0.125, 9.9999,
        1.0 / 3.0, 0x1p-1074, DBL_MIN, DBL_MAX, 1e-300, 1e300 };
    state = UINT64_C(0x13198a2e03707344);
    for (int i = 0; i < 1024 + (int)(sizeof corners / sizeof corners[0]); i++) {
        double f;
        if (i < (int)(sizeof corners / sizeof corners[0])) f = corners[i];
        else { state ^= state << 13; state ^= state >> 7; state ^= state << 17; memcpy(&f, &state, sizeof f); }
        if (!isfinite(f)) continue;
        for (size_t j = 0; j < sizeof fmt / sizeof fmt[0]; j++) {
            mcs_buf_t b; mcs_buf_init(&b, vm);
            if (!mcs_format_spec(vm, &b, mcs_float(f), fmt[j], strlen(fmt[j]))) return 1;
            puts(b.data ? b.data : ""); mcs_buf_free(&b);
        }
    }
    mcs_free(vm);
    return 0;
}
