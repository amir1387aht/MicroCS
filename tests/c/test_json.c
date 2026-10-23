/* JSON allocation-failure cleanup and strict byte-level Unicode regressions. */
#include "mcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures;
#define CHECK(x) do { checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);} } while(0)
#if MCS_ENABLE_JSON && MCS_ENABLE_LIST && MCS_ENABLE_DICT
typedef struct {size_t live;int call,fail;bool move;} heap_t;
static void* alloc(void* ctx,void* p,size_t old,size_t n){
 heap_t* h=(heap_t*)ctx;
 if(!n){CHECK(h->live>=old);h->live-=old;free(p);return NULL;}
 if(h->fail && ++h->call==h->fail)return NULL;
 void* q;if(h->move && p){q=malloc(n);if(q){memcpy(q,p,old<n?old:n);free(p);}}else q=realloc(p,n);if(q){CHECK(h->live>=old);h->live=h->live-old+n;}return q;
}
static void quiet(void* ud,const char* text,size_t n){(void)ud;(void)text;(void)n;}
static mcs_vm_t* vm_new(heap_t* heap){
 mcs_config_t cfg;mcs_config_default(&cfg);cfg.realloc_fn=alloc;cfg.alloc_ud=heap;cfg.write_fn=quiet;
 mcs_vm_t* vm=mcs_new(&cfg);CHECK(vm!=NULL);return vm;
}
#if MCS_ENABLE_COMPILER
static void fault(const char* method,int stringify){
 int successes=0;
 for(int fail=1;fail<256;fail++){
  heap_t heap={0};mcs_vm_t* vm=vm_new(&heap);if(!vm)return;
  CHECK(mcs_exec_source(vm,"warm.cs","class Data {public string Name=\"value\"; public int Value=>42;} var sample = new Data(); Json.Parse(\"{}\"); Json.Serialize(null);")==MCS_OK);
  mcs_value_t input=stringify?mcs_get_global(vm,"sample"):mcs_string(vm,"{\"items\":[1,2,{\"text\":\"a somewhat longer string that grows the scratch buffer twice\"}],\"tail\":true}");
  mcs_push_root(vm,input);
  heap.call=0;heap.fail=fail;mcs_value_t out=mcs_null();
  mcs_result_t result=mcs_call(vm,method,1,&input,&out);heap.fail=0;
  if(result==MCS_OK)successes++;
  else {
   CHECK(result==MCS_ERR_MEMORY || result==MCS_ERR_RUNTIME);
   mcs_gc(vm);
   CHECK(mcs_call(vm,method,1,&input,&out)==MCS_OK); /* same-VM recovery, GC pause/root restoration */
  }
  mcs_pop_root(vm,1);mcs_free(vm);if(heap.live)fprintf(stderr,"LEAK %s fail=%d live=%zu\n",method,fail,heap.live);CHECK(heap.live==0);
  if(successes){printf("PASS JSON %s: %d injected allocation stages\n",method,fail-1);break;}
 }
 CHECK(successes==1);
}
static void dictionary_retry(void){
 for(int fail=1;fail<=4;fail++){
  heap_t h={0};h.move=true;mcs_vm_t* vm=vm_new(&h);if(!vm)return;
  CHECK(mcs_exec_source(vm,"dict.cs","var keep=new Dictionary<string,int>{[\"a\"]=1,[\"b\"]=2,[\"c\"]=3,[\"d\"]=4}; void Grow(){keep[\"e\"]=5;}")==MCS_OK);
  h.call=0;h.fail=fail;mcs_result_t r=mcs_call(vm,"Grow",0,NULL,NULL);h.fail=0;
  CHECK(r==MCS_OK || r==MCS_ERR_MEMORY);
  CHECK(mcs_call(vm,"Grow",0,NULL,NULL)==MCS_OK);
  CHECK(mcs_exec_source(vm,"verify.cs","if(keep.Count!=5 || keep[\"a\"]!=1 || keep[\"e\"]!=5) throw new Exception(\"dictionary damaged\");")==MCS_OK);
  mcs_free(vm);CHECK(h.live==0);
 }
}
static void dictionary_storage(void){
 heap_t h={0};h.move=true;mcs_vm_t* vm=vm_new(&h);if(!vm)return;
 CHECK(mcs_exec_source(vm,"storage.cs",
  "var data=new Dictionary<int,int>();var seen=new HashSet<int>();"
  "for(int i=0;i<600;i++){data[i]=i*3;seen.Add(i);}"
  "for(int i=0;i<600;i++){if(data[i]!=i*3 || !seen.Contains(i))throw new Exception(\"growth lost values\");}"
  "for(int i=0;i<300;i++){data.Remove(i);seen.Remove(i);}"
  "for(int i=600;i<900;i++){data[i]=i*3;seen.Add(i);}"
  "if(data.Count!=600 || seen.Count!=600)throw new Exception(\"count\");"
  "for(int i=300;i<900;i++){if(data[i]!=i*3 || !seen.Contains(i))throw new Exception(\"regrowth lost values\");}"
  "data.Clear();seen.Clear();for(int i=0;i<200;i++){data[i]=i+1;seen.Add(i);}"
  "for(int i=0;i<200;i++){if(data[i]!=i+1 || !seen.Contains(i))throw new Exception(\"clear/reuse\");}"
  "data.Clear();seen.Clear();for(int i=0;i<50000;i++){data[i]=i+1;seen.Add(i);}"
  "for(int i=0;i<50000;i++){if(data[i]!=i+1 || !seen.Contains(i))throw new Exception(\"wide index\");}"
 )==MCS_OK);
 mcs_gc(vm);mcs_free(vm);CHECK(h.live==0);
}

