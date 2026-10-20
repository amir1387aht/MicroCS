/* Allocation-free telemetry and idle-stack OOM/reentry safety. */
#include "../../src/mcs_internal.h"
#include "mcs_runtime.h"
#include <stdio.h>
#include <string.h>
static unsigned failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL memory %d: %s\n",__LINE__,#x); } } while(0)
static uint8_t heap[128*1024];
static char output[2048];static size_t output_len;static const char* input="";
static void write_out(void* ud,const char* s,size_t n) {(void)ud;size_t room=sizeof output-1-output_len;if(n>room)n=room;memcpy(output+output_len,s,n);output_len+=n;output[output_len]=0;}
static int read_in(void* ud,uint8_t* b,size_t n,uint32_t ms) {(void)ud;(void)ms;size_t k=strlen(input);if(k>n)k=n;memcpy(b,input,k);input+=k;return (int)k;}
static bool device_info(void* ud,mcs_heap_stats_t* st) {(void)ud;*st=(mcs_heap_stats_t){200000,150000,160000,50000,30000};return true;}
static mcs_value_t reenter(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)self;CHECK(argc==1);CHECK(mcs_to_int(vm,argv[0])==42);
 mcs_value_t result;CHECK(mcs_call(vm,"Deep",0,NULL,&result)==MCS_OK);CHECK(mcs_to_int(vm,argv[0])==42);return result;
}
int main(void) {
 mcs_hal_t hal;memset(&hal,0,sizeof hal);hal.memory_info=device_info;
 mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;cfg.heap=heap;cfg.heap_size=sizeof heap;cfg.hal=&hal;
 cfg.console=(mcs_transport_t){read_in,write_out,NULL};cfg.mode=MCS_RUNTIME_SHELL;cfg.run_boot_scripts=false;
 cfg.stack_slots=1024; /* prove that compaction does not lower an explicit limit */
 mcs_runtime_t rt;CHECK(!mcs_runtime_start(&rt,&cfg));if(!rt.vm)return 1;
 mcs_pool_t* p=&rt.pool;mcs_heap_stats_t st;mcs_pool_stats(p,&st);
 CHECK(st.total==p->size);CHECK(st.used==p->used);CHECK(st.free+st.used==st.total);CHECK(st.largest_free<=st.free);
 output_len=0;input="mem\n";size_t used=p->used,peak=p->peak;CHECK(mcs_runtime_step(&rt,0));
 CHECK(strstr(output,"heap ")!=NULL);CHECK(strstr(output,"vm_limit ")!=NULL);CHECK(strstr(output,"pool 131072 total,")!=NULL);
 CHECK(strstr(output,"device_heap 200000 total, 150000 used, 50000 free, 40000 minimum_free, 30000 largest")!=NULL);
 CHECK(p->used==used);CHECK(p->peak==peak);
 CHECK(mcs_check_source(rt.vm,"check.cs","int a=1;")==MCS_OK);CHECK(rt.vm->stack_cap==32);CHECK(rt.vm->cfg.stack_slots==1024);CHECK(rt.vm->frame_cap==1);CHECK(rt.vm->cfg.max_frames==MCS_DEFAULT_FRAMES);
 char src[8192];size_t pos=(size_t)snprintf(src,sizeof src,"int Deep() {");
 for(int i=0;i<220;i++)pos+=(size_t)snprintf(src+pos,sizeof src-pos,"int v%d=%d;",i,i);
 snprintf(src+pos,sizeof src-pos,"return v219; } int answer=Reenter(42);");
 mcs_register_function(rt.vm,"Reenter",reenter,1);CHECK(mcs_exec_source(rt.vm,"large.cs",src)==MCS_OK);
 CHECK(rt.vm->stack_cap==1024);CHECK(rt.vm->frame_cap==rt.vm->cfg.max_frames);CHECK(mcs_to_int(rt.vm,mcs_get_global(rt.vm,"answer"))==219);
 CHECK(mcs_check_source(rt.vm,"bad.cs","int broken=;")==MCS_ERR_COMPILE);CHECK(rt.vm->stack_cap==32);
 CHECK(mcs_call(rt.vm,"Deep",0,NULL,NULL)==MCS_OK);CHECK(rt.vm->stack_cap==1024);
 /* Fail restoration after compile: guard stores an offset, not a stale pointer. */
 CHECK(mcs_check_source(rt.vm,"compact.cs","int a=1;")==MCS_OK);
 size_t limit=rt.vm->cfg.heap_limit;rt.vm->cfg.heap_limit=rt.vm->bytes_allocated+16;
 CHECK(mcs_call(rt.vm,"Deep",0,NULL,NULL)==MCS_ERR_MEMORY);CHECK(rt.vm->sp==rt.vm->stack);CHECK(rt.vm->stack_cap==32);
 rt.vm->cfg.heap_limit=limit;CHECK(mcs_call(rt.vm,"Deep",0,NULL,NULL)==MCS_OK);CHECK(rt.vm->stack_cap==1024);
 /* Optional allocation failure must not inflate either successful-use peak. */
 size_t vm_peak=rt.vm->peak_bytes,pool_peak=p->peak,bytes=rt.vm->bytes_allocated;
 rt.vm->cfg.heap_limit=bytes+16;
 CHECK(mcs_try_realloc(rt.vm,NULL,0,1024)==NULL);
 CHECK(rt.vm->peak_bytes==vm_peak);CHECK(rt.vm->bytes_allocated==bytes);CHECK(p->peak==pool_peak);
 rt.vm->cfg.heap_limit=limit;
 CHECK(mcs_pool_realloc(p,NULL,0,p->size+1)==NULL);CHECK(p->peak==pool_peak);
 mcs_runtime_stop(&rt);CHECK(p->used==0);
 printf("%s memory telemetry and idle stack: %u checks, %u failures\n",failures?"FAIL":"PASS",checks,failures);return failures!=0;
}
