/* Reproducible supplied-app profiler. Instrumentation lives outside the VM pool.
 * Run with real U8g2 and simulated HAL; this does not measure SDK radio memory. */
#include "mcs_runtime.h"
#include "mcs_internal.h"
#include "mcs_ble.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef char require_32_bit_profile[(sizeof(void*) == 4 && sizeof(mcs_value_t) == 12) ? 1 : -1];
#define RECORDS 8192
#define DEPTH 18
typedef struct { void* p; size_t size, owned; int temp, depth; void* frames[DEPTH]; } record_t;
static record_t live[RECORDS], peak_live[RECORDS];
static size_t peak, failures;
static int tracing;
static const char* prefix;
static mcs_runtime_t rt;
static size_t largest(void) { mcs_heap_stats_t s; mcs_pool_stats(&rt.pool,&s); return s.largest_free; }
static uint8_t heap[256*1024];
static uint32_t clock_ms;
static size_t provider_bytes;
static int provider_owned;
static void* acquire(void* ud,size_t minimum,size_t preferred,size_t* actual) {
    (void)ud;size_t n=provider_bytes<preferred?provider_bytes:preferred;
    if(provider_owned || n<minimum)return NULL;
    provider_owned=1;*actual=n;return heap;
}
static void release(void* ud,void* p,size_t n){(void)ud;(void)p;(void)n;provider_owned=0;}

