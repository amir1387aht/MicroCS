/* Independent trusted internal invariant test: shared-layout bit and pending bit.
 * This is NOT a registration failure sweep or a public script feature claim. */
#include "mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef union { size_t n; long double align; void *ptr; } header_t;
static size_t live,bytes;static unsigned checks,failures;
#define T(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);}}while(0)
static void* alloc(void*u,void*p,size_t hint,size_t n){(void)u;(void)hint;header_t*h=p?(header_t*)p-1:NULL;size_t old=h?h->n:0;if(!n){if(h){live--;bytes-=old;free(h);}return NULL;}header_t*q=realloc(h,sizeof(*q)+n);if(!q)return NULL;q->n=n;bytes=bytes-old+n;if(!p)live++;return q+1;}
int main(void){mcs_config_t cfg;mcs_config_default(&cfg);cfg.realloc_fn=alloc;cfg.heap_limit=262144;cfg.stack_slots=512;mcs_vm_t*v=mcs_new(&cfg);T(v!=NULL);if(!v)return 1;
 v->gc_pause++;mcs_class_t*base=mcs_new_class(v,mcs_intern_c(v,"ReviewBase"),CLS_SCRIPT);mcs_push_root(v,OBJ_VAL(base));mcs_string_t*a=mcs_intern_c(v,"A");mcs_class_add_field(v,base,a,mcs_int(7));v->gc_pause--;
 for(unsigned copy=0;copy<2;copy++){
  v->gc_pause++;mcs_class_t*child=mcs_new_class(v,mcs_intern_c(v,copy?"ReviewOwn":"ReviewBorrow"),CLS_SCRIPT);mcs_push_root(v,OBJ_VAL(child));mcs_class_inherit_shared(v,child,base);child->layout_shared|=MCS_CLASS_PENDING_REGS;
  T((child->layout_shared&MCS_CLASS_SHARED_LAYOUT)!=0);T(child->field_defaults==base->field_defaults);T(child->field_count==1);
  if(copy){mcs_class_add_field(v,child,mcs_intern_c(v,"B"),mcs_int(9));T(!(child->layout_shared&MCS_CLASS_SHARED_LAYOUT));T(child->layout_shared&MCS_CLASS_PENDING_REGS);T(child->field_defaults!=base->field_defaults);T(child->field_count==2);T(base->field_count==1);}
  v->gc_pause--;mcs_pop_root(v,1);mcs_gc(v);mcs_value_t slot;T(mcs_table_get_s(&base->fields,a,&slot));T(base->field_defaults[mcs_to_int(v,slot)].as.i==7);
 }
 mcs_pop_root(v,1);mcs_gc(v);mcs_free(v);T(live==0);T(bytes==0);printf("%u/%u independent shared-layout/pending-bit ownership checks; physical allocation tracking\n",checks-failures,checks);return failures?1:0;}
