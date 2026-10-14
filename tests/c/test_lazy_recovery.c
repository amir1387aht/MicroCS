/* Trusted ordinary source: restore allocator after first-use registration OOM.
 * Tracks physical pointer ownership independently of allocator old-size hints. */
#include "../../src/mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { void *p; size_t bytes; } owned_t;
static owned_t owned[8192];
static unsigned checks, failures;
static int fail_next, constructions;
static size_t physical_bytes, live_allocations;
#define CHECK(c) do { checks++; if(!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)
static void quiet(void *ud, const char *s, size_t n) { (void)ud; (void)s; (void)n; }
static void *physical_alloc(void *ud, void *p, size_t hint, size_t n) {
    (void)ud; (void)hint;
    unsigned i=0;
    if(p) { for(;i<8192;i++) if(owned[i].p==p) break; if(i==8192) { failures++; return NULL; } }
    else { for(;i<8192;i++) if(!owned[i].p) break; if(i==8192) { failures++; return NULL; } }
    if(!n) {
        if(p) { physical_bytes-=owned[i].bytes; live_allocations--; owned[i].p=NULL; owned[i].bytes=0; free(p); }
        return NULL;
    }
    if(fail_next && --fail_next==0) return NULL;
    void *out=realloc(p,n);
    if(out) { physical_bytes=physical_bytes-owned[i].bytes+n; if(!p) live_allocations++; owned[i].p=out; owned[i].bytes=n; }
    return out;
}
static mcs_value_t next_failure(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm; (void)self; (void)argc; (void)argv; fail_next=1; return mcs_null();
}
static mcs_value_t second_failure(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm; (void)self; (void)argc; (void)argv; fail_next=2; return mcs_null();
}
static mcs_value_t third_failure(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm; (void)self; (void)argc; (void)argv; fail_next=3; return mcs_null();
}
static void owned_ctor(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm; (void)argc; (void)argv; constructions++; *(int *)mcs_userdata(self)=42;
}
static mcs_value_t owned_value(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm; (void)argc; (void)argv; return mcs_int(*(int *)mcs_userdata(self));
}
static const mcs_reg_t members[]={MCS_GET("Value",owned_value),MCS_REG_END};
static const mcs_class_def_t late_def={"LateOwned",sizeof(int),owned_ctor,NULL,members,NULL};
static const mcs_class_def_t partial_def={"PartialLateOwned",sizeof(int),owned_ctor,NULL,members,NULL};
static const mcs_class_def_t deep_def={"DeepLateOwned",sizeof(int),owned_ctor,NULL,members,NULL};
static const mcs_class_def_t raw_def={"RawLateOwned",sizeof(int),owned_ctor,NULL,members,NULL};
static const mcs_class_def_t nested_def={"NestedLateOwned",sizeof(int),owned_ctor,NULL,members,NULL};
static mcs_value_t nested_lookup(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self; (void)argc; (void)argv; return mcs_get_global(vm,"NestedLateOwned");
}
static const mcs_class_def_t held_def={"HeldOwned",sizeof(int),owned_ctor,NULL,members,NULL};
int main(void) {
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.realloc_fn=physical_alloc; cfg.write_fn=quiet; cfg.error_fn=quiet;
    mcs_vm_t *vm=mcs_new(&cfg); CHECK(vm!=NULL); if(!vm) return 1;
    mcs_register_function(vm,"FailNext",next_failure,0);
    mcs_register_function(vm,"FailThird",third_failure,0);
    mcs_register_function(vm,"FailSecond",second_failure,0);
    mcs_register_function(vm,"NestedLookup",nested_lookup,0);
    mcs_register_class(vm,&late_def); mcs_register_class(vm,&held_def);
    mcs_register_class(vm,&deep_def); mcs_register_class(vm,&raw_def);
    mcs_register_class(vm,&nested_def); mcs_register_class(vm,&partial_def);
    CHECK(mcs_exec_source(vm,"existing.cs","var kept=new HeldOwned(); int saved=99;") == MCS_OK);
    CHECK(mcs_exec_source(vm,"first-use.cs","FailNext(); var firstUse=new LateOwned();") == MCS_ERR_MEMORY);
    CHECK(fail_next==0);
    mcs_result_t retry=mcs_exec_source(vm,"retry.cs","var recovered=new LateOwned(); if(recovered.Value!=42 || kept.Value!=42 || saved!=99) throw new Exception(\"state changed\");");
    if(retry!=MCS_OK) printf("retry result=%d error=%s\n",(int)retry,mcs_last_error(vm));
    CHECK(retry==MCS_OK);
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    CHECK(mcs_exec_source(vm,"partial-class.cs","FailThird(); var partialUse=new DeepLateOwned();") == MCS_ERR_MEMORY);
    CHECK(fail_next==0);
    mcs_string_t *partial_name=mcs_find_interned(vm,deep_def.name,strlen(deep_def.name));
    CHECK(partial_name && partial_name->obj.aux);
    /* Allocation three is the instance, after the complete class is published. */
    CHECK(partial_name && partial_name->obj.aux && IS_KIND(vm->globals[partial_name->obj.aux-1],MCS_O_CLASS) && AS_CLASS(vm->globals[partial_name->obj.aux-1])->def==&deep_def);
    CHECK(constructions==2);
#endif
    CHECK(mcs_exec_source(vm,"partial-retry.cs","var completeUse=new DeepLateOwned(); if(completeUse.Value!=42 || kept.Value!=42) throw new Exception(\"partial publication\");") == MCS_OK);
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    /* Allocation two is ROM registration metadata, before class publication. */
    CHECK(mcs_exec_source(vm,"metadata-class.cs","FailSecond(); var metadataUse=new PartialLateOwned();") == MCS_ERR_MEMORY);
    CHECK(fail_next==0);
    mcs_string_t *metadata_name=mcs_find_interned(vm,partial_def.name,strlen(partial_def.name));
    CHECK(metadata_name && metadata_name->obj.aux && vm->globals[metadata_name->obj.aux-1].type==MCS_T_UNDEF);
    CHECK(constructions==3);
#endif
    CHECK(mcs_exec_source(vm,"metadata-retry.cs","var metadataRecovered=new PartialLateOwned(); if(metadataRecovered.Value!=42) throw new Exception(\"metadata retry\");") == MCS_OK);
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    const int entry_pause=vm->gc_pause, entry_roots=vm->root_count;
    mcs_push_root(vm,mcs_get_global(vm,"kept")); vm->gc_pause=entry_pause+2;
    fail_next=1;
    CHECK(mcs_is_null(mcs_get_global(vm,"RawLateOwned")));
    CHECK(fail_next==0);
    CHECK(vm->gc_pause==entry_pause+2 && vm->root_count==entry_roots+1);
    CHECK(!mcs_is_null(mcs_get_global(vm,"RawLateOwned")));
    CHECK(vm->gc_pause==entry_pause+2 && vm->root_count==entry_roots+1);
    vm->gc_pause=entry_pause; mcs_pop_root(vm,1);
    mcs_mem_stats_t before_gc, after_gc;
    mcs_mem_stats(vm,&before_gc); mcs_gc(vm); mcs_mem_stats(vm,&after_gc);
    CHECK(after_gc.collections>before_gc.collections);
#endif
    CHECK(!mcs_is_null(mcs_get_global(vm,"RawLateOwned")));
    CHECK(mcs_exec_source(vm,"raw-retry.cs","var rawUse=new RawLateOwned(); if(rawUse.Value!=42) throw new Exception(\"raw lookup retry\");") == MCS_OK);
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    const int nested_pause=vm->gc_pause, nested_roots=vm->root_count;
    mcs_push_root(vm,mcs_get_global(vm,"kept")); vm->gc_pause=nested_pause+3;
    CHECK(mcs_exec_source(vm,"nested-lookup.cs","FailNext(); NestedLookup();") == MCS_ERR_MEMORY);
    CHECK(fail_next==0);
    CHECK(vm->gc_pause==nested_pause+3 && vm->root_count==nested_roots+1);
    CHECK(mcs_last_error(vm)[0]!=0);
    vm->gc_pause=nested_pause; mcs_pop_root(vm,1);
#endif
    CHECK(mcs_exec_source(vm,"nested-retry.cs","var nestedUse=new NestedLateOwned(); if(nestedUse.Value!=42 || kept.Value!=42) throw new Exception(\"nested lookup retry\");") == MCS_OK);
    CHECK(constructions==6);
    mcs_gc(vm);
    CHECK(mcs_exec_source(vm,"again.cs","if(kept.Value!=42 || recovered.Value!=42) throw new Exception(\"GC state changed\");") == MCS_OK);
    CHECK(mcs_exec_source(vm,"legitimate-miss.cs","for(int i=0;i<3;i++){try { var missing=kept.NoSuchMember; throw new Exception(\"missing became present\"); } catch(MissingMemberException e) {}}") == MCS_OK);
    CHECK(mcs_exec_source(vm,"cached.cs","for(int i=0;i<50;i++) if(kept.Value!=42) throw new Exception(\"cached value\");") == MCS_OK);
    CHECK(constructions==6);
    mcs_free(vm);
    if(live_allocations) printf("physical outstanding: %zu pointers, %zu requested bytes\n",live_allocations,physical_bytes);
    CHECK(live_allocations==0); CHECK(physical_bytes==0);
    printf("%u/%u trusted lazy recovery checks passed\n",checks-failures,checks);
    return failures!=0;
}
