/* Both acquisition policies use the production pool/runtime; finite provider,
 * deliberately unaligned regions, canaries, partial/OOM and complete teardown. */
#include "mcs_runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if MCS_ENABLE_POOL_REGIONS
#define CAP (96u * 1024u)
typedef struct { unsigned char* allocation; void* raw; size_t size; } block_t;
typedef struct {
    block_t blocks[32]; size_t native, reserve, max_block, live;
    unsigned calls, releases, count, fail_at;
} source_t;
static void init(source_t* s) {
    memset(s,0,sizeof *s);s->native=CAP+32768;s->reserve=32768;s->max_block=24576;
}
static void* acquire(void* ud,size_t minimum,size_t preferred,size_t* actual) {
    source_t* s=ud;s->calls++;
    if(s->fail_at==s->calls)return NULL;
    size_t n=preferred,free=s->native-s->live;
    size_t available=free>s->reserve?free-s->reserve:0;
    if(n>available)n=available;
    if(n>s->max_block)n=s->max_block;
    if(n<minimum)return NULL;
    for(unsigned i=0;i<32;i++)if(!s->blocks[i].raw){
        unsigned char* p=malloc(n+32);assert(p);memset(p,0xa7,n+32);
        void* raw=p+11;s->blocks[i]=(block_t){p,raw,n};
        s->live+=n;s->count++;*actual=n;return raw;
    }
    return NULL;
}
static void release(void* ud,void* p,size_t bytes){
    source_t* s=ud;
    for(unsigned i=0;i<32;i++)if(s->blocks[i].raw==p){
        block_t* b=&s->blocks[i];assert(b->size==bytes);
        for(unsigned j=0;j<11;j++)assert(b->allocation[j]==0xa7);
        for(size_t j=11+bytes;j<bytes+32;j++)assert(b->allocation[j]==0xa7);
        s->live-=bytes;s->count--;s->releases++;free(b->allocation);memset(b,0,sizeof *b);return;
    }
    assert(!"unknown release");
}
static void startup_pool(void){
    source_t s;init(&s);mcs_pool_provider_t provider={acquire,release,&s};
    mcs_pool_t p;mcs_pool_dynamic_t d;
    assert(mcs_pool_init_dynamic(&p,&d,&provider,CAP,0));assert(!s.calls);
    assert(mcs_pool_acquire_at_start(&p));assert(d.sealed && d.committed==CAP && d.count==4);
    size_t capacity=p.size;unsigned calls=s.calls;
    assert(capacity<CAP && capacity>CAP-1024);assert(!mcs_pool_acquire_at_start(&p));
    void* a[2048]={0};size_t n=0;
    while(n<2048 && (a[n]=mcs_pool_realloc(&p,NULL,0,64))){memset(a[n],(int)(n&255),64);n++;}
    assert(n>1000 && n<2048);assert(s.calls==calls);assert(!mcs_pool_trim(&p));
    assert(!mcs_pool_realloc_temporary(&p,NULL,0,CAP));assert(s.calls==calls);
    for(size_t i=0;i<n;i++){
        for(unsigned j=0;j<64;j++)assert(((unsigned char*)a[i])[j]==(i&255));
        mcs_pool_realloc(&p,a[i],64,0);
    }
    assert(!p.used && p.size==capacity && d.committed==CAP);
    assert(!mcs_pool_trim(&p) && !s.releases);
    mcs_pool_headroom_t h={CAP,CAP,CAP,0};assert(!mcs_pool_growth_estimate(&p,&h));
    void* x=mcs_pool_realloc(&p,NULL,0,100);assert(x);memset(x,0x5b,100);
    assert(!mcs_pool_realloc(&p,x,100,30000)); /* cannot combine distinct regions */
    for(unsigned i=0;i<100;i++)assert(((unsigned char*)x)[i]==0x5b);
    mcs_pool_realloc(&p,x,100,0);assert(p.size==capacity && s.calls==calls);
    mcs_pool_destroy(&p);assert(!s.live && !s.count && s.releases==4);
}
static void partial_and_failures(void){
    for(unsigned failure=0;failure<=5;failure++){
        source_t s;init(&s);s.fail_at=failure;mcs_pool_provider_t provider={acquire,release,&s};
        mcs_pool_t p;mcs_pool_dynamic_t d;assert(mcs_pool_init_dynamic(&p,&d,&provider,CAP,0));
        bool ok=mcs_pool_acquire_at_start(&p);assert(d.sealed);assert(ok==(s.live!=0));
        if(failure==1)assert(!ok);
        if(failure==2)assert(d.committed==s.max_block);
        assert(s.live<=CAP && s.native-s.live>=s.reserve);
        unsigned calls=s.calls;s.native+=CAP;
        assert(!mcs_pool_realloc(&p,NULL,0,CAP*2));assert(s.calls==calls);
        mcs_pool_destroy(&p);assert(!s.live && !s.count);
    }
    source_t s;init(&s);s.native=70000;s.reserve=30000;
    mcs_pool_provider_t provider={acquire,release,&s};mcs_pool_t p;mcs_pool_dynamic_t d;
    assert(mcs_pool_init_dynamic(&p,&d,&provider,CAP,0));assert(mcs_pool_acquire_at_start(&p));
    assert(d.committed==40000 && d.count==2 && s.native-s.live==s.reserve);
    mcs_pool_destroy(&p);assert(!s.live);
    s.native=s.reserve;assert(mcs_pool_init_dynamic(&p,&d,&provider,CAP,0));
    assert(!mcs_pool_acquire_at_start(&p));mcs_pool_destroy(&p);assert(!s.live);
    init(&s);assert(mcs_pool_init_dynamic(&p,&d,&provider,CAP,0));
    void* x=mcs_pool_realloc(&p,NULL,0,32);assert(x);
    assert(!mcs_pool_acquire_at_start(&p) && !d.sealed);mcs_pool_destroy(&p);
}

