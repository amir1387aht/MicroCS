/* Automatic VM regions from ESP-IDF's internal byte-addressable heap. */
#ifndef MCS_ESP32_HEAP_H
#define MCS_ESP32_HEAP_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    mcs_pool_provider_t provider;
    size_t reserve; /* keep this much native heap free; includes deferred radios */
} mcs_esp32_heap_t;
/* Must outlive the runtime. Sampling headroom is not a reservation against
 * concurrent native allocations; size for worst-case driver loads. */
void mcs_esp32_heap_init(mcs_esp32_heap_t* heap, size_t reserve);
#ifdef __cplusplus
}
#endif
#endif
