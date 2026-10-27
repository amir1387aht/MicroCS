/* Native allocator owns all physical regions. Never use raw address probing. */
#include "mcs_esp32_heap.h"
#include "esp_heap_caps.h"

#define VM_CAPS (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)
static void* acquire(void* ud, size_t minimum, size_t preferred, size_t* actual) {
    mcs_esp32_heap_t* h = (mcs_esp32_heap_t*)ud;
    size_t total = heap_caps_get_free_size(VM_CAPS);
    size_t largest = heap_caps_get_largest_free_block(VM_CAPS);
    size_t available = total > h->reserve ? total - h->reserve : 0;
    /* Retain a contiguous native allocation margin as well as total headroom. */
    size_t contiguous = largest > 8192 ? largest - 8192 : 0;
    size_t n = preferred;
    if (n > available) n = available;
    if (n > contiguous) n = contiguous;
    if (n < minimum) return NULL;
    for (;;) {
        void* p = heap_caps_malloc(n, VM_CAPS);
        if (p) {
            /* Account for IDF allocator overhead and races since the sample. */
            if (heap_caps_get_free_size(VM_CAPS) >= h->reserve) {
                *actual = n;
                return p;
            }
            heap_caps_free(p); /* retry smaller after SDK overhead/race */
        }
        if (n == minimum) return NULL;
        n = n / 2 < minimum ? minimum : n / 2;
    }
}
static void release(void* ud, void* block, size_t bytes) {
    (void)ud; (void)bytes;
    heap_caps_free(block);
}
void mcs_esp32_heap_init(mcs_esp32_heap_t* heap, size_t reserve) {
    heap->reserve = reserve;
    heap->provider.acquire = acquire;
    heap->provider.release = release;
    heap->provider.ud = heap;
}
