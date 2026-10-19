/* Pool properties and forced compiler OOM cleanup. Runs in normal + ASan CI. */
#include "mcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)
static uint8_t heap[256*1024];
static uint32_t rng = 42;
static uint32_t random32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
typedef struct blk { size_t size; struct blk* next; } blk_t;
static void validate(mcs_pool_t* p) {
    size_t free_bytes = 0; uintptr_t end = (uintptr_t)p->base;
    for (blk_t* b = p->free_list; b; b = b->next) {
        CHECK((uintptr_t)b >= end); CHECK(b->size >= sizeof *b);
        end = (uintptr_t)b + b->size; CHECK(end <= (uintptr_t)p->base + p->size);
        if (b->next) CHECK(end < (uintptr_t)b->next); /* adjacent blocks coalesce */
        free_bytes += b->size;
    }
    CHECK(free_bytes + p->used == p->size); CHECK(p->used <= p->peak && p->peak <= p->size);
}
static void pool_tests(void) {
    mcs_pool_t p; mcs_pool_init(&p,heap,sizeof heap);
    uint8_t* a = mcs_pool_realloc(&p,NULL,0,1024); CHECK(a != NULL); memset(a,0x5a,1024);
    size_t used = p.used;
    CHECK(mcs_pool_realloc(&p,a,1024,16)==a); CHECK(p.used < used);
    CHECK(mcs_pool_realloc(&p,a,16,2048)==a); CHECK(a[0]==0x5a);
    CHECK(!mcs_pool_realloc(&p,a,2048,SIZE_MAX)); CHECK(a[0]==0x5a);
    CHECK(!mcs_pool_realloc(&p,a,2048,sizeof heap)); CHECK(a[0]==0x5a);
    mcs_pool_realloc(&p,a,2048,0); CHECK(!p.used); validate(&p);
    struct { uint8_t* p; size_t n; uint8_t fill; } slots[64] = {{0}};
    for (int i=0;i<20000;i++) {
        unsigned k=random32()%64; size_t n=random32()%8192;
        for(size_t j=0;j<slots[k].n;j++) if(slots[k].p[j]!=slots[k].fill) { CHECK(0); break; }
        uint8_t* q=mcs_pool_realloc(&p,slots[k].p,slots[k].n,n);
        if(q || !n) {
            if(q) { CHECK((uintptr_t)q%MCS_POOL_ALIGN==0); for(size_t j=0;j<(n<slots[k].n?n:slots[k].n);j++) if(q[j]!=slots[k].fill) { CHECK(0); break; } }
            slots[k].p=q; slots[k].n=q?n:0; slots[k].fill=(uint8_t)k;
            if(q) memset(q,slots[k].fill,n);
        }
        validate(&p);
    }
    for(int k=0;k<64;k++) mcs_pool_realloc(&p,slots[k].p,slots[k].n,0);
    CHECK(!p.used); validate(&p);
    void* whole=mcs_pool_realloc(&p,NULL,0,p.size-MCS_POOL_OVERHEAD);
    CHECK(whole!=NULL); mcs_pool_realloc(&p,whole,p.size-MCS_POOL_OVERHEAD,0); CHECK(!p.used);
}
typedef struct { mcs_pool_t pool; int countdown; } alloc_t;
static void* alloc(void* ud, void* ptr, size_t old, size_t n) {
    alloc_t* a=ud;
    if(n>old && a->countdown>0 && --a->countdown==0) return NULL;
    return mcs_pool_realloc(&a->pool,ptr,old,n);
}
static void quiet(void* ud,const char* s,size_t n) { (void)ud;(void)s;(void)n; }
static void oom_tests(void) {
    alloc_t a={0}; mcs_pool_init(&a.pool,heap,sizeof heap);
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.realloc_fn=alloc;cfg.alloc_ud=&a;
    cfg.alloc_overhead=MCS_POOL_OVERHEAD;cfg.heap_limit=sizeof heap;cfg.write_fn=quiet;
    mcs_vm_t* vm=mcs_new(&cfg); CHECK(vm!=NULL); if(!vm)return;
    const char* src="class Box { int value; Box(int x) { value=x; } int Get() { Func<int> f=()=>value; return f(); } } var b=new Box(42);";
    CHECK(mcs_check_source(vm,"forced.cs",src)==MCS_OK); mcs_gc(vm);
    size_t baseline=a.pool.used; int injected=0;
    for(int site=1;site<90;site++) {
        a.countdown=site; mcs_result_t st=mcs_check_source(vm,"forced.cs",src);
        if(st==MCS_ERR_MEMORY)injected++;
        else CHECK(st==MCS_OK);
        a.countdown=0; mcs_gc(vm);
        CHECK(a.pool.used==baseline); validate(&a.pool);
    }
    CHECK(injected>10);
    CHECK(mcs_exec_source(vm,"recover.cs","int recovered=42;")==MCS_OK);
    mcs_free(vm); CHECK(a.pool.used==0);
    mcs_config_default(&cfg); cfg.write_fn=quiet; vm=mcs_new(&cfg); CHECK(vm!=NULL); if(!vm)return;
    char srcbuf[16384];
    for(int n=254;n<=255;n++) {
        size_t pos=(size_t)snprintf(srcbuf,sizeof srcbuf,"int Many() {");
        for(int i=0;i<n;i++) pos+=(size_t)snprintf(srcbuf+pos,sizeof srcbuf-pos,"int x%d=%d;",i,i);
        snprintf(srcbuf+pos,sizeof srcbuf-pos,"return x0; }");
        CHECK(mcs_check_source(vm,"locals.cs",srcbuf)==(n==254?MCS_OK:MCS_ERR_COMPILE));
    }
    CHECK(mcs_check_source(vm,"limit-recovery.cs","int fine=1;")==MCS_OK);
    mcs_free(vm);
}
int main(void) { pool_tests(); oom_tests(); printf("%s smallheap: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures); return failures!=0; }