static uint32_t ticks(void* u) { (void)u;return clock_ms; }
static void delay(void* u,uint32_t ms) { (void)u;clock_ms+=ms; }
static void output(void* u,const char* s,size_t n) { (void)u;fwrite(s,1,n,stderr); }
void* __real_mcs_pool_realloc(void*,void*,size_t,size_t);
void* __real_mcs_pool_realloc_temporary(void*,void*,size_t,size_t);
static void* trace_alloc(void* pool,void* p,size_t old,size_t n,int temp) {
    int slot=-1;
    if (tracing) {
        for(int i=0;i<RECORDS;i++)if(live[i].p==p && p){slot=i;break;}
        if(slot<0 && n)for(int i=0;i<RECORDS;i++)if(!live[i].p){slot=i;break;}
    }
    void* result=temp?__real_mcs_pool_realloc_temporary(pool,p,old,n):__real_mcs_pool_realloc(pool,p,old,n);
    if(!tracing)return result;
    if(!result && n){failures++;fprintf(stderr,"ALLOC_FAIL request=%zu used=%zu free=%zu largest=%zu temp=%d\n",n,rt.pool.used,rt.pool.size-rt.pool.used,largest(),temp);return result;}
    if(slot>=0){
        if(!n)memset(&live[slot],0,sizeof live[slot]);
        else {live[slot].p=result;live[slot].size=n;live[slot].owned=*(size_t*)((char*)result-MCS_POOL_OVERHEAD);live[slot].temp=temp;live[slot].depth=backtrace(live[slot].frames,DEPTH);}
    } else if(n){fprintf(stderr,"profiling record overflow\n");exit(2);}
    if(rt.pool.used>peak){peak=rt.pool.used;memcpy(peak_live,live,sizeof live);}
    return result;
}
void* __wrap_mcs_pool_realloc(void* a,void* p,size_t o,size_t n){return trace_alloc(a,p,o,n,0);}
void* __wrap_mcs_pool_realloc_temporary(void* a,void* p,size_t o,size_t n){return trace_alloc(a,p,o,n,1);}
static void allocations(const char* tag,record_t* records) {
    char path[512];snprintf(path,sizeof path,"%s-%s.alloc",prefix,tag);FILE* f=fopen(path,"w");if(!f)exit(2);
    for(int i=0;i<RECORDS;i++)if(records[i].p){fprintf(f,"%p %zu %zu %d",records[i].p,records[i].size,records[i].owned,records[i].temp);for(int j=0;j<records[i].depth;j++)fprintf(f," %p",records[i].frames[j]);fputc('\n',f);}
    fclose(f);
}
static void snapshot(const char* tag) {
    mcs_mem_stats_t s;mcs_mem_stats(rt.vm,&s);
    printf("SNAP %s used=%zu peak=%zu free=%zu largest=%zu tracked=%zu objects=%u gc=%u\n",tag,rt.pool.used,rt.pool.peak,rt.pool.size-rt.pool.used,largest(),s.bytes_in_use,s.objects,s.collections);
    size_t accounted=0;for(int i=0;i<RECORDS;i++)if(live[i].p)accounted+=live[i].owned;
    if(accounted!=rt.pool.used){fprintf(stderr,"trace mismatch %zu != %zu\n",accounted,rt.pool.used);exit(2);}
    allocations(tag,live);
    char path[512];snprintf(path,sizeof path,"%s-%s.functions",prefix,tag);FILE* f=fopen(path,"w");if(!f)exit(2);
    for(mcs_obj_t* o=rt.vm->objects;o;o=o->next)if(o->kind==MCS_O_FUNCTION){mcs_function_t* fn=(mcs_function_t*)o;fprintf(f,"%s code=%u/%u const=%u/%u lines=%u/%u object=%zu cache=%u\n",fn->name?fn->name->chars:"?",fn->code_len,fn->code_cap,fn->const_count,fn->const_cap,fn->line_count,fn->line_cap,sizeof *fn,fn->fcache_n);fprintf(f,"PTR fn=%p code=%p const=%p lines=%p cache=%p\n",(void*)fn,(void*)fn->code,(void*)fn->consts,(void*)fn->lines,(void*)fn->fcache);}
    for(mcs_obj_t* o=rt.vm->objects;o;o=o->next) {
        fprintf(f,"OBJ p=%p kind=%u",(void*)o,o->kind);
        if(o->kind==MCS_O_INSTANCE)fprintf(f," name=%s",((mcs_instance_t*)o)->cls->name->chars);
        if(o->kind==MCS_O_DICT)fprintf(f," keys=%p idx=%p",(void*)((mcs_dict_t*)o)->keys,((mcs_dict_t*)o)->idx);
        if(o->kind==MCS_O_USERDATA) {mcs_userdata_t* u=(mcs_userdata_t*)o;fprintf(f," name=%s bytes=%zu",u->cls->name->chars,u->size);}
        if(o->kind==MCS_O_LIST || o->kind==MCS_O_ARRAY)fprintf(f," items=%p",(void*)((mcs_list_t*)o)->items);
        if(o->kind==MCS_O_CLASS){mcs_class_t* c=(mcs_class_t*)o;fprintf(f," name=%s methods=%p getters=%p setters=%p statics=%p fields=%p defaults=%p",c->name->chars,(void*)c->methods.entries,(void*)c->getters.entries,(void*)c->setters.entries,(void*)c->statics.entries,(void*)c->fields.entries,(void*)c->field_defaults);}
        fputc('\n',f);
    }
    fprintf(f,"VM self=%p stack=%p frames=%p global_values=%p global_names=%p strings=%p gray=%p\n",(void*)rt.vm,(void*)rt.vm->stack,(void*)rt.vm->frames,(void*)rt.vm->globals,(void*)rt.vm->global_names,(void*)rt.vm->strings.slots,(void*)rt.vm->gray);
    fclose(f);
}
mcs_function_t* __real_mcs_compile(mcs_vm_t*,const char*,const char*);
mcs_function_t* __wrap_mcs_compile(mcs_vm_t* vm,const char* name,const char* src){mcs_function_t* f=__real_mcs_compile(vm,name,src);if(tracing){snapshot("compiled");allocations("compile_peak",peak_live);}return f;}
static int run(mcs_result_t rc,const char* label){if(rc!=MCS_OK){fprintf(stderr,"%s rc=%d: %s\n",label,rc,mcs_last_error(rt.vm));return 0;}return 1;}
int main(int argc,char** argv) {
    if(argc<5){fprintf(stderr,"usage: profiler fs-root file heap-bytes output-prefix [compile-image]\n");return 2;}
    prefix=argv[4];size_t capacity=strtoul(argv[3],NULL,10);if(capacity>sizeof heap)return 2;
    mcs_hal_t hal;mcs_hal_sim_t sim;memset(&sim,0,sizeof sim);mcs_hal_sim_init(&hal,&sim);
    mcs_sim_oled_t oled;memset(&oled,0,sizeof oled);oled.width=96;oled.height=16;mcs_hal_sim_attach_oled(&sim,&oled);
    mcs_ble_t ble; mcs_ble_sim_init(&ble); mcs_ble_set(&ble);
    mcs_posixfs_t fs;if(mcs_posixfs_init(&fs,argv[1]))return 2;
    mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap=heap;cfg.heap_size=capacity;cfg.hal=&hal;cfg.fs_ops=&mcs_posixfs_ops;cfg.fs_ctx=&fs;cfg.mode=MCS_RUNTIME_HEADLESS;cfg.run_boot_scripts=false;cfg.console.write=output;cfg.ticks=ticks;cfg.delay=delay;
    mcs_pool_provider_t provider={acquire,release,NULL};
    if(getenv("PROFILE_CEILING")) { provider_bytes=capacity;cfg.heap=NULL;cfg.heap_size=strtoul(getenv("PROFILE_CEILING"),NULL,10);cfg.heap_provider=&provider;cfg.heap_policy=MCS_HEAP_ACQUIRE_AT_START; }

    tracing=1;if(mcs_runtime_start(&rt,&cfg)){fprintf(stderr,"runtime startup failed\n");return 1;}
    printf("LAYOUT pointer=%zu value=%zu function=%zu class=%zu pool_align=%d\n",sizeof(void*),sizeof(mcs_value_t),sizeof(mcs_function_t),sizeof(mcs_class_t),MCS_POOL_ALIGN);
    snapshot("baseline");
    if(argc>5){
        char* data;size_t len;uint8_t* image=NULL;size_t image_len=0;
        if(mcs_vfs_read_file(&rt.vfs,argv[2],&data,&len))return 2;
        int ok=run(mcs_compile_image(rt.vm,argv[2],data,!strcmp(argv[5],"strip"),&image,&image_len),"compile image");
        if(ok){char path[512];snprintf(path,sizeof path,"%s.mcsb",prefix);FILE* f=fopen(path,"wb");if(!f)return 2;fwrite(image,1,image_len,f);fclose(f);printf("IMAGE bytes=%zu\n",image_len);mcs_free_image(rt.vm,image);}
        mcs_vfs_free(&rt.vfs,data,len);mcs_runtime_stop(&rt);return !ok;
    }
    int ok=run(mcs_exec_file(rt.vm,&rt.vfs,argv[2]),"app");snapshot("after_run");mcs_gc(rt.vm);snapshot("after_gc");
    if(ok && !getenv("PROFILE_NO_WORKLOAD")){
        mcs_value_t app=mcs_get_global(rt.vm,"app");mcs_value_t value=mcs_int(1),result;
        ok &= run(mcs_invoke(rt.vm,app,"ShowScreen",1,&value,&result),"load screen");
        value=mcs_int(2);ok &= run(mcs_invoke(rt.vm,app,"ShowScreen",1,&value,&result),"settings screen");
        ok &= run(mcs_invoke(rt.vm,app,"SaveSetting",0,NULL,&result),"warm save");
        mcs_gc(rt.vm);snapshot("warm");size_t stable=rt.pool.used;
        for(int i=0;i<1000 && ok;i++){
            value=mcs_int(i%3);ok &= run(mcs_invoke(rt.vm,app,"ShowScreen",1,&value,&result),"screen loop");
            ok &= run(mcs_invoke(rt.vm,app,"SaveSetting",0,NULL,&result),"save loop");
            clock_ms+=30;sim.virt_us+=30000;mcs_hal_poll(rt.vm);
        }
        mcs_gc(rt.vm);snapshot("after_1000");printf("STABILITY before=%zu after=%zu oled_bytes=%u timer_fired=%u\n",stable,rt.pool.used,oled.bytes,sim.timer_fired[0]);
        if(rt.pool.used!=stable || !sim.timer_on[0] || sim.timer_fired[0]<1000 || oled.bytes<192*1000)ok=0;
    }
    allocations("peak",peak_live);printf("RESULT ok=%d failed_requests=%zu peak=%zu\n",ok,failures,rt.pool.peak);
    mcs_runtime_stop(&rt);printf("TEARDOWN used=%zu\n",rt.pool.used);return !ok || rt.pool.used!=0 || provider_owned;
}
