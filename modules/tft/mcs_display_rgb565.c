#include "mcs_config.h"
#if MCS_ENABLE_TFT_ESPI || MCS_ENABLE_LVGL
#include "mcs_display_rgb565.h"
#include <string.h>
int mcs_rgb565_transfer(int dw, int dh, int x, int y, int w, int h, const void *pixels,
                        size_t stride, size_t bytes, mcs_rgb565_sink_fn sink, void *ud) {
    if (!pixels || !sink || dw < 1 || dh < 1 || dw > 4096 || dh > 4096 || w < 1 || h < 1 ||
        w > dw || h > dh || x < 0 || y < 0 || x > dw - w || y > dh - h || stride < (size_t)w * 2 ||
        stride > SIZE_MAX / (size_t)h || bytes != stride * (size_t)h)
        return -1;
    uint16_t chunk[64];
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col += 64) {
            int n = w - col < 64 ? w - col : 64;
            memcpy(chunk, (const unsigned char *)pixels + (size_t)row * stride + (size_t)col * 2,
                   (size_t)n * 2);
            int result = sink(ud, x + col, y + row, n, chunk);
            if (result < 0)
                return result;
        }
    }
    return 0;
}
#endif
