#include "mcs_lvgl.h"
#include "mcs_lvgl_compat.h"
#if LVGL_VERSION_MAJOR == 8
#define lv_obj_send_event lv_event_send
#define lv_obj_delete lv_obj_del
#define lv_obj_get_child_count lv_obj_get_child_cnt
#define lv_display_t lv_disp_t
#define lv_display_get_screen_active lv_disp_get_scr_act
#define lv_display_delete lv_disp_remove
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

void mcs_test_lvgl_assert(const char* file, int line) { fprintf(stderr,"LVGL assertion %s:%d\n",file,line);abort(); }
static int checks, failures, flushes;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)
typedef struct { int locked, allocations, fail_alloc; bool owner; pthread_mutex_t mutex; pthread_t vm_thread; } host_t;
static void lock(void* p) { host_t* h=p; pthread_mutex_lock(&h->mutex); if(h->locked)abort(); ++h->locked; }
static void unlock(void* p) { host_t* h=p; if(h->locked!=1)abort(); --h->locked; pthread_mutex_unlock(&h->mutex); }
static bool owner(void* p) { host_t* h=p; return h->owner && pthread_equal(pthread_self(),h->vm_thread); }
static void* alloc(void* p,size_t n) { host_t* h=p; if(h->fail_alloc)return NULL;void* v=malloc(n);if(v)++h->allocations;return v; }
static void release(void* p,void* v) { --((host_t*)p)->allocations;free(v); }
static void quiet(void* ud,const char* p,size_t n) { (void)ud;(void)p;(void)n; }
static mcs_vm_t* new_vm(void) { mcs_config_t c;mcs_config_default(&c);c.write_fn=quiet;c.error_fn=quiet;return mcs_new(&c); }
static void run(mcs_vm_t* vm,const char* s) { mcs_result_t rc=mcs_exec_source(vm,"lvgl.cs",s);if(rc!=MCS_OK)fprintf(stderr,"%s\n",mcs_last_error(vm));CHECK(rc==MCS_OK); }
#if LVGL_VERSION_MAJOR == 8
static void flush(lv_disp_drv_t* d,const lv_area_t* a,lv_color_t* p) { (void)a;(void)p;++flushes;lv_disp_flush_ready(d); }
#else
static void flush(lv_display_t* d,const lv_area_t* a,uint8_t* p) { (void)a;(void)p;++flushes;lv_display_flush_ready(d); }
#endif
static mcs_lvgl_config_t config(host_t* h,lv_obj_t* root) { mcs_lvgl_config_t c={root,h,lock,unlock,owner,alloc,release};return c; }
typedef struct { mcs_pool_t pool; int at,calls,enabled,injected; } fault_t;
static void* fault_realloc(void* ud,void* p,size_t old,size_t n) {
    fault_t* f=ud;if(n && f->enabled && f->calls++==f->at){f->injected=1;return NULL;}return mcs_pool_realloc(&f->pool,p,old,n);
}
static void test_faults(host_t* h,lv_obj_t* root) {
    int done=0;
    for(int i=0;i<2000;++i) {
        uint8_t heap[128*1024];fault_t f={0};mcs_pool_init(&f.pool,heap,sizeof heap);
        mcs_config_t c;mcs_config_default(&c);c.realloc_fn=fault_realloc;c.alloc_ud=&f;c.write_fn=quiet;c.error_fn=quiet;
        mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(!vm)break;
        mcs_lvgl_config_t cfg=config(h,root);CHECK(mcs_lvgl_attach(vm,&cfg)==0);
        f.at=i;f.enabled=1;
        int rc=mcs_exec_source(vm,"fault.cs","var p=new LvObject(\"Object\");var c=new LvObject(\"Object\",p);p.OnClicked(()=>{});c.SetSize(10,10);");
        f.enabled=0;if(!f.injected){CHECK(rc==MCS_OK);done=1;}
        mcs_free(vm);CHECK(!f.pool.used && !h->allocations && !h->locked && lv_obj_get_child_count(root)==0);
        if(done)break;
    }
    CHECK(done);
}
typedef struct { host_t* host; lv_obj_t* root; } producer_t;
static void* producer(void* arg) {
    producer_t* p=arg;
    for(int i=0;i<10000;++i) {
        /* Native task discovers a live widget only while holding the GUI mutex. */
        lock(p->host);
        lv_obj_t* container=lv_obj_get_child(p->root,0);
        lv_obj_t* child=container ? lv_obj_get_child(container,0) : NULL;
        if(child)lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);
        unlock(p->host);
    }
    return NULL;
}
static void test_threads(host_t* h,lv_obj_t* root) {
    producer_t context={h,root};pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,producer,&context));
    mcs_lvgl_config_t c=config(h,root);
    for(int i=0;i<50;++i) {
        mcs_vm_t* vm=new_vm();CHECK(mcs_lvgl_attach(vm,&c)==0);
        run(vm,"int count=0;var w=new LvObject(\"Object\");w.OnClicked(()=>{count++;});Lvgl.Dispatch(16);");
        mcs_free(vm); /* races with event production, never VM execution */
    }
    CHECK(!pthread_join(thread,NULL));CHECK(!h->allocations && !h->locked);
}
static void test_attach_faults(host_t* h,lv_obj_t* root) {
    bool completed=false;
    for(int i=0;i<1000;++i) {
        uint8_t heap[128*1024];fault_t f={0};mcs_pool_init(&f.pool,heap,sizeof heap);
        mcs_config_t c;mcs_config_default(&c);c.realloc_fn=fault_realloc;c.alloc_ud=&f;c.write_fn=quiet;c.error_fn=quiet;
        mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(!vm)break;
        mcs_lvgl_config_t cfg=config(h,root);f.at=i;f.enabled=1;
        int rc=mcs_lvgl_attach(vm,&cfg);f.enabled=0;
        if(!f.injected){CHECK(rc==MCS_LVGL_OK);completed=true;}
        else { CHECK(rc==MCS_LVGL_NO_MEMORY);CHECK(!h->allocations && !h->locked && lv_obj_get_child_count(root)==0); }
        mcs_free(vm);CHECK(!f.pool.used && !h->allocations && !h->locked && lv_obj_get_child_count(root)==0);
        if(completed)break;
    }
    CHECK(completed);
}
int main(void) {

    host_t h={0};h.owner=true;pthread_mutex_init(&h.mutex,NULL);h.vm_thread=pthread_self();
    lv_init();
#if LVGL_VERSION_MAJOR == 8
    static lv_color_t draw[128*8];static lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,draw,NULL,128*8);
    static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=128;drv.ver_res=64;drv.draw_buf=&buf;drv.flush_cb=flush;
    lv_display_t* d=lv_disp_drv_register(&drv);CHECK(d!=NULL);
