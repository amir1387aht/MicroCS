/* Shared real adapter allocation arithmetic; no TFT mock or hardware result. */
#include "mcs_tft_sprite_budget.h"
#include <limits.h>
#include <stdio.h>
static int checks, failures;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(expr)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL line %d: %s\n", __LINE__, #expr);                                         \
        }                                                                                          \
    } while (0)
int main(void) {
    size_t bytes = 123;
    CHECK(mcs_tft_sprite_allocation(1, 1, 16, &bytes) && bytes == 4);
    CHECK(mcs_tft_sprite_allocation(1, 1, 8, &bytes) && bytes == 2);
    CHECK(mcs_tft_sprite_allocation(4096, 4096, 16, &bytes) && bytes == 33554434);
    CHECK(mcs_tft_sprite_allocation(4096, 4096, 8, &bytes) && bytes == 16777217);
    const int bad[] = {INT_MIN, -1, 0, 4097, INT_MAX};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        bytes = 123;
        CHECK(!mcs_tft_sprite_allocation(bad[i], 8, 16, &bytes) && bytes == 123);
        CHECK(!mcs_tft_sprite_allocation(8, bad[i], 16, &bytes) && bytes == 123);
    }
    for (int depth = -1; depth <= 33; depth++) {
        if (depth == 8 || depth == 16)
            continue;
        bytes = 123;
        CHECK(!mcs_tft_sprite_allocation(3, 5, depth, &bytes) && bytes == 123);
    }
    CHECK(!mcs_tft_sprite_allocation(3, 5, 16, NULL));
    for (int w = 1; w <= 4096; w += 7) {
        for (int h = 1; h <= 4096; h += 31) {
            CHECK(mcs_tft_sprite_allocation(w, h, 8, &bytes) && bytes == (size_t)w * h + 1);
            CHECK(mcs_tft_sprite_allocation(w, h, 16, &bytes) && bytes == ((size_t)w * h + 1) * 2);
        }
    }
    printf("%d/%d sprite allocation arithmetic checks passed (not native/hardware execution)\n",
           checks - failures, checks);
    return failures != 0;
}
