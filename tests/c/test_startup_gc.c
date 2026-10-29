/* A partial acquire-at-start pool must set GC pressure from owned capacity,
 * not a configured ceiling that the provider could never acquire. */
#include "mcs_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char backing[48*1024];
static int owned;
static void* acquire(void* ud,size_t minimum,size_t preferred,size_t* actual){
    (void)ud;size_t n=sizeof backing;if(n>preferred)n=preferred;
    if(owned || n<minimum)return NULL;
    owned=1;*actual=n;return backing;
}
static void release(void* ud,void* p,size_t n){(void)ud;assert(p==backing && n==sizeof backing);owned=0;}
static void quiet(void* ud,const char* s,size_t n){(void)ud;(void)s;(void)n;}
int main(void){
    mcs_pool_provider_t provider={acquire,release,NULL};mcs_runtime_t rt;
    mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap_provider=&provider;cfg.heap_size=192*1024;
    cfg.heap_policy=MCS_HEAP_ACQUIRE_AT_START;cfg.mode=MCS_RUNTIME_HEADLESS;cfg.run_boot_scripts=false;cfg.console.write=quiet;
    assert(!mcs_runtime_start(&rt,&cfg));assert(rt.pool.dynamic->sealed && rt.pool.size<sizeof backing);
    char retained[24*1024];memset(retained,'x',sizeof retained);
    mcs_value_t held=mcs_string_n(rt.vm,retained,sizeof retained);mcs_push_root(rt.vm,held);mcs_set_global(rt.vm,"held",held);mcs_pop_root(rt.vm,1);
    const char* script="for(int i=0;i<5000;i++){var xs=new List<int>();for(int j=0;j<32;j++)xs.Add(j);} if(held.Length!=24576)throw new Exception(\"corruption\");";
    mcs_result_t rc=mcs_exec_source(rt.vm,"churn.cs",script);
    mcs_mem_stats_t stats;mcs_mem_stats(rt.vm,&stats);
    assert(stats.heap_limit<=rt.pool.size && rt.cfg.heap_size==192*1024);
    printf("startup GC: rc=%d pool=%zu peak=%zu limit=%zu collections=%u error=%s\n",rc,rt.pool.size,rt.pool.peak,stats.heap_limit,stats.collections,mcs_last_error(rt.vm));
    int failed=rc!=MCS_OK || stats.collections<10;
    mcs_gc(rt.vm);assert(mcs_to_cstr(rt.vm,mcs_get_global(rt.vm,"held"))[0]=='x');
    size_t capacity=rt.pool.size;assert(!mcs_runtime_reset_application(&rt));assert(rt.pool.size==capacity);
    mcs_runtime_stop(&rt);assert(!rt.pool.used && !owned);return failed;
}