#else
    lv_display_t* d=lv_display_create(128,64);CHECK(d!=NULL);
    static uint8_t draw[128*8*2];lv_display_set_buffers(d,draw,NULL,sizeof draw,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
#endif
    lv_obj_t* screen=lv_display_get_screen_active(d);
    lv_obj_t* host_widget=lv_obj_create(screen);
    lv_obj_t* root=lv_obj_create(screen);
    mcs_lvgl_config_t cfg=config(&h,root);
    mcs_vm_t* vm=new_vm();CHECK(vm!=NULL);
    h.fail_alloc=1;CHECK(mcs_lvgl_attach(vm,&cfg)==MCS_LVGL_NO_MEMORY);h.fail_alloc=0;
    CHECK(mcs_lvgl_attach(vm,&cfg)==0);CHECK(mcs_lvgl_attach(vm,&cfg)==MCS_LVGL_ALREADY_ATTACHED);
    run(vm,"if(!Lvgl.Ready || !Lvgl.Supports(\"Object\") || Lvgl.Supports(\"Unknown\"))throw new Exception(\"capability\");var p=new LvObject(\"Object\");var child=new LvObject(\"Object\",p);int count=0;child.OnClicked(()=>{count++;});GC.Collect();");
    lv_obj_t* container=lv_obj_get_child(root,0);lv_obj_t* p=lv_obj_get_child(container,0);lv_obj_t* child=lv_obj_get_child(p,0);
    lock(&h);for(int i=0;i<MCS_LVGL_EVENT_QUEUE+4;++i)lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);unlock(&h);
    run(vm,"if(Lvgl.DroppedEvents!=4)throw new Exception(\"overflow\");Lvgl.Dispatch(16);if(count!=16)throw new Exception(\"pin\");");
    lock(&h);lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);unlock(&h);
    run(vm,"child.OnClicked(()=>{count+=100;});Lvgl.Dispatch(16);if(count!=16)throw new Exception(\"old registration\");");
    lock(&h);lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);lv_obj_delete(p);unlock(&h);
    run(vm,"Lvgl.Dispatch(16);if(count!=16 || child.IsAlive || p.IsAlive)throw new Exception(\"delete\");p.Dispose();child.Dispose();bool caught=false;try{child.Center();}catch(Exception e){caught=true;}if(!caught)throw new Exception(\"stale\");var again=new LvObject(\"Object\");if(child.IsAlive)throw new Exception(\"reuse\");");
#if LV_USE_LABEL
    run(vm,"var label=new LvObject(\"Label\");string text=\"hello\"+\" world\";label.Text=text;label.SetTextColor(0xabcdef);label.SetBackground(0x123456);label.SetSize(70,20);label.SetPosition(2,3);GC.Collect();");
    lv_obj_t* label=lv_obj_get_child(container,-1);CHECK(!strcmp(lv_label_get_text(label),"hello world"));
