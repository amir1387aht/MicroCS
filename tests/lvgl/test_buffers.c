#include "mcs_lvgl.h"
#include "mcs_lvgl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"buffer FAIL %d: %s\n",__LINE__,#x);}}while(0)
static void quiet(void* p,const char* s,size_t n){(void)p;(void)s;(void)n;}
static int run(mcs_vm_t* vm,const char* code){int rc=mcs_exec_source(vm,"buffers.cs",code);if(rc)fprintf(stderr,"%s\nsource: %.300s\n",mcs_last_error(vm),code);CHECK(rc==MCS_OK);return rc;}
typedef struct { mcs_lvgl_config_t base;unsigned live,peak;bool fail,owner;void* ptrs[128];size_t sizes[128]; } host_t;
static void lock(void* p){host_t* h=p;h->base.lock(h->base.ctx);}
static void unlock(void* p){host_t* h=p;h->base.unlock(h->base.ctx);}
static bool owner(void* p){host_t* h=p;return h->owner && h->base.is_vm_thread(h->base.ctx);}
static void* alloc(void* p,size_t n){host_t* h=p;if(h->fail)return NULL;void* ptr=h->base.alloc(h->base.ctx,n);if(ptr){unsigned i;for(i=0;i<128 && h->ptrs[i];++i){}if(i==128)abort();h->ptrs[i]=ptr;h->sizes[i]=n;if(++h->live>h->peak)h->peak=h->live;}return ptr;}
static void release(void* p,void* ptr){host_t* h=p;unsigned i;for(i=0;i<128 && h->ptrs[i]!=ptr;++i){}if(i==128 || !h->live)abort();memset(ptr,0xa5,h->sizes[i]);h->ptrs[i]=NULL;--h->live;h->base.free(h->base.ctx,ptr);}
static mcs_lvgl_config_t config(host_t* h,const mcs_lvgl_config_t* base){memset(h,0,sizeof(*h));h->base=*base;h->owner=true;mcs_lvgl_config_t c=*base;c.ctx=h;c.lock=lock;c.unlock=unlock;c.is_vm_thread=owner;c.alloc=alloc;c.free=release;return c;}
static mcs_vm_t* new_vm(const mcs_lvgl_config_t* cfg){mcs_config_t c;mcs_config_default(&c);c.write_fn=c.error_fn=quiet;mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(vm)CHECK(mcs_lvgl_attach(vm,cfg)==0);return vm;}
#if LV_USE_GRID || LV_USE_LINE
typedef struct {mcs_pool_t pool;int at,calls;bool inject,injected;} fault_t;
static void* failing(void* p,void* ptr,size_t old,size_t n){fault_t* f=p;if(n && f->inject && f->calls++==f->at){f->injected=true;return NULL;}return mcs_pool_realloc(&f->pool,ptr,old,n);}
static void test_faults(const mcs_lvgl_config_t* base){
    bool done=false;
    for(int at=0;at<1200 && !done;++at){
        unsigned char heap[128*1024];fault_t f={0};mcs_pool_init(&f.pool,heap,sizeof heap);
        host_t h;mcs_lvgl_config_t cfg=config(&h,base);mcs_config_t c;mcs_config_default(&c);c.write_fn=c.error_fn=quiet;c.realloc_fn=failing;c.alloc_ud=&f;
        mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(!vm)break;CHECK(mcs_lvgl_attach(vm,&cfg)==0);
        f.at=at;f.inject=true;
        const char* code="var panel=new LvObject(\"Object\");"
#if LV_USE_GRID
          "panel.SetGrid(new int[]{20,Lvgl.GridFraction(1)},new int[]{30});"
#endif
#if LV_USE_LINE
          "var line=new LvObject(\"Line\",panel);line.SetPoints(new int[]{0,0,40,20});GC.Collect();line.SetPoints(new int[]{0,0,20,10});"
#endif
          "panel.Dispose();Lvgl.Dispatch(0);";
        int rc=mcs_exec_source(vm,"buffer-fault.cs",code);f.inject=false;
        if(!f.injected){CHECK(rc==MCS_OK);done=true;}
        mcs_free(vm);CHECK(!h.live && !f.pool.used);CHECK(mcs_lv_child_count(base->root)==0);
    }
    CHECK(done);
}
#endif
int test_lvgl_buffers(const mcs_lvgl_config_t* base){
    host_t h;mcs_lvgl_config_t cfg=config(&h,base);mcs_vm_t* vm=new_vm(&cfg);if(!vm)return failures;
    run(vm,"var panel=new LvObject(\"Object\");panel.SetSize(180,100);");CHECK(h.live==1);
#if LV_USE_GRID
    run(vm,"int[] columns={20,Lvgl.GridFraction(1)};int[] rows={40};panel.SetGrid(columns,rows);columns[0]=999;rows[0]=999;columns=null;rows=null;GC.Collect();var cell=new LvObject(\"Object\",panel);cell.Call(\"lv_obj_set_grid_cell\",3,1,1,3,0,1);panel.Call(\"lv_obj_update_layout\");");CHECK(h.live==2);
    lock(&h);lv_obj_t* container=lv_obj_get_child(base->root,0);lv_obj_t* panel=lv_obj_get_child(container,0);
    const void* cols=lv_obj_get_style_grid_column_dsc_array(panel,0);const void* rows=lv_obj_get_style_grid_row_dsc_array(panel,0);
#if LVGL_VERSION_MAJOR == 8
    CHECK(((const lv_coord_t*)cols)[0]==20 && ((const lv_coord_t*)rows)[0]==40);
#else
    CHECK(((const int32_t*)cols)[0]==20 && ((const int32_t*)rows)[0]==40);
#endif
    lv_tick_inc(40);lv_timer_handler();unlock(&h);
    run(vm,"bool small=false;try{panel.SetGrid(new int[]{20},new int[]{40});}catch(ArgumentException e){small=true;}if(!small)throw new Exception(\"grid shrink\");bool span=false;try{cell.Call(\"lv_obj_set_grid_cell\",3,1,2,3,0,1);}catch(ArgumentException e){span=true;}if(!span)throw new Exception(\"grid span\");bool style=false;try{cell.Call(\"lv_obj_set_style_grid_cell_column_pos\",255,0);}catch(ArgumentException e){style=true;}if(!style)throw new Exception(\"grid style index\");bool state=false;try{cell.Call(\"lv_obj_set_style_grid_cell_column_pos\",1,1);}catch(ArgumentException e){state=true;}if(!state)throw new Exception(\"grid state selector\");");CHECK(h.live==2);
    run(vm,"var other=new LvObject(\"Object\");other.SetGrid(new int[]{20},new int[]{20});bool parent=false;try{cell.Call(\"lv_obj_set_parent\",other);}catch(ArgumentException e){parent=true;}if(!parent)throw new Exception(\"grid reparent\");other.Dispose();Lvgl.Dispatch(0);");CHECK(h.live==2);
    h.fail=true;run(vm,"bool oom=false;try{panel.SetGrid(new int[]{30,40},new int[]{40});}catch(OutOfMemoryException e){oom=true;}if(!oom)throw new Exception(\"grid OOM\");");h.fail=false;CHECK(h.live==2);
    run(vm,"for(int n=0;n<40;n++)panel.SetGrid(new int[]{20,40},new int[]{40});panel.Call(\"lv_obj_update_layout\");bool bad=false;try{panel.SetGrid(new int[]{0},new int[]{20});}catch(ArgumentException e){bad=true;}if(!bad)throw new Exception(\"zero track\");bool fraction=false;try{Lvgl.GridFraction(100);}catch(ArgumentException e){fraction=true;}if(!fraction)throw new Exception(\"sentinel fraction\");bool sentinel=false;try{panel.SetGrid(new int[]{Lvgl.GridFraction(99)+1},new int[]{20});}catch(ArgumentException e){sentinel=true;}if(!sentinel)throw new Exception(\"caller sentinel\");bool capacity=false;try{panel.SetGrid(new int[256],new int[]{20});}catch(ArgumentException e){capacity=true;}if(!capacity)throw new Exception(\"grid capacity\");");CHECK(h.live==2);
#else
    run(vm,"bool noGrid=false;try{panel.SetGrid(new int[]{20},new int[]{20});}catch(NotSupportedException e){noGrid=true;}if(!noGrid)throw new Exception(\"disabled grid\");bool noFraction=false;try{Lvgl.GridFraction(1);}catch(NotSupportedException e){noFraction=true;}if(!noFraction)throw new Exception(\"disabled fraction\");");
#endif
#if LV_USE_LINE
    unsigned before=h.live;
    run(vm,"var line=new LvObject(\"Line\",panel);int[] xy={0,0,40,20};line.SetPoints(xy);xy[2]=999;xy=null;GC.Collect();if(line.PointCount!=2)throw new Exception(\"point count\");");CHECK(h.live==before+1);
    lock(&h);lv_obj_t* line=lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(base->root,0),0),-1);CHECK(lv_obj_get_self_width(line)>=40 && lv_obj_get_self_width(line)<100);lv_tick_inc(40);lv_timer_handler();unlock(&h);
    h.fail=true;run(vm,"bool oom=false;try{line.SetPoints(new int[]{0,0,80,20});}catch(OutOfMemoryException e){oom=true;}if(!oom || line.PointCount!=2)throw new Exception(\"line OOM\");");h.fail=false;CHECK(h.live==before+1);
    h.owner=false;run(vm,"bool owner=false;try{line.SetPoints(new int[]{0,0,20,10});}catch(InvalidOperationException e){owner=true;}if(!owner)throw new Exception(\"line owner\");");h.owner=true;CHECK(h.live==before+1);
    run(vm,"for(int n=0;n<40;n++)line.SetPoints(new int[]{0,0,30,10});bool odd=false;try{line.SetPoints(new int[]{0,0,1});}catch(ArgumentException e){odd=true;}if(!odd)throw new Exception(\"odd points\");bool range=false;try{line.SetPoints(new int[]{0,0,5000,0});}catch(ArgumentException e){range=true;}if(!range)throw new Exception(\"point range\");bool kind=false;try{panel.SetPoints(new int[]{0,0,20,10});}catch(NotSupportedException e){kind=true;}if(!kind)throw new Exception(\"point kind\");bool type=false;try{line.SetPoints(new object[]{0,0,\"bad\",0});}catch(ArgumentException e){type=true;}if(!type)throw new Exception(\"point type\");bool capacity=false;try{line.SetPoints(new int[1024]);}catch(ArgumentException e){capacity=true;}if(!capacity)throw new Exception(\"point capacity\");line.SetPoints(new int[0]);if(line.PointCount!=0)throw new Exception(\"clear points\");");CHECK(h.live==before);
    run(vm,"line.SetPoints(new int[]{0,0,20,10});");CHECK(h.live==before+1);
#else
    run(vm,"bool noLine=false;try{panel.SetPoints(new int[0]);}catch(NotSupportedException e){noLine=true;}if(!noLine)throw new Exception(\"disabled line\");");
#endif
    lock(&h);lv_obj_t* doomed=lv_obj_get_child(lv_obj_get_child(base->root,0),0);mcs_lv_obj_delete(doomed);unlock(&h);
    run(vm,"Lvgl.Dispatch(0);if(panel.IsAlive)throw new Exception(\"native deletion\");");CHECK(h.live==1);
#if LV_USE_LINE
    run(vm,"bool stale=false;try{line.SetPoints(new int[]{0,0,20,10});}catch(InvalidOperationException e){stale=true;}if(!stale)throw new Exception(\"stale line\");");CHECK(h.live==1);
#endif
    mcs_free(vm);CHECK(!h.live);
#if LV_USE_GRID || LV_USE_LINE
    test_faults(base);
#endif
    printf("LVGL copied buffers: %d checks, %d failures; peak wrapper blocks=%u\n",checks,failures,h.peak);return failures;
}
