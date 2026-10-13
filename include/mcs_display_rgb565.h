/* Bounded synchronous RGB565 transport shared by real TFT adapter and tests.
 * Not a display driver and never evidence that hardware is connected. */
#ifndef MCS_DISPLAY_RGB565_H
#define MCS_DISPLAY_RGB565_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* sink consumes n native-endian RGB565 words immediately. No retained pointer.
 * Return 0 on consumption, negative on failure. At most 64 words per callback. */
typedef int (*mcs_rgb565_sink_fn)(void *ud, int x, int y, int n, uint16_t *pixels);
int mcs_rgb565_transfer(int display_width, int display_height, int x, int y, int width, int height,
                        const void *pixels, size_t stride, size_t bytes, mcs_rgb565_sink_fn sink,
                        void *ud);
#ifdef __cplusplus
}
#endif
#endif