#if MCS_ENABLE_RUNTIME
static void quiet(void* ud,const char* text,size_t n){(void)ud;(void)text;(void)n;}
static const char* input="";
static char output[4096];static size_t output_len;
static int read_input(void* ud,uint8_t* dst,size_t n,uint32_t timeout){
    (void)ud;(void)timeout;size_t k=strlen(input);if(k>n)k=n;memcpy(dst,input,k);input+=k;return (int)k;
}
static void capture(void* ud,const char* text,size_t n){
    (void)ud;size_t room=sizeof output-1-output_len;if(n>room)n=room;
    memcpy(output+output_len,text,n);output_len+=n;output[output_len]=0;
}
static void runtime_policies(void){
    mcs_runtime_cfg_t defaults=MCS_RUNTIME_DEFAULTS;assert(defaults.heap_policy==MCS_RUNTIME_HEAP_POLICY);
    for(unsigned mode=0;mode<2;mode++){
        source_t s;init(&s);mcs_pool_provider_t provider={acquire,release,&s};
        mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap_size=CAP;cfg.heap_provider=&provider;
        cfg.heap_policy=(mcs_heap_policy_t)mode;cfg.run_boot_scripts=false;cfg.mode=MCS_RUNTIME_SHELL;
        cfg.console=(mcs_transport_t){read_input,capture,NULL};input="";output_len=0;mcs_runtime_t rt;assert(!mcs_runtime_start(&rt,&cfg));
        assert(rt.pool.dynamic->sealed==(mode==MCS_HEAP_ACQUIRE_AT_START));
        size_t capacity=rt.pool.size,backing=s.live;unsigned calls=s.calls;
        if(mode==MCS_HEAP_ACQUIRE_AT_START)assert(backing==CAP);
        else assert(backing<CAP);
        for(unsigned i=0;i<50;i++){
#if MCS_ENABLE_COMPILER
            assert(mcs_exec_source(rt.vm,"policy","var xs = new List<int>(); for (int i=0;i<150;i++) xs.Add(i);")==MCS_OK);
#endif
            assert(!mcs_runtime_reset_application(&rt));
            mcs_shell_memory_t st;rt.shell.memory_info(rt.shell.memory_ud,&st);
            assert(st.pool.used+st.pool.free==st.pool.total);
            assert(!strcmp(st.pool_policy,mode?"acquire_at_start":"on_demand"));
            if(mode){
                assert(st.has_budget && !st.growth_estimate);
                assert(rt.pool.size==capacity && s.live==backing && s.calls==calls && !s.releases);
            }
        }
        input="mem\n";output_len=0;assert(mcs_runtime_step(&rt,0));
        assert(strstr(output,mode?"pool_policy acquire_at_start\n":"pool_policy on_demand\n"));
        assert(!strstr(output,"native_headroom ")); /* no callback: never claim zero native reserve */
        assert((strstr(output,"vm_budget ")!=NULL)==(mode==MCS_HEAP_ACQUIRE_AT_START));
        mcs_runtime_stop(&rt);assert(!s.live && !s.count);
    }
    { /* A requested ceiling above native capacity still starts with the actual pool. */
        source_t s;init(&s);s.native=70000;s.reserve=30000;
        mcs_pool_provider_t provider={acquire,release,&s};mcs_runtime_t rt;
        mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap_size=CAP;cfg.heap_provider=&provider;
        cfg.heap_policy=MCS_HEAP_ACQUIRE_AT_START;cfg.run_boot_scripts=false;cfg.console.write=quiet;
        assert(!mcs_runtime_start(&rt,&cfg));assert(rt.pool.dynamic->committed==40000);
        assert(rt.pool.size<40000 && s.native-s.live==s.reserve);
        size_t capacity=rt.pool.size;unsigned calls=s.calls;
        assert(!mcs_runtime_reset_application(&rt));
        assert(rt.pool.size==capacity && s.calls==calls);
        mcs_runtime_stop(&rt);assert(!s.live && !s.count);
    }
    /* Startup provider failure and later VM-init failure both release all backing. */
    for(unsigned i=0;i<2;i++){
        source_t s;init(&s);if(!i)s.fail_at=1;
        mcs_pool_provider_t provider={acquire,release,&s};mcs_runtime_t rt;
        mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap_size=i?1024:CAP;cfg.heap_provider=&provider;
        cfg.heap_policy=MCS_HEAP_ACQUIRE_AT_START;cfg.run_boot_scripts=false;cfg.console.write=quiet;
        assert(mcs_runtime_start(&rt,&cfg)!=0);assert(!s.live && !s.count);
    }
    /* No platform allocator required for static buffers: both policies stay fixed. */
    static unsigned char fixed[CAP];
    for(unsigned mode=0;mode<2;mode++){
        mcs_runtime_t rt;mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;
        cfg.heap=fixed;cfg.heap_size=sizeof fixed;cfg.heap_policy=(mcs_heap_policy_t)mode;
        cfg.run_boot_scripts=false;cfg.console.write=quiet;assert(!mcs_runtime_start(&rt,&cfg));
        mcs_shell_memory_t st;rt.shell.memory_info(rt.shell.memory_ud,&st);
        assert(!rt.pool.dynamic && !strcmp(st.pool_policy,"static"));mcs_runtime_stop(&rt);
    }
    mcs_runtime_t rt;mcs_runtime_cfg_t bad=MCS_RUNTIME_DEFAULTS;bad.heap_policy=(mcs_heap_policy_t)99;
    assert(mcs_runtime_start(&rt,&bad)!=0);bad.heap_policy=MCS_HEAP_ACQUIRE_AT_START;
    assert(mcs_runtime_start(&rt,&bad)!=0); /* no bounded pool configured */
#if MCS_ENABLE_MALLOC
    mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap_size=CAP;cfg.heap_policy=MCS_HEAP_ACQUIRE_AT_START;
    cfg.run_boot_scripts=false;cfg.console.write=quiet;assert(!mcs_runtime_start(&rt,&cfg));
    assert(rt.pool.dynamic->sealed && rt.pool.dynamic->committed==CAP);mcs_runtime_stop(&rt);
#endif
}
#endif
int main(void){startup_pool();partial_and_failures();
#if MCS_ENABLE_RUNTIME
    runtime_policies();
#endif
    puts("PASS heap policies: on-demand/startup, multi-region cap/reserve, canaries, partial/OOM, fixed reload capacity, static/system providers and teardown");return 0;}
#else
int main(void){mcs_pool_t p;mcs_pool_dynamic_t d;
    assert(!mcs_pool_init_dynamic(&p,&d,NULL,98304,0));assert(!mcs_pool_acquire_at_start(&p));
    puts("PASS heap policies: automatic regions disabled safely");return 0;}
#endif
