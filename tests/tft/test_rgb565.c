/* Transport tests, NOT mocked TFT_eSPI or evidence of physical rendering. */
#include "mcs_display_rgb565.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
static int calls, seen, failures, fail_at;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                            \
        }                                                                                          \
    } while (0)
static int sink(void *ud, int x, int y, int n, uint16_t *pixels) {
    (void)ud;
    calls++;
    if (fail_at && calls == fail_at)
        return -7;
    CHECK(n > 0 && n <= 64);
    CHECK(y >= 3 && y < 6);
    CHECK(x >= 2 && x + n <= 132);
    for (int i = 0; i < n; i++)
        CHECK(pixels[i] == (uint16_t)((y - 3) * 130 + x - 2 + i));
    seen += n;
    memset(pixels, 0xFF, (size_t)n * 2); /* caller's input must remain unchanged */
    return 0;
}
int main(void) {
    unsigned char buf[3 * 264 + 1], copy[sizeof buf];
    memset(buf, 0xAD, sizeof buf);
    /* Unaligned input, 130-pixel rows, four padding bytes, three partial chunks. */
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 130; col++) {
            uint16_t value = (uint16_t)(row * 130 + col);
            memcpy(buf + 1 + row * 264 + col * 2, &value, 2);
        }
    memcpy(copy, buf, sizeof buf);
    CHECK(!mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, buf + 1, 264, 792, sink, NULL));
    CHECK(calls == 9 && seen == 390);
    CHECK(!memcmp(buf, copy, sizeof buf));
    calls = seen = 0;
    fail_at = 2;
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, buf + 1, 264, 792, sink, NULL) == -7);
    CHECK(calls == 2 && seen == 64);
    fail_at = 0;
    calls = 0;
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, buf + 1, 259, 777, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, buf + 1, 264, 791, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, 239, 3, 130, 3, buf + 1, 264, 792, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, -1, 3, 130, 3, buf + 1, 264, 792, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 0, 3, buf + 1, 264, 792, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, buf + 1, SIZE_MAX, SIZE_MAX, sink, NULL) ==
          -1);
    CHECK(mcs_rgb565_transfer(INT_MAX, 320, 2, 3, 130, 3, buf + 1, 264, 792, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, NULL, 264, 792, sink, NULL) == -1);
    CHECK(mcs_rgb565_transfer(240, 320, 2, 3, 130, 3, buf + 1, 264, 792, NULL, NULL) == -1);
    CHECK(!calls);
    printf("RGB565 transport: %d failures\n", failures);
    return failures != 0;
}
