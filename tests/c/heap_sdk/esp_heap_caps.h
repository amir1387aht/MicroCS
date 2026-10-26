#ifndef TEST_HEAP_CAPS_H
#define TEST_HEAP_CAPS_H
#include <stddef.h>
#define MALLOC_CAP_8BIT 4
#define MALLOC_CAP_INTERNAL 2048
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);
void* heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void* ptr);
#endif
