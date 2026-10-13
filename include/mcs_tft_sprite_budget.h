/* Requested actual upstream native allocations: one frame/off-screen pixel,
 * plus the required32-byte palette for4bit. Not allocator/object overhead. */
#ifndef MCS_TFT_SPRITE_BUDGET_H
#define MCS_TFT_SPRITE_BUDGET_H
#include <stddef.h>
#include <stdint.h>
static inline int mcs_tft_sprite_allocation(int width, int height, int depth, size_t *bytes) {
    if (!bytes || width < 1 || width > 4096 || height < 1 || height > 4096)
        return 0;
    size_t w = (size_t)width, h = (size_t)height, row, extra;
    switch (depth) {
    case 16:
        if (w > SIZE_MAX / 2)
            return 0;
        row = w * 2;
        extra = 2;
        break;
    case 8:
        row = w;
        extra = 1;
        break;
    case 4:
        row = (w + 1) / 2;
        extra = 33;
        break;
    case 1:
        row = (w + 7) / 8;
        extra = 1;
        break;
    default:
        return 0;
    }
    if (row > (SIZE_MAX - extra) / h)
        return 0;
    *bytes = row * h + extra;
    return 1;
}
#endif