#endif
#if LV_USE_SLIDER && LV_USE_BAR && LV_USE_ARC && LV_USE_CHECKBOX && LV_USE_SWITCH && MCS_LV_USE_BUTTON
    run(vm,"var slider=new LvObject(\"Slider\");slider.Value=25;if(slider.Value!=25)throw new Exception(\"value\");var bar=new LvObject(\"Bar\");bar.Value=40;var arc=new LvObject(\"Arc\");arc.Value=50;var box=new LvObject(\"Checkbox\");box.Text=\"check\";box.Checked=true;var sw=new LvObject(\"Switch\");sw.Checked=true;if(!box.Checked||!sw.Checked)throw new Exception(\"state\");var button=new LvObject(\"Button\");");
#endif
#if !LV_USE_LABEL
    run(vm,"if(Lvgl.Supports(\"Label\"))throw new Exception(\"disabled capability\");bool unavailable=false;try{var bad=new LvObject(\"Label\");}catch(Exception e){unavailable=true;}if(!unavailable)throw new Exception(\"disabled widget\");");
#endif
    run(vm,"bool range=false;try{again.SetSize(-1,10);}catch(Exception e){range=true;}if(!range)throw new Exception(\"range\");");
    h.owner=false;
    CHECK(mcs_exec_source(vm,"wrong.cs","again.Center();")!=MCS_OK);CHECK(!h.locked);h.owner=true;
    run(vm,"int errors=0;again.OnClicked(()=>{throw new Exception(\"callback\");});");
    lv_obj_t* again=lv_obj_get_child(container,0);lock(&h);lv_obj_send_event(again,LV_EVENT_CLICKED,NULL);unlock(&h);
    CHECK(mcs_exec_source(vm,"error.cs","Lvgl.Dispatch(16);")!=MCS_OK);
    run(vm,"again.ClearEvent();Lvgl.Dispatch(16);");
    lock(&h);lv_tick_inc(40);lv_timer_handler();unlock(&h);CHECK(flushes>0);
    clock_t start=clock();
    run(vm,"for(int n=0;n<10000;n++){again.SetPosition(n%50,n%20);}");
    double script_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
    lock(&h);start=clock();for(int n=0;n<10000;++n)lv_obj_set_pos(again,n%50,n%20);
    double native_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;unlock(&h);
    printf("10000 position updates: script %.3f ms (includes compilation), native %.3f ms; host-only, instrumented when SANITIZE=ON\n",script_ms,native_ms);
    mcs_mem_stats_t st;mcs_mem_stats(vm,&st);
    printf("attachment=%zu native bytes, handle=%zu bytes; active VM=%zu bytes (not added to device heap)\n",mcs_lvgl_storage_size(),sizeof(unsigned)+sizeof(uint32_t),st.bytes_in_use);
    mcs_free(vm);CHECK(!h.allocations && !h.locked);CHECK(lv_obj_is_valid(host_widget));CHECK(lv_obj_get_child_count(root)==0);
    for(int i=0;i<100;++i) { vm=new_vm();CHECK(mcs_lvgl_attach(vm,&cfg)==0);run(vm,"var w=new LvObject(\"Object\");w.OnClicked(()=>{});var c=new LvObject(\"Object\",w);");mcs_free(vm);CHECK(lv_obj_get_child_count(root)==0 && !h.allocations); }
    test_faults(&h,root);test_attach_faults(&h,root);test_threads(&h,root);
    vm=new_vm();CHECK(mcs_lvgl_attach(vm,&cfg)==0);run(vm,"var w=new LvObject(\"Object\");");lock(&h);lv_obj_delete(root);unlock(&h);run(vm,"if(Lvgl.Ready||w.IsAlive)throw new Exception(\"host deletion\");");mcs_free(vm);CHECK(!h.allocations);
#if LVGL_VERSION_MAJOR != 8 || LVGL_VERSION_MINOR != 0
    vm=new_vm();cfg.root=host_widget;lv_display_delete(d);CHECK(mcs_lvgl_attach(vm,&cfg)==MCS_LVGL_NOT_READY);mcs_free(vm);
#else
    /* Upstream 8.0 removes its display from the list BEFORE deleting screens;
     * lv_obj_del cannot find the owning display and lv_disp_remove loops.
     * This host teardown is unrelated to the wrapper (which never deletes a display).
     * Its fixed-pool lv_deinit clears native test state instead. */
    puts("SKIP upstream LVGL 8.0 display removal; wrapper VM teardown was tested");
#endif
    lv_deinit();
    pthread_mutex_destroy(&h.mutex);
    printf("%d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
