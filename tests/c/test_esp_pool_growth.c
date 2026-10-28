/* Real pool + production ESP32 provider; deterministic, multi-block SDK mock.
 * This models reserve accounting, not ESP-IDF fragmentation or board timing. */
#include "mcs.h"
#include "mcs_esp32_heap.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { void* ptr; size_t size; } block_t;
static block_t blocks[32];
static size_t total, largest_initial, live;
static unsigned active, attempts, releases;
static int fail_allocations;
static void caps_check(unsigned caps) { assert(caps==(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL)); }
size_t heap_caps_get_free_size(unsigned caps) { caps_check(caps); return total-live; }
size_t heap_caps_get_largest_free_block(unsigned caps) {
    caps_check(caps); return largest_initial>live ? largest_initial-live : 0;
}
void* heap_caps_malloc(size_t n,unsigned caps) {
    caps_check(caps);attempts++;
    if(fail_allocations)return NULL;
    if(n>total-live || n>heap_caps_get_largest_free_block(caps))return NULL;
    for(unsigned i=0;i<32;i++)if(!blocks[i].ptr) {
        void* p=malloc(n);if(!p)return NULL;
        blocks[i]=(block_t){p,n};live+=n;active++;return p;
    }
    return NULL;
}
void heap_caps_free(void* p) {
    for(unsigned i=0;i<32;i++)if(blocks[i].ptr==p) {
        live-=blocks[i].size;active--;releases++;free(p);blocks[i]=(block_t){0};return;
    }
    assert(!"unknown/double free");
}
static void reset(size_t free_bytes,size_t largest) {
    assert(!live && !active);total=free_bytes;largest_initial=largest;
    attempts=releases=0;fail_allocations=0;
}
static void sufficient_headroom(void) {
    reset(256*1024,192*1024);
    mcs_esp32_heap_t source;mcs_esp32_heap_init(&source,80*1024);
    mcs_pool_t pool;mcs_pool_dynamic_t state;
    const size_t cap=74*1024;
    assert(mcs_pool_init_dynamic(&pool,&state,&source.provider,cap,0));
    assert(!live && !state.committed);
    void* p[32]={0};size_t count=0;
    while(count<32 && (p[count]=mcs_pool_realloc(&pool,NULL,0,8000))) {
        memset(p[count],(int)count+1,8000);count++;
        assert(state.committed==live && live<=cap && total-live>=source.reserve);
    }
    assert(count>2 && count<32 && state.count>1 && state.committed>16*1024);
    /* Small allocations can consume the final partial region, without exceeding cap. */
    void* small[128]={0};size_t n=0;
    while(n<128 && (small[n]=mcs_pool_realloc(&pool,NULL,0,512)))n++;
    assert(n<128 && state.committed<=cap && state.committed>=cap-1024);
    for(size_t i=0;i<count;i++) {
        for(size_t j=0;j<8000;j++)assert(((unsigned char*)p[i])[j]==i+1);
        mcs_pool_realloc(&pool,p[i],8000,0);
    }
    for(size_t i=0;i<n;i++)mcs_pool_realloc(&pool,small[i],512,0);
    mcs_pool_destroy(&pool);assert(!live && !active);
}
static void reported_board_headroom(void) {
    /* After the initial 16 KiB region: free=83848, largest=79872. */
    reset(83848+16384,79872+16384);
    mcs_esp32_heap_t source;mcs_esp32_heap_init(&source,80*1024);
    mcs_pool_t pool;mcs_pool_dynamic_t state;
    assert(mcs_pool_init_dynamic(&pool,&state,&source.provider,74*1024,0));
    void* a=mcs_pool_realloc(&pool,NULL,0,9000);assert(a);memset(a,0x5a,9000);
    assert(state.committed==16384 && total-live==83848);
    void* b=mcs_pool_realloc(&pool,NULL,0,6000);assert(b);memset(b,0xa5,6000);
    assert(!mcs_pool_realloc(&pool,NULL,0,4096));
    assert(state.count==1 && state.committed==16384 && total-live==83848);
    for(size_t i=0;i<9000;i++)assert(((unsigned char*)a)[i]==0x5a);
    for(size_t i=0;i<6000;i++)assert(((unsigned char*)b)[i]==0xa5);
    mcs_pool_realloc(&pool,a,9000,0);mcs_pool_realloc(&pool,b,6000,0);
    mcs_pool_destroy(&pool);assert(!live && !active);
}

static void fragmented_recovery(int temporary,int force_failure) {
    /* Reproduce four retained backing regions totalling 58,332 B. The final
     * region is clamped by the 41 KiB native reserve, as on the reported C2.
     * Mock largest-block recovery is idealized, not a hardware guarantee. */
    const size_t reserve=41*1024;
    reset(reserve+58332,reserve+58332-4012);
    mcs_esp32_heap_t source;mcs_esp32_heap_init(&source,reserve);
    mcs_pool_t pool;mcs_pool_dynamic_t state;
    assert(mcs_pool_init_dynamic(&pool,&state,&source.provider,96*1024,0));
    void* keep=mcs_pool_realloc(&pool,NULL,0,9000);assert(keep);memset(keep,0x5a,9000);
    void* fill=mcs_pool_realloc(&pool,NULL,0,6000);assert(fill);
    void* scratch[3];
    scratch[0]=mcs_pool_realloc(&pool,NULL,0,14000);
    scratch[1]=mcs_pool_realloc(&pool,NULL,0,14000);
    scratch[2]=mcs_pool_realloc(&pool,NULL,0,8000);
    assert(scratch[0] && scratch[1] && scratch[2]);
    assert(state.count==4 && state.committed==58332);
    mcs_pool_realloc(&pool,fill,6000,0);
    mcs_pool_realloc(&pool,scratch[0],14000,0);
    mcs_pool_realloc(&pool,scratch[1],14000,0);
    mcs_pool_realloc(&pool,scratch[2],8000,0);
    mcs_heap_stats_t st;mcs_pool_stats(&pool,&st);
    assert(st.free>48000 && st.largest_free<16384);
    unsigned before_attempts=attempts;
    /* Native allocations outside the pool may consume headroom independently. */
    total-=84;largest_initial-=84;
    assert(heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL)==41900);
    fail_allocations=force_failure;
    void* large=(temporary ? mcs_pool_realloc_temporary : mcs_pool_realloc)(&pool,NULL,0,32*1024);
    assert(releases==3 && attempts>before_attempts);
    assert(state.committed==live && live<=96*1024);
    if(force_failure)assert(!large && state.count==1);
    else {
        assert(large && state.count==2);
        assert(total-live>=reserve);
        memset(large,0xa5,32*1024);
        for(size_t i=0;i<32*1024;i++)assert(((unsigned char*)large)[i]==0xa5);
    }
    for(size_t i=0;i<9000;i++)assert(((unsigned char*)keep)[i]==0x5a);
    if(large)mcs_pool_realloc(&pool,large,32*1024,0);
    /* Recovered or failed, the surviving original region remains usable. */
    void* small=mcs_pool_realloc(&pool,NULL,0,2048);assert(small);
    mcs_pool_realloc(&pool,small,2048,0);mcs_pool_realloc(&pool,keep,9000,0);
    mcs_pool_destroy(&pool);assert(!live && !active);
}

int main(void) {
    sufficient_headroom();reported_board_headroom();
    fragmented_recovery(0,0);fragmented_recovery(1,0);
    fragmented_recovery(0,1);fragmented_recovery(1,1);
    puts("ESP32 pool growth: 74 KiB ceiling, multiple regions, native reserve OOM, empty-region recovery, failed recovery, data retention and teardown PASS");
    return 0;
}
