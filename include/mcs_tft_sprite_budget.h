/* Requested upstream callocSprite bytes, one frame, exact supported depths.
 * Does not include allocator metadata or the VM-accounted native object. */
#ifndef MCS_TFT_SPRITE_BUDGET_H
#define MCS_TFT_SPRITE_BUDGET_H
#include <stddef.h>
#include <stdint.h>
static inline int mcs_tft_sprite_allocation(int width, int height, int depth, size_t *bytes) {
    if (!bytes || width < 1 || width > 4096 || height < 1 || height > 4096 ||
        (depth != 8 && depth != 16))
        return 0;
    size_t w = (size_t)width, h = (size_t)height, unit = depth == 16 ? 2 : 1;
    if (w > (SIZE_MAX / unit - 1) / h)
        return 0;
    *bytes = (w * h + 1) * unit;
    return 1;
}
#endif