#endif
static void pool_repeat(void){
 static unsigned char bytes[40*1024];mcs_pool_t pool;mcs_pool_init(&pool,bytes,sizeof bytes);
 mcs_config_t cfg;mcs_config_default(&cfg);cfg.realloc_fn=mcs_pool_realloc;cfg.alloc_ud=&pool;cfg.heap_limit=sizeof bytes;cfg.write_fn=quiet;
 mcs_vm_t* vm=mcs_new(&cfg);CHECK(vm!=NULL);if(!vm)return;
 mcs_value_t text=mcs_string(vm,"{\"id\":7,\"values\":[1,true,null,\"text\"],\"child\":{\"x\":2}}");mcs_push_root(vm,text);
 size_t baseline=0;
 for(int i=0;i<1000;i++){
  mcs_value_t value=mcs_null(),out=mcs_null();
  CHECK(mcs_call(vm,"Json.Parse",1,&text,&value)==MCS_OK);mcs_push_root(vm,value);
  CHECK(mcs_call(vm,"Json.Serialize",1,&value,&out)==MCS_OK);CHECK(mcs_is_string(out));
  mcs_pop_root(vm,1);mcs_gc(vm);
  if(i==2)baseline=pool.used;
  if(i>2)CHECK(pool.used==baseline);
 }
 printf("PASS JSON: 1000 round trips, 40 KiB pool, peak %zu bytes, retained %zu bytes\n",pool.peak,pool.used);
 mcs_pop_root(vm,1);mcs_free(vm);CHECK(pool.used==0);
}
static void numeric_bounds(void){
 heap_t h={0};mcs_vm_t* vm=vm_new(&h);if(!vm)return;
#if MCS_INT64
 const char* tokens[]={"-9223372036854775808","9223372036854775807"};
#else
 const char* tokens[]={"-2147483648","2147483647"};
#endif
 for(unsigned i=0;i<2;i++){
  mcs_value_t text=mcs_string(vm,tokens[i]),out=mcs_null();mcs_push_root(vm,text);
  CHECK(mcs_call(vm,"Json.Parse",1,&text,&out)==MCS_OK);CHECK(mcs_is_int(out));
  mcs_push_root(vm,out);mcs_value_t encoded=mcs_null();CHECK(mcs_call(vm,"Json.Serialize",1,&out,&encoded)==MCS_OK);
  CHECK(!strcmp(mcs_to_cstr(vm,encoded),tokens[i]));mcs_pop_root(vm,2);
 }
 mcs_free(vm);CHECK(h.live==0);
}
static void fuzz_bytes(void){
 heap_t h={0};mcs_vm_t* vm=vm_new(&h);if(!vm)return;unsigned rng=17;
 for(int i=0;i<1000;i++){
  char data[96];size_t n=(size_t)i%sizeof data;
  for(size_t j=0;j<n;j++){rng=rng*1664525u+1013904223u;data[j]=(char)(rng>>24);}
  mcs_value_t text=mcs_string_n(vm,data,n),out=mcs_null();mcs_push_root(vm,text);
  mcs_result_t result=mcs_call(vm,"Json.Parse",1,&text,&out);CHECK(result==MCS_OK || result==MCS_ERR_RUNTIME);
  mcs_pop_root(vm,1);mcs_gc(vm);
 }
 mcs_free(vm);CHECK(h.live==0);
}
static void invalid_bytes(void){
 heap_t h={0};mcs_vm_t* vm=vm_new(&h);if(!vm)return;
 const unsigned char cases[][7]={{'"',0xc0,0x80,'"',0},{'"',0xed,0xa0,0x80,'"',0},{'"',0xf4,0x90,0x80,0x80,'"',0},{'"',0xe2,0x82,'"',0},{'"',0x80,'"',0}};
 for(size_t i=0;i<sizeof cases/sizeof cases[0];i++){
  mcs_value_t text=mcs_string(vm,(const char*)cases[i]),out=mcs_null();mcs_push_root(vm,text);
  CHECK(mcs_call(vm,"Json.Parse",1,&text,&out)==MCS_ERR_RUNTIME);
  CHECK(mcs_call(vm,"Json.Escape",1,&text,&out)==MCS_ERR_RUNTIME);
  mcs_pop_root(vm,1);
 }
 mcs_value_t text=mcs_string(vm,"{\"ok\":true}"),out=mcs_null();CHECK(mcs_call(vm,"Json.Parse",1,&text,&out)==MCS_OK);
 mcs_free(vm);CHECK(h.live==0);
}
#endif
int main(void){
#if MCS_ENABLE_JSON && MCS_ENABLE_LIST && MCS_ENABLE_DICT
#if MCS_ENABLE_COMPILER
 fault("Json.Parse",0);fault("Json.Serialize",1);dictionary_retry();dictionary_storage();
#endif
 numeric_bounds();invalid_bytes();fuzz_bytes();pool_repeat();
#endif
 printf("JSON: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
