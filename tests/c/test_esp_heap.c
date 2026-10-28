/* Execute the production ESP32 provider with a deterministic SDK allocator. */
#include "mcs_esp32_heap.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static size_t available=284864,largest=147456,allocated,requested,overhead;
static unsigned calls;static int fail,drain;static size_t fail_above;
static unsigned caps_expected=MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL;
size_t heap_caps_get_free_size(unsigned caps){assert(caps==caps_expected);return available;}
size_t heap_caps_get_largest_free_block(unsigned caps){assert(caps==caps_expected);return largest;}
void* heap_caps_malloc(size_t n,unsigned caps){
    assert(caps==caps_expected);calls++;requested=n;
    if(fail || (fail_above && n>fail_above) || n+overhead>available)return NULL;
    void* p=malloc(n);if(!p)return NULL;allocated=n+overhead;available-=allocated;
    if(drain)available=0;
    return p;
}
void heap_caps_free(void* p){free(p);available+=allocated;allocated=0;}
int main(void){
    mcs_esp32_heap_t h;mcs_esp32_heap_init(&h,32768);size_t actual=0;
    mcs_pool_headroom_t info; unsigned before_calls=calls;
    mcs_esp32_heap_headroom(h.provider.ud,&info);
    assert(info.native_free==available && info.reserve==32768);
    assert(info.available==available-32768 && info.largest==largest-8192);
    assert(calls==before_calls && !allocated); /* reporting never allocates */
    void* p=h.provider.acquire(h.provider.ud,4096,196608,&actual);
    assert(p&&actual==139264);assert(available>=h.reserve);h.provider.release(h.provider.ud,p,actual);
    /* Fragmentation clamps size, then later calls can acquire a different block. */
    largest=114688;p=h.provider.acquire(h.provider.ud,4096,57344,&actual);
    assert(p&&actual==57344);h.provider.release(h.provider.ud,p,actual);
    available=35000;largest=114688;calls=0;
    assert(!h.provider.acquire(h.provider.ud,4096,16384,&actual));assert(calls==0);
    mcs_esp32_heap_headroom(h.provider.ud,&info);assert(info.available==2232);
    available=10000;mcs_esp32_heap_headroom(h.provider.ud,&info);assert(!info.available);
    available=65536;largest=8192;mcs_esp32_heap_headroom(h.provider.ud,&info);assert(!info.largest);
    available=65536;largest=8192;assert(!h.provider.acquire(h.provider.ud,64,16384,&actual));
    available=65536;largest=65536;fail=1;calls=0;
    assert(!h.provider.acquire(h.provider.ud,4096,16384,&actual));assert(calls==3&&requested==4096);
    fail=0;fail_above=8192;calls=0;
    p=h.provider.acquire(h.provider.ud,4096,16384,&actual);
    assert(p&&actual==8192&&calls==2);h.provider.release(h.provider.ud,p,actual);fail_above=0;
    available=32768+16384;largest=65536;overhead=16;calls=0;
    p=h.provider.acquire(h.provider.ud,4096,16384,&actual);
    assert(p&&actual==8192&&calls==2);h.provider.release(h.provider.ud,p,actual);
    available=32768+4096;largest=65536;overhead=16;
    assert(!h.provider.acquire(h.provider.ud,4096,4096,&actual));assert(allocated==0); /* rollback */
    overhead=0;available=65536;drain=1;
    assert(!h.provider.acquire(h.provider.ud,4096,8192,&actual));assert(allocated==0);
    puts("ESP32 provider: reserve, contiguous margin, split request, fallback and post-allocation rollback PASS");return 0;
}
