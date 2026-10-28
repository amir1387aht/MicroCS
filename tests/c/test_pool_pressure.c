/* Production pool + ESP32 provider over a bounded, coalescing native arena.
 * The arena, oracle and pointer tables are host test instrumentation, not a
 * model of firmware static RAM cost. No SDK malloc uses the host heap. */
#include "mcs.h"
#include "mcs_esp32_heap.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ARENA_SIZE (192u*1024u)
#define MAX_SEGMENTS 2048
#define GUARD 16u
#define CAPS (MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL)
static union { long double align; uint8_t data[ARENA_SIZE]; } arena;
typedef struct { size_t offset,size,request; int used; } segment_t;
static segment_t seg[MAX_SEGMENTS];
static unsigned count,calls,fail_at,native_live,releases;
static size_t native_peak;
static uint32_t rng=0xfedc1234u;
static unsigned next_random(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static size_t rounded(size_t n) { return (n+15u)&~(size_t)15u; }
static void guards(void) {
    size_t position=0;
    for(unsigned i=0;i<count;i++) {
        assert(seg[i].offset==position && seg[i].size);position+=seg[i].size;
        if(seg[i].used)for(unsigned j=0;j<GUARD;j++) {
            assert(arena.data[seg[i].offset+j]==0xa7);
            assert(arena.data[seg[i].offset+GUARD+seg[i].request+j]==0x7a);
        }
    }
    assert(position==ARENA_SIZE);
}
static void reset(void) {
    assert(!native_live);count=1;seg[0]=(segment_t){0,ARENA_SIZE,0,0};
    calls=fail_at=releases=0;native_peak=0;memset(arena.data,0xdd,sizeof arena.data);
}
size_t heap_caps_get_free_size(unsigned caps) {
    assert(caps==CAPS);size_t n=0;for(unsigned i=0;i<count;i++)if(!seg[i].used)n+=seg[i].size;return n;
}
size_t heap_caps_get_largest_free_block(unsigned caps) {
    assert(caps==CAPS);size_t n=0;for(unsigned i=0;i<count;i++)if(!seg[i].used&&seg[i].size>n)n=seg[i].size;return n;
}
void* heap_caps_malloc(size_t n,unsigned caps) {
    assert(caps==CAPS);calls++;if(calls==fail_at)return NULL;
    assert(n<=SIZE_MAX-2*GUARD-15);size_t charge=rounded(n+2*GUARD);
    for(unsigned i=0;i<count;i++)if(!seg[i].used && seg[i].size>=charge) {
        if(seg[i].size>charge) {
            assert(count<MAX_SEGMENTS);memmove(seg+i+2,seg+i+1,(count-i-1)*sizeof *seg);
            seg[i+1]=(segment_t){seg[i].offset+charge,seg[i].size-charge,0,0};count++;
        }
        seg[i].size=charge;seg[i].used=1;seg[i].request=n;native_live++;
        memset(arena.data+seg[i].offset,0xa7,GUARD);
        memset(arena.data+seg[i].offset+GUARD+n,0x7a,GUARD);
        size_t used=ARENA_SIZE-heap_caps_get_free_size(CAPS);if(used>native_peak)native_peak=used;
        return arena.data+seg[i].offset+GUARD;
    }
    return NULL;
}
void heap_caps_free(void* p) {
    if(!p)return;
    guards();
    for(unsigned i=0;i<count;i++)if(seg[i].used && p==arena.data+seg[i].offset+GUARD) {
        memset(arena.data+seg[i].offset,0xdd,seg[i].size);
        seg[i].used=0;seg[i].request=0;native_live--;releases++;
        if(i+1<count && !seg[i+1].used) {
            seg[i].size+=seg[i+1].size;memmove(seg+i+1,seg+i+2,(count-i-2)*sizeof *seg);count--;
        }
        if(i && !seg[i-1].used) {
            seg[i-1].size+=seg[i].size;memmove(seg+i,seg+i+1,(count-i-1)*sizeof *seg);count--;
        }
        return;
    }
    assert(!"unknown/double native free");
}
static void invariant(mcs_pool_t* p,mcs_esp32_heap_t* provider,size_t cap) {
    guards();mcs_heap_stats_t st;mcs_pool_stats(p,&st);
    assert(st.used+st.free==st.total && st.total<=p->dynamic->committed);
    assert(st.largest_free<=st.free && p->dynamic->committed<=cap);
    assert(heap_caps_get_free_size(CAPS)>=provider->reserve);
}
static void teardown(mcs_pool_t* p) {
    mcs_pool_destroy(p);guards();assert(!native_live && count==1);
    assert(heap_caps_get_free_size(CAPS)==ARENA_SIZE && heap_caps_get_largest_free_block(CAPS)==ARENA_SIZE);
}
static void exhaust_usable_pool(void) {
    reset();mcs_pool_t p;mcs_pool_dynamic_t d;mcs_esp32_heap_t h;
    mcs_esp32_heap_init(&h,32*1024);const size_t cap=64*1024;
    assert(mcs_pool_init_dynamic(&p,&d,&h.provider,cap,0));
    void* pointers[8192];unsigned n=0;
    while(n<8192 && (pointers[n]=mcs_pool_realloc(&p,NULL,0,1))) {
        *(uint8_t*)pointers[n]=(uint8_t)n;n++;
    }
    assert(n<8192 && n>1000);invariant(&p,&h,cap);
    mcs_heap_stats_t st;mcs_pool_stats(&p,&st);
    /* Every usable pool byte is occupied (including allocation headers), not
     * a false OOM caused by stranded empty backing regions. */
    assert(d.committed==cap && st.free==0 && st.used==st.total);
    assert(!mcs_pool_realloc(&p,NULL,0,1));
    printf("Full usable pool: %u tiny allocations, %u/%u pool bytes occupied, %u backing bytes (headers included)\n",n,(unsigned)st.used,(unsigned)st.total,(unsigned)d.committed);
    for(unsigned i=0;i<n;i++){assert(*(uint8_t*)pointers[i]==(uint8_t)i);mcs_pool_realloc(&p,pointers[i],1,0);}
    teardown(&p);
}
static void native_fragmentation(int barriers,int fail_recovery) {
    reset();mcs_pool_t p;mcs_pool_dynamic_t d;mcs_esp32_heap_t h;
    mcs_esp32_heap_init(&h,24*1024);const size_t cap=64*1024;
    assert(mcs_pool_init_dynamic(&p,&d,&h.provider,cap,0));
    void* keep=mcs_pool_realloc(&p,NULL,0,9000);assert(keep);memset(keep,0x59,9000);
    void* fill=mcs_pool_realloc(&p,NULL,0,6000);assert(fill);
    void* a[3];void* gap[3]={0};
    for(unsigned i=0;i<3;i++) {
        if(barriers){gap[i]=heap_caps_malloc(2048,CAPS);assert(gap[i]);memset(gap[i],0xb4,2048);}
        a[i]=mcs_pool_realloc(&p,NULL,0,14000);assert(a[i]);
    }
    assert(d.count==4 && d.committed==cap);
    /* Occupy the tail independently, leaving native free fragmented after
     * empty regions are returned. With no barriers it can coalesce again. */
    size_t tail_bytes=heap_caps_get_largest_free_block(CAPS)-8192-2*GUARD;
    void* tail=heap_caps_malloc(tail_bytes,CAPS);assert(tail);memset(tail,0xc1,tail_bytes);
    mcs_pool_realloc(&p,fill,6000,0);
    for(unsigned i=0;i<3;i++)mcs_pool_realloc(&p,a[i],14000,0);
    if(fail_recovery)fail_at=calls+1;
    unsigned old_calls=calls;
    void* large=mcs_pool_realloc_temporary(&p,NULL,0,30000);
    assert(releases==3);
    if(barriers || fail_recovery) {
        assert(!large && d.count==1);
        if(barriers)assert(heap_caps_get_largest_free_block(CAPS)<30000);
    } else {assert(large && d.count==2);memset(large,0x88,30000);}
    assert(calls-old_calls<=1);invariant(&p,&h,cap);
    for(unsigned i=0;i<9000;i++)assert(((uint8_t*)keep)[i]==0x59);
    for(unsigned i=0;i<3;i++)if(gap[i]) {
        for(unsigned j=0;j<2048;j++)assert(((uint8_t*)gap[i])[j]==0xb4);
        heap_caps_free(gap[i]);
    }
    for(size_t i=0;i<tail_bytes;i++)assert(((uint8_t*)tail)[i]==0xc1);
    heap_caps_free(tail);
    if(large)mcs_pool_realloc(&p,large,30000,0);
    mcs_pool_realloc(&p,keep,9000,0);teardown(&p);
}
static void randomized_pressure(void) {
    reset();mcs_pool_t p;mcs_pool_dynamic_t d;mcs_esp32_heap_t h;
    const size_t cap=80*1024;mcs_esp32_heap_init(&h,32*1024);
    assert(mcs_pool_init_dynamic(&p,&d,&h.provider,cap,0));
    void* pointers[48]={0};size_t lengths[48]={0};uint8_t marks[48]={0};
    for(unsigned iteration=0;iteration<12000;iteration++) {
        unsigned i=next_random()%48;size_t n=next_random()%9000;
        if(iteration%7==0)n=0;
        if(pointers[i])for(size_t j=0;j<lengths[i];j++)assert(((uint8_t*)pointers[i])[j]==marks[i]);
        fail_at=iteration%31==0 ? calls+1 : 0;
        void* q=(iteration&1?mcs_pool_realloc:mcs_pool_realloc_temporary)(&p,pointers[i],lengths[i],n);
        if(!n){pointers[i]=NULL;lengths[i]=0;}
        else if(q) {
            if(pointers[i])for(size_t j=0;j<(n<lengths[i]?n:lengths[i]);j++)assert(((uint8_t*)q)[j]==marks[i]);
            pointers[i]=q;lengths[i]=n;marks[i]=(uint8_t)(next_random()|1);memset(q,marks[i],n);
        } else if(pointers[i])for(size_t j=0;j<lengths[i];j++)assert(((uint8_t*)pointers[i])[j]==marks[i]);
        invariant(&p,&h,cap);
    }
    for(unsigned i=0;i<48;i++) {
        if(pointers[i])for(size_t j=0;j<lengths[i];j++)assert(((uint8_t*)pointers[i])[j]==marks[i]);
        mcs_pool_realloc(&p,pointers[i],lengths[i],0);
    }
    teardown(&p);
}
int main(void) {
    exhaust_usable_pool();native_fragmentation(0,0);native_fragmentation(0,1);native_fragmentation(1,0);
    for(unsigned seed=0;seed<4;seed++){rng=0xfedc1234u^(seed*0x9e3779b9u);randomized_pressure();}
    puts("Strict pool pressure: full capacity, real native coalescing/fragmentation, canaries, injected failure, 48000 randomized operations, realloc preservation, reserve/cap and complete teardown PASS");
    return 0;
}
