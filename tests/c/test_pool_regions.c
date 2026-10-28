/* Automatic regions: actual backing cap, ownership, OOM, relocation and runtime. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); failures++; } } while (0)
typedef struct { void* raw; void* allocation; size_t size; } slot_t;
typedef struct { slot_t slots[128]; size_t max_block, native_total, reserve, live, peak; unsigned calls, fail_at, count; } source_t;
static void* acquire(void* ud, size_t minimum, size_t preferred, size_t* actual) {
    source_t* s = ud;
    s->calls++;
    if (s->fail_at && s->calls == s->fail_at) return NULL;
    size_t n = preferred;
    if (n > s->max_block) n = s->max_block;
    size_t left = s->native_total - s->reserve - s->live;
    if (n > left) n = left;
    if (n < minimum) return NULL;
    for (unsigned i=0;i<128;i++) if (!s->slots[i].raw) {
        void* p = malloc(n+3);
        if (!p) return NULL;
        void* raw = (uint8_t*)p+3; /* adversarial provider alignment */
        s->slots[i] = (slot_t){raw,p,n};
        s->live += n; s->count++; if (s->live > s->peak) s->peak = s->live;
        *actual=n; return raw;
    }
    return NULL;
}
static void release(void* ud, void* p, size_t bytes) {
    source_t* s = ud;
    for (unsigned i=0;i<128;i++) if (s->slots[i].raw == p) {
        CHECK(bytes==s->slots[i].size); s->live-=bytes; s->count--;
        free(s->slots[i].allocation); memset(&s->slots[i],0,sizeof s->slots[i]); return;
    }
    CHECK(0); /* double/unknown free */
}
static void init_source(source_t* s) {
    memset(s,0,sizeof *s); s->max_block=24*1024; s->native_total=256*1024; s->reserve=32*1024;
}
static uint32_t rng=0xa8ce1234;
static unsigned random32(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static void invariant(mcs_pool_t* p, source_t* s, size_t cap) {
    mcs_heap_stats_t st; mcs_pool_stats(p,&st);
    CHECK(st.used+st.free==st.total); CHECK(st.total<=s->live);
    CHECK(p->dynamic->committed==s->live); CHECK(s->live<=cap);
    CHECK(p->dynamic->count==s->count); CHECK(st.largest_free<=st.free);
}
static void stress(void) {
    source_t s;init_source(&s);mcs_pool_provider_t provider={acquire,release,&s};
    mcs_pool_t p;mcs_pool_dynamic_t d;
    CHECK(mcs_pool_init_dynamic(&p,&d,&provider,64*1024,8192));
    CHECK(s.calls==0);
    void* blocks[48]={0};size_t lengths[48]={0};uint8_t marks[48]={0};
    for (unsigned iter=0;iter<40000;iter++) {
        unsigned i=random32()%48;
        size_t n=random32()%5000;
        if (random32()%4==0)n=0;
        if (blocks[i]) for(size_t j=0;j<lengths[i];j++) CHECK(((uint8_t*)blocks[i])[j]==marks[i]);
        void* q=(iter&1?mcs_pool_realloc:mcs_pool_realloc_temporary)(&p,blocks[i],lengths[i],n);
        if (!n) { blocks[i]=NULL;lengths[i]=0; }
        else if (q) {
            if(blocks[i])for(size_t j=0;j<(n<lengths[i]?n:lengths[i]);j++)CHECK(((uint8_t*)q)[j]==marks[i]);
            CHECK((uintptr_t)q % (sizeof(void*)>MCS_POOL_ALIGN?sizeof(void*):MCS_POOL_ALIGN)==0);
            blocks[i]=q;lengths[i]=n;marks[i]=(uint8_t)(random32()|1);memset(q,marks[i],n);
        } /* Failed realloc must preserve its original block. */
        invariant(&p,&s,64*1024);
        if(iter%97==0)mcs_pool_trim(&p);
    }
    CHECK(!mcs_pool_realloc(&p,NULL,0,SIZE_MAX));
    for(unsigned i=0;i<48;i++)mcs_pool_realloc(&p,blocks[i],lengths[i],0);
    CHECK(p.used==0);CHECK(s.count>1);CHECK(mcs_pool_trim(&p)>0);CHECK(s.live==0);CHECK(p.size==0);
    CHECK(mcs_pool_realloc(&p,NULL,0,100));mcs_pool_destroy(&p);CHECK(s.live==0);
}
/* Adjacent provider allocations must NEVER coalesce across ownership boundaries. */
static union { uint64_t align; uint8_t bytes[64*1024]; } adjacent;
static size_t adj_next;
static void* adj_acquire(void* ud,size_t minimum,size_t preferred,size_t* actual){
    (void)ud;(void)preferred;if(minimum>8192||adj_next+8192>sizeof adjacent.bytes)return NULL;
    void* p=adjacent.bytes+adj_next;adj_next+=8192;*actual=8192;return p;
}
static void adj_release(void* ud,void* p,size_t n){(void)ud;(void)p;CHECK(n==8192);}
static void boundaries(void){
    mcs_pool_t p;mcs_pool_dynamic_t d;mcs_pool_provider_t provider={adj_acquire,adj_release,NULL};
    CHECK(mcs_pool_init_dynamic(&p,&d,&provider,32*1024,8192));
    void* a=mcs_pool_realloc(&p,NULL,0,7000);void* b=mcs_pool_realloc(&p,NULL,0,7000);
    CHECK(a&&b);memset(a,0x4e,7000);
    CHECK(!mcs_pool_realloc(&p,a,7000,13000));CHECK(((uint8_t*)a)[6999]==0x4e);
    mcs_pool_realloc(&p,a,7000,0);mcs_pool_realloc(&p,b,7000,0);
    CHECK(!mcs_pool_realloc(&p,NULL,0,13000));mcs_pool_destroy(&p);
    uint8_t tiny[8];mcs_pool_init(&p,tiny+1,0);CHECK(p.size==0);CHECK(!mcs_pool_realloc(&p,NULL,0,20));
}
static void budget192(void) {
    source_t s;init_source(&s);s.native_total=284864;
    mcs_pool_t p;mcs_pool_dynamic_t d;mcs_pool_provider_t provider={acquire,release,&s};
    CHECK(mcs_pool_init_dynamic(&p,&d,&provider,192*1024,0));
    void* blocks[12];
    for(unsigned i=0;i<12;i++){blocks[i]=mcs_pool_realloc(&p,NULL,0,14000);CHECK(blocks[i]!=NULL);}
    CHECK(s.live==192*1024);CHECK(s.count==12);CHECK(s.native_total-s.live>=s.reserve);
    CHECK(!mcs_pool_realloc(&p,NULL,0,14000));
    for(unsigned i=0;i<12;i++)mcs_pool_realloc(&p,blocks[i],14000,0);
    mcs_pool_trim(&p);CHECK(s.live==0);mcs_pool_destroy(&p);
}
/* Backing ceiling (not native reserve) blocks growth until cached empty
 * regions are returned. Exercise moving realloc while its old region is live. */
static void recover_at_cap(void) {
    source_t s;init_source(&s);s.max_block=64*1024;
    mcs_pool_t p;mcs_pool_dynamic_t d;mcs_pool_provider_t provider={acquire,release,&s};
    CHECK(mcs_pool_init_dynamic(&p,&d,&provider,48*1024,0));
    void* keep=mcs_pool_realloc(&p,NULL,0,9000);CHECK(keep);if(!keep)return;
    memset(keep,0x6b,9000);
    void* fill=mcs_pool_realloc(&p,NULL,0,6000);
    void* a=mcs_pool_realloc(&p,NULL,0,14000);
    void* b=mcs_pool_realloc(&p,NULL,0,14000);
    CHECK(fill&&a&&b);CHECK(d.committed==48*1024&&d.count==3);
    mcs_pool_realloc(&p,fill,6000,0);mcs_pool_realloc(&p,a,14000,0);mcs_pool_realloc(&p,b,14000,0);
    void* moved=mcs_pool_realloc_temporary(&p,keep,9000,30000);
    CHECK(moved!=NULL);
    if(moved) {
        for(size_t i=0;i<9000;i++)CHECK(((uint8_t*)moved)[i]==0x6b);
        memset(moved,0x4c,30000);mcs_pool_realloc(&p,moved,30000,0);
    } else mcs_pool_realloc(&p,keep,9000,0);
    CHECK(d.count==2);invariant(&p,&s,48*1024);
    mcs_pool_destroy(&p);CHECK(!s.live&&!s.count);
}

static void quiet(void* ud,const char* s,size_t n){(void)ud;(void)s;(void)n;}
static char output[4096];static size_t used;
static void capture(void* ud,const char* s,size_t n){(void)ud;if(n>sizeof output-1-used)n=sizeof output-1-used;memcpy(output+used,s,n);used+=n;output[used]=0;}
static const char* input;
static int read_input(void* ud,uint8_t* dst,size_t n,uint32_t timeout){(void)ud;(void)timeout;size_t k=strlen(input);if(k>n)k=n;memcpy(dst,input,k);input+=k;return (int)k;}
static void headroom(void* ud,mcs_pool_headroom_t* h){
    source_t* s=ud;h->native_free=s->native_total-s->live;h->reserve=s->reserve;
    h->available=h->native_free>h->reserve?h->native_free-h->reserve:0;
    h->largest=s->max_block;
}
static void budget_checks(mcs_runtime_t* rt,source_t* s){
    mcs_shell_memory_t ms;unsigned calls=s->calls;size_t live=s->live,used_before=rt->pool.used;
    rt->shell.memory_info(rt->shell.memory_ud,&ms);
    CHECK(ms.has_budget && ms.has_pool);
    CHECK(ms.budget_total==ms.pool.used+ms.budget_free);
    CHECK(ms.budget_free<=ms.pool.free+ms.growth_estimate);
    CHECK(ms.growth_estimate<=ms.headroom.available);
    CHECK(ms.growth_estimate<=rt->pool.dynamic->limit-rt->pool.dynamic->committed);
    mcs_mem_stats_t tracked;mcs_mem_stats(rt->vm,&tracked);
    CHECK(!tracked.heap_limit || ms.budget_free<=tracked.heap_limit-tracked.bytes_in_use);
    CHECK(s->calls==calls && s->live==live && rt->pool.used==used_before);
}
static void runtime(void){
    source_t s;mcs_pool_provider_t provider={acquire,release,&s};
    mcs_runtime_t rt;mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;
    cfg.heap_size=192*1024;cfg.heap_provider=&provider;cfg.heap_headroom=headroom;cfg.ramfs_size=2048;
    cfg.thread_heap=48*1024; /* host default is 256 KiB, larger than this whole test cap */
    cfg.run_boot_scripts=false;cfg.mode=MCS_RUNTIME_SHELL;cfg.console=(mcs_transport_t){read_input,capture,NULL};
    init_source(&s);input="";used=0;
    CHECK(mcs_runtime_start(&rt,&cfg)==0);
    CHECK(mcs_exec_source(rt.vm,"first","File.WriteAllText(\"/keep\",\"yes\"); var x = new List<int>(); for(int i=0;i<300;i++) x.Add(i);")==MCS_OK);
    for(int i=0;i<100;i++){
        CHECK(mcs_runtime_reset_application(&rt)==0);
        CHECK(mcs_exec_source(rt.vm,"again","if (File.ReadAllText(\"/keep\") != \"yes\") throw new Exception(\"lost\"); var xs = new List<string>(); for(int i=0;i<100;i++) xs.Add($\"item {i}\");")==MCS_OK);
        invariant(&rt.pool,&s,cfg.heap_size);
    }
#if MCS_ENABLE_THREADS
    s.max_block=64*1024; /* worker heaps are themselves single contiguous objects */
    mcs_result_t worker_rc = mcs_exec_source(rt.vm,"workers",
        "File.WriteAllText(\"/worker.cs\",\"int Fib(int n) { return n<2?n:Fib(n-1)+Fib(n-2); }\");"
        "var a = Thread.Run(\"/worker.cs\", \"Fib\", 16);"
        "var b = Thread.Run(\"/worker.cs\", \"Fib\", 17);"
        "if (a.Result != 987 || b.Result != 1597) throw new Exception(\"worker result\");");
    if (worker_rc != MCS_OK) fprintf(stderr,"worker error: %s\n",mcs_last_error(rt.vm));
    CHECK(worker_rc == MCS_OK);
    invariant(&rt.pool,&s,cfg.heap_size);
    CHECK(mcs_runtime_reset_application(&rt)==0);
#endif
    budget_checks(&rt,&s);
    size_t old_reserve=s.reserve;s.reserve=s.native_total;
    mcs_shell_memory_t ms;rt.shell.memory_info(rt.shell.memory_ud,&ms);
    CHECK(ms.has_budget && ms.growth_estimate==0 && ms.budget_free<=ms.pool.free);
    s.reserve=old_reserve;
    input="mem\n";used=0;CHECK(mcs_runtime_step(&rt,0));CHECK(strstr(output,"pool_backing ")!=NULL);CHECK(strstr(output,"196608 limit")!=NULL);
    CHECK(strstr(output,"pool_policy on_demand\n")!=NULL);
    CHECK(strstr(output,"vm_budget ")!=NULL);CHECK(strstr(output,"native_headroom ")!=NULL);
    rt.cfg.heap_headroom=NULL;used=0;input="mem\n";CHECK(mcs_runtime_step(&rt,0));
    CHECK(strstr(output,"vm_budget ")==NULL); /* never invent unknown growth */
    mcs_runtime_stop(&rt);CHECK(s.live==0);CHECK(s.count==0);
    /* Inject a failure at EACH acquisition of cold runtime initialization. */
    unsigned completed=0;
    for(unsigned fail=1;fail<100;fail++){
        init_source(&s);s.fail_at=fail;input="";used=0;
        int rc=mcs_runtime_start(&rt,&cfg);
        unsigned calls=s.calls;
        if(!rc)mcs_runtime_stop(&rt);
        CHECK(s.live==0);
        if(calls<fail){completed=fail;break;}
    }
    CHECK(completed>1);
    /* No provider: bounded system allocator is the portable OS-backed default. */
    cfg.heap_provider=NULL;cfg.console.write=quiet;input="";
#if MCS_ENABLE_MALLOC
    CHECK(mcs_runtime_start(&rt,&cfg)==0);CHECK(rt.pool.dynamic!=NULL);mcs_runtime_stop(&rt);
#else
    CHECK(mcs_pool_system_provider()==NULL);CHECK(mcs_runtime_start(&rt,&cfg)!=0);
#endif
    cfg.heap=(void*)adjacent.bytes;cfg.heap_provider=&provider;
    CHECK(mcs_runtime_start(&rt,&cfg)!=0); /* ambiguous ownership is rejected */
}
static void growth_estimates(void){
    source_t s;init_source(&s);mcs_pool_provider_t provider={acquire,release,&s};
    mcs_pool_t p;mcs_pool_dynamic_t d;mcs_pool_headroom_t h={65536,65536,100000,34464};
    CHECK(mcs_pool_init_dynamic(&p,&d,&provider,98304,0));
    unsigned calls=s.calls;
    size_t growth=mcs_pool_growth_estimate(&p,&h);
    CHECK(growth>64000 && growth<65536);CHECK(s.calls==calls && !d.committed);
    h.available=0;CHECK(!mcs_pool_growth_estimate(&p,&h));
    h.available=65536;h.largest=8;CHECK(!mcs_pool_growth_estimate(&p,&h));
    h.largest=12000;growth=mcs_pool_growth_estimate(&p,&h);CHECK(growth>64000 && growth<65536);
    d.committed=d.limit-16;CHECK(!mcs_pool_growth_estimate(&p,&h));
    d.committed=d.limit;CHECK(!mcs_pool_growth_estimate(&p,&h));
    d.committed=0;d.limit=SIZE_MAX;d.chunk=SIZE_MAX;h.available=SIZE_MAX;h.largest=SIZE_MAX;
    CHECK(mcs_pool_growth_estimate(&p,&h)<SIZE_MAX); /* no overflow */
    p.dynamic=NULL;CHECK(!mcs_pool_growth_estimate(&p,&h));
}
int main(void){growth_estimates();stress();boundaries();budget192();recover_at_cap();runtime();printf("pool regions: %u checks, %u failures\n",checks,failures);return failures?1:0;}
