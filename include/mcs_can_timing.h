/* Integer-only nominal bit timing search, shared by STM32 and MCP2515. */
#ifndef MCS_CAN_TIMING_H
#define MCS_CAN_TIMING_H
#include <stdint.h>
typedef struct { uint16_t prescaler, seg1, seg2; } mcs_can_timing_t;
static inline int mcs_can_timing(uint32_t clock, uint32_t rate, unsigned max_prescaler,
                                 unsigned max_seg1, unsigned max_seg2, unsigned min_seg2,
                                 mcs_can_timing_t* out) {
    if (!clock || !rate || rate > 1000000) return 0;
    unsigned best_tq = 0, best = 1001, target = rate > 500000 ? 800 : 875;
    int found = 0;
    for (unsigned tq = 8; tq <= 1 + max_seg1 + max_seg2; tq++) {
        uint64_t divisor = (uint64_t)rate * tq;
        if (clock % divisor) continue;           /* never silently approximate bitrate */
        unsigned pre = (unsigned)(clock / divisor);
        if (!pre || pre > max_prescaler) continue;
        for (unsigned s2 = min_seg2; s2 <= max_seg2 && s2 + 1 < tq; s2++) {
            unsigned s1 = tq - s2 - 1;
            if (s1 < 2 || s1 > max_seg1) continue;
            unsigned sp = (1 + s1) * 1000 / tq;
            unsigned error = sp > target ? sp - target : target - sp;
            if (!found || error < best || (error == best && tq > best_tq)) {
                out->prescaler = (uint16_t)pre; out->seg1 = (uint16_t)s1; out->seg2 = (uint16_t)s2;
                found = 1; best = error; best_tq = tq;
            }
        }
    }
    return found;
}
#endif
