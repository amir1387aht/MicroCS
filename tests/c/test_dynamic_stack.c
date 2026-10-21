/* Force every successful realloc to move, including suspended interpreter
 * registers, open captures, native argv and constructors. ASan checks lifetime. */
#include "../../src/mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL dynamic %d: %s\n",__LINE__,#x); } } while(0)
static size_t outstanding;
static bool deny_growth;
static int fail_after=-1;
static void* moving(void* ud,void* p,size_t old,size_t n) {
 (void)ud;
 if(!n) { free(p);outstanding-=old;return NULL; }
 if(deny_growth && n>old)return NULL;
 if(n>old && fail_after>=0){if(!fail_after)return NULL;fail_after--;}
 void* q=malloc(n);if(!q)return NULL;
 if(p)memcpy(q,p,old<n?old:n);
 free(p);outstanding=outstanding-old+n;return q;
}
static void quiet(void* ud,const char* s,size_t n) {(void)ud;(void)s;(void)n;}
static mcs_value_t bounce(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)self;CHECK(argc==2);CHECK(mcs_to_int(vm,argv[0])==42);
 argv[1]=mcs_string(vm,"root only in native argv");
 mcs_value_t result;CHECK(mcs_call(vm,"Grow",0,NULL,&result)==MCS_OK);
 mcs_gc(vm);
 CHECK(mcs_to_int(vm,argv[0])==42);CHECK(!strcmp(mcs_to_cstr(vm,argv[1]),"root only in native argv"));
 return result;
}
static mcs_value_t fail_native(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)self;(void)argc;(void)argv;deny_growth=true;
 mcs_mem_realloc(vm,NULL,0,123456);return mcs_null();
}
static void userdata_ctor(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)bounce(vm,self,argc,argv);
}
static const mcs_class_def_t native_class={"NativeBox",1,userdata_ctor,NULL,NULL,NULL};
static mcs_value_t wrong_arity(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)self;(void)argc;(void)argv;mcs_value_t n=mcs_int(1);
 CHECK(mcs_call(vm,"Grow",1,&n,NULL)==MCS_ERR_RUNTIME);return mcs_null();
}
static mcs_value_t request_abort(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)self;(void)argc;(void)argv;mcs_request_abort(vm);return mcs_null();
}
static mcs_value_t invoke_abort(mcs_vm_t* vm,mcs_value_t self,int argc,mcs_value_t* argv) {
 (void)self;CHECK(argc==1);CHECK(mcs_invoke(vm,argv[0],"Stop",0,NULL,NULL)==MCS_ERR_ABORTED);
 return mcs_null();
}
static bool in_hook;
static int reentry_hook(mcs_vm_t* vm,void* ud) {
 (void)ud;if(in_hook)return 0;in_hook=true;
 mcs_value_t r;CHECK(mcs_call(vm,"Grow",0,NULL,&r)==MCS_OK);CHECK(mcs_to_int(vm,r)==239);
 in_hook=false;return 0;
}
static void fixture(char* src,size_t size) {
 size_t p=(size_t)snprintf(src,size,"int Grow(){");
 for(int i=0;i<240;i++)p+=(size_t)snprintf(src+p,size-p,"int v%d=%d;",i,i);
 snprintf(src+p,size-p,"return v239;} int Rec(int n){if(n==0)return Grow();return 1+Rec(n-1);} "
 "Func<int> Make(){int x=41;Func<int> f=()=>++x;Bounce(42,0);return f;} "
 "int Test(){int x=7;int y=Bounce(42,0);return x+y;} "
 "int Expr(){return 1+(2+(3+(4+(5+(6+(7+(8+(9+(10+(11+(12+(13+(14+(15+(16+(17+(18+(19+(20+(21+(22+(23+(24+(25+(26+(27+(28+(29+(30+(31+(32+(33+(34+(35+(36+(37+(38+(39+(40)))))))))))))))))))))))))))))))))))))));} "
 "int Catch(){try{Rec(1000);}catch(StackOverflowException){return 42;}return 0;} "
 "class C{public int V{get{return Bounce(42,0);}set{Bounce(42,0);}} public int this[int i]{get{return Bounce(42,0);}set{Bounce(42,0);}}} "
 "int Properties(){var c=new C();c.V=3;c[0]=4;return c.V+c[0];}");
}
static void run_test(mcs_vm_t* vm,const char* name,int expected) {
 CHECK(mcs_check_source(vm,"trim.cs","int dummy=0;")==MCS_OK);
 mcs_value_t r;CHECK(mcs_call(vm,name,0,NULL,&r)==MCS_OK);CHECK(mcs_to_int(vm,r)==expected);
 CHECK(vm->exec_views==NULL);CHECK(vm->native_args==NULL);CHECK(vm->sp==vm->stack);CHECK(!vm->frame_count);
}
int main(void) {
 mcs_config_t cfg;mcs_config_default(&cfg);cfg.realloc_fn=moving;cfg.write_fn=quiet;cfg.error_fn=quiet;
 mcs_vm_t* vm=mcs_new(&cfg);CHECK(vm!=NULL);if(!vm)return 1;
 CHECK(vm->stack_cap==32);CHECK(vm->frame_cap==1);CHECK(vm->cfg.stack_slots==MCS_DEFAULT_STACK);CHECK(vm->cfg.max_frames==MCS_DEFAULT_FRAMES);
 mcs_register_function(vm,"Bounce",bounce,2);
 mcs_register_function(vm,"WrongArity",wrong_arity,0);
 mcs_register_function(vm,"FailNative",fail_native,1);
 mcs_register_function(vm,"RequestAbort",request_abort,0);mcs_register_function(vm,"InvokeAbort",invoke_abort,1);
 mcs_register_class(vm,&native_class);
 mcs_class_t* factory=mcs_new_class(vm,mcs_intern_c(vm,"NativeFactory"),CLS_BUILTIN);
 factory->native_ctor=bounce;mcs_set_global(vm,"NativeFactory",OBJ_VAL(factory));
 char src[16384];fixture(src,sizeof src);mcs_result_t built=mcs_exec_source(vm,"dynamic.cs",src);CHECK(built==MCS_OK);if(built!=MCS_OK){fprintf(stderr,"%s\n",mcs_last_error(vm));mcs_free(vm);return 1;}
 CHECK(vm->frame_peak==1);
 CHECK(mcs_exec_source(vm,"arity.cs","try{WrongArity();}catch(ArgumentException){}")==MCS_OK);
 CHECK(vm->frame_peak==1);CHECK(vm->frame_cap==1);
 run_test(vm,"Test",246);CHECK(vm->stack_cap>=240);CHECK(vm->stack_cap<cfg.stack_slots);
 CHECK(mcs_exec_source(vm,"capture.cs","var captured=Make(); int a=captured(); int b=captured();")==MCS_OK);
 CHECK(mcs_to_int(vm,mcs_get_global(vm,"a"))==42);CHECK(mcs_to_int(vm,mcs_get_global(vm,"b"))==43);
 mcs_value_t n=mcs_int(36),r;CHECK(mcs_call(vm,"Rec",1,&n,&r)==MCS_OK);CHECK(mcs_to_int(vm,r)==275);CHECK(vm->frame_peak>16);
 CHECK(mcs_exec_source(vm,"ctor.cs","int Construct(){var b=new NativeBox(42,0);return 12;} int Factory(){return new NativeFactory(42,0);} int Fail(){FailNative(1);return 0;}")==MCS_OK);
 run_test(vm,"Construct",12);run_test(vm,"Factory",239);
 CHECK(mcs_call(vm,"Fail",0,NULL,NULL)==MCS_ERR_MEMORY);deny_growth=false;
 CHECK(vm->native_args==NULL);CHECK(vm->exec_views==NULL);CHECK(vm->sp==vm->stack);
 run_test(vm,"Expr",820);run_test(vm,"Properties",478);run_test(vm,"Catch",42);run_test(vm,"Test",246);
 CHECK(mcs_check_source(vm,"trim.cs","int noop=0;")==MCS_OK);CHECK(vm->stack_cap==32);CHECK(vm->frame_cap==1);
 deny_growth=true;CHECK(mcs_call(vm,"Grow",0,NULL,NULL)==MCS_ERR_MEMORY);deny_growth=false;
 CHECK(!vm->exec_views);CHECK(!vm->native_args);run_test(vm,"Grow",239);
 /* Sweep allocation failure through recursive frame/slot growth. Every OOM
  * must restore suspended views and leave the VM usable for another call. */
 unsigned failed_calls=0,successful_calls=0;
 for(int fault=0;fault<24;fault++) {
  CHECK(mcs_check_source(vm,"trim.cs","int noop=0;")==MCS_OK);
  fail_after=fault;mcs_result_t code=mcs_call(vm,"Rec",1,&n,&r);fail_after=-1;
  CHECK(code==MCS_OK || code==MCS_ERR_MEMORY);
  if(code==MCS_OK){successful_calls++;CHECK(mcs_to_int(vm,r)==275);}else failed_calls++;
  CHECK(!vm->native_args);CHECK(!vm->exec_views);CHECK(vm->sp==vm->stack);
  CHECK(!vm->frame_count);CHECK(!vm->handler_count);CHECK(!mcs_has_exception(vm));
  CHECK(mcs_call(vm,"Grow",0,NULL,&r)==MCS_OK);CHECK(mcs_to_int(vm,r)==239);
 }
 CHECK(failed_calls>0);CHECK(successful_calls>0);
 /* A hook reenters while the outer dispatch loop holds live register caches. */
 CHECK(mcs_check_source(vm,"trim.cs","int noop=0;")==MCS_OK);
 vm->cfg.hook_fn=reentry_hook;vm->hook_counter=1;
 CHECK(mcs_exec_source(vm,"hook.cs","int outer=7;for(int i=0;i<10000;i++)outer++; int hookAnswer=outer;")==MCS_OK);
 CHECK(mcs_to_int(vm,mcs_get_global(vm,"hookAnswer"))==10007);vm->cfg.hook_fn=NULL;
 mcs_mem_stats_t st;mcs_mem_stats(vm,&st);CHECK(st.stack_peak>32);CHECK(st.frames_peak>16);CHECK(st.stack_limit==cfg.stack_slots);
 CHECK(mcs_exec_source(vm,"invoke-abort.cs","class AbortTarget{public void Stop(){try{RequestAbort();}finally{int x=1;}}} int afterAbort=0; try{InvokeAbort(new AbortTarget());}catch(Exception){afterAbort=1;} afterAbort=2;")==MCS_ERR_ABORTED);
 CHECK(mcs_to_int(vm,mcs_get_global(vm,"afterAbort"))==0);CHECK(!vm->handler_count);
 CHECK(!vm->native_args);CHECK(!vm->exec_views);run_test(vm,"Grow",239);
 mcs_free(vm);CHECK(outstanding==0);
 /* Exhaust slots before frames as a separate catchable ceiling, then recover. */
 cfg.stack_slots=96;vm=mcs_new(&cfg);CHECK(vm!=NULL);if(!vm)return 1;
 CHECK(mcs_exec_source(vm,"slotlimit.cs","int Rec(int n){return 1+Rec(n+1);} int Catch(){try{Rec(0);}catch(StackOverflowException){return 42;}return 0;}")==MCS_OK);
 run_test(vm,"Catch",42);CHECK(vm->stack_cap<=96);CHECK(vm->frame_peak<cfg.max_frames);
 /* A top-level invocation rejected before dispatch must finish the exception,
  * rather than leaving it pending to poison the next independent API call. */
 mcs_value_t many[96];for(int i=0;i<96;i++)many[i]=mcs_null();
 CHECK(mcs_invoke(vm,mcs_null(),"Missing",96,many,&r)==MCS_ERR_RUNTIME);
 CHECK(!mcs_has_exception(vm));CHECK(vm->sp==vm->stack);CHECK(!vm->frame_count);
 CHECK(mcs_exec_source(vm,"after-invoke.cs","int afterInvoke=42;")==MCS_OK);
 CHECK(mcs_to_int(vm,mcs_get_global(vm,"afterInvoke"))==42);

 mcs_free(vm);CHECK(outstanding==0);
 printf("%s dynamic execution storage: %u checks, %u failures\n",failures?"FAIL":"PASS",checks,failures);return failures!=0;
}
