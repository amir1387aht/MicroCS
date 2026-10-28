#include "mcs_lvgl.h"
#include "mcs_lvgl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if LVGL_VERSION_MAJOR == 8
#define lv_obj_get_child_count lv_obj_get_child_cnt
#endif
static int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"native API FAIL %d: %s\n",__LINE__,#x);}}while(0)
static void quiet(void* u,const char* s,size_t n){(void)u;(void)s;(void)n;}
static void run(mcs_vm_t* vm,const char* code){int rc=mcs_exec_source(vm,"native.cs",code);if(rc)fprintf(stderr,"%s\nsource: %.240s\n",mcs_last_error(vm),code);CHECK(rc==MCS_OK);}
static mcs_vm_t* vm_new(const mcs_lvgl_config_t* cfg){mcs_config_t c;mcs_config_default(&c);c.write_fn=quiet;c.error_fn=quiet;mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(vm)CHECK(mcs_lvgl_attach(vm,cfg)==0);return vm;}
#include "native_cases.inc"
static void test_manifest(const mcs_lvgl_config_t* cfg){
    mcs_vm_t* vm=vm_new(cfg);if(!vm)return;
    run(vm,"var probe=new LvObject(\"Object\");");char code[768];
    for(size_t i=0;i<sizeof native_cases/sizeof native_cases[0];++i){
        snprintf(code,sizeof code,"if(Lvgl.HasApi(\"%s\")!=%s)throw new Exception(\"API availability\");",native_cases[i].name,native_cases[i].available ? "true" : "false");run(vm,code);
        if(native_cases[i].available){
            snprintf(code,sizeof code,"int id=Lvgl.Api(\"%s\");if(id<0)throw new Exception(\"ID\");",native_cases[i].name);run(vm,code);
            if(native_cases[i].argc){snprintf(code,sizeof code,"bool bad=false;try{probe.Call(\"%s\");}catch(ArgumentException e){bad=true;}if(!bad)throw new Exception(\"arity\");",native_cases[i].name);run(vm,code);}
            if(native_cases[i].smoke)run(vm,native_cases[i].smoke);
        }else{
            snprintf(code,sizeof code,"bool bad=false;try{Lvgl.Api(\"%s\");}catch(NotSupportedException e){bad=true;}if(!bad)throw new Exception(\"disabled API\");",native_cases[i].name);run(vm,code);
        }
    }
    for(size_t i=0;i<sizeof constant_cases/sizeof constant_cases[0];++i){
        snprintf(code,sizeof code,"if(Lvgl.HasConstant(\"%s\")!=%s)throw new Exception(\"constant availability\");",constant_cases[i].name,constant_cases[i].available ? "true" : "false");run(vm,code);
        if(constant_cases[i].available){snprintf(code,sizeof code,"int value=Lvgl.Constant(\"%s\");",constant_cases[i].name);run(vm,code);}
    }
    run(vm,"if(Lvgl.HasApi(\"not_an_api\")||Lvgl.HasConstant(\"not_a_constant\"))throw new Exception(\"unknown\");probe.Dispose();GC.Collect();");
    mcs_free(vm);
}
/* Count native snapshot resources; test alloc failure independently of LVGL's
 * own allocator (which may assert on OOM and is not intercepted here). */
#if LV_USE_LABEL
typedef struct { mcs_lvgl_config_t base;int live,fail; } native_host_t;
static void hlock(void* p){native_host_t* h=p;h->base.lock(h->base.ctx);}
static void hunlock(void* p){native_host_t* h=p;h->base.unlock(h->base.ctx);}
static bool howner(void* p){native_host_t* h=p;return h->base.is_vm_thread(h->base.ctx);}
static void* halloc(void* p,size_t n){native_host_t* h=p;if(h->fail)return NULL;void* v=h->base.alloc(h->base.ctx,n);if(v)++h->live;return v;}
static void hfree(void* p,void* v){native_host_t* h=p;--h->live;h->base.free(h->base.ctx,v);}
typedef struct {mcs_pool_t pool;int fail_at,calls,injected;} failing_t;
static void* failing_alloc(void* p,void* v,size_t old,size_t n){failing_t* f=p;if(n && f->fail_at>=0 && f->calls++==f->fail_at){f->injected=1;return NULL;}return mcs_pool_realloc(&f->pool,v,old,n);}
#endif
static void test_text_failures(const mcs_lvgl_config_t* base){
#if LV_USE_LABEL
    native_host_t h={*base,0,0};mcs_lvgl_config_t cfg=*base;cfg.ctx=&h;cfg.lock=hlock;cfg.unlock=hunlock;cfg.is_vm_thread=howner;cfg.alloc=halloc;cfg.free=hfree;
    mcs_vm_t* vm=vm_new(&cfg);if(!vm)return;
    run(vm,"var text=new LvObject(\"Label\");text.Text=\"native snapshot\";");
    h.fail=1;run(vm,"bool oom=false;try{text.Call(\"lv_label_get_text\");}catch(OutOfMemoryException e){oom=true;}if(!oom)throw new Exception(\"native OOM\");");h.fail=0;CHECK(h.live==1);
    run(vm,"string copy=text.Call(\"lv_label_get_text\");text.Dispose();GC.Collect();if(copy!=\"native snapshot\")throw new Exception(\"copied snapshot\");");mcs_free(vm);CHECK(h.live==0);
    bool done=false;
    for(int at=0;at<1000 && !done;++at){
        unsigned char heap[128*1024];failing_t f={0};f.fail_at=-1;mcs_pool_init(&f.pool,heap,sizeof heap);
        mcs_config_t c;mcs_config_default(&c);c.realloc_fn=failing_alloc;c.alloc_ud=&f;c.write_fn=quiet;c.error_fn=quiet;
        vm=mcs_new(&c);CHECK(vm!=NULL);if(!vm)break;CHECK(mcs_lvgl_attach(vm,&cfg)==0);
        run(vm,"var text=new LvObject(\"Label\");");
        hlock(&h);lv_obj_t* container=lv_obj_get_child(cfg.root,0);lv_obj_t* label=lv_obj_get_child(container,0);lv_label_set_text(label,"Host-only text not interned by the script");hunlock(&h);
        f.fail_at=at;f.calls=0;
        int rc=mcs_exec_source(vm,"snapshot.cs","string copy=text.Call(\"lv_label_get_text\");");
        f.fail_at=-1;if(!f.injected){CHECK(rc==MCS_OK);done=true;}
        CHECK(h.live==1);mcs_free(vm);CHECK(h.live==0 && !f.pool.used);
    }
    CHECK(done);
#else
    (void)base;
#endif
}
static void test_dispatch_cost(const mcs_lvgl_config_t* cfg){
    mcs_vm_t* vm=vm_new(cfg);if(!vm)return;
    run(vm,"var bench=new LvObject(\"Object\");int setX=Lvgl.Api(\"lv_obj_set_x\");");
    clock_t start=clock();
    run(vm,"for(int n=0;n<10000;n++)bench.Call(\"lv_obj_set_x\",n%50);");
    double name_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
    start=clock();run(vm,"for(int n=0;n<10000;n++)bench.Call(setX,n%50);");
    double id_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
    cfg->lock(cfg->ctx);lv_obj_t* container=lv_obj_get_child(cfg->root,0);lv_obj_t* obj=lv_obj_get_child(container,0);
    start=clock();for(int n=0;n<10000;++n)lv_obj_set_x(obj,n%50);
    double native_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;cfg->unlock(cfg->ctx);
    printf("10000 set-x calls: name %.3f ms, cached ID %.3f ms (both include script compilation), native %.3f ms; host-only, instrumented when SANITIZE=ON\n",name_ms,id_ms,native_ms);
    mcs_free(vm);
}
int test_lvgl_native(const mcs_lvgl_config_t* cfg){
    test_manifest(cfg);
    mcs_vm_t* vm=vm_new(cfg);if(!vm)return failures;
    run(vm,"var p=new LvObject(\"Object\");var child=new LvObject(\"Object\",p);int width=Lvgl.Api(\"lv_obj_set_width\");p.Call(width,100);p.Call(\"lv_obj_set_height\",40);p.Call(\"lv_obj_update_layout\");if(p.Call(\"lv_obj_get_width\")!=100)throw new Exception(\"geometry\");p.Call(\"lv_obj_set_style_bg_color\",0xff0000,0);if(p.Call(\"lv_obj_get_style_bg_color\",0)!=0xff0000)throw new Exception(\"style color\");int hidden=Lvgl.Constant(\"LV_OBJ_FLAG_HIDDEN\");p.Call(\"lv_obj_add_flag\",hidden);if(!p.Call(\"lv_obj_has_flag\",hidden))throw new Exception(\"flag\");p.Call(\"lv_obj_clear_flag\",hidden);int checkState=Lvgl.Constant(\"LV_STATE_CHECKED\");p.Call(\"lv_obj_add_state\",checkState);if(!p.Call(\"lv_obj_has_state\",checkState))throw new Exception(\"state\");p.Call(\"lv_obj_clear_state\",checkState);");
    if(LV_COORD_GET_PCT(LV_PCT(1001))>1000){
        char code[384];snprintf(code,sizeof code,"bool pct=false;try{p.Call(\"lv_obj_set_width\",%d);}catch(ArgumentException e){pct=true;}if(!pct)throw new Exception(\"encoded percentage\");",(int)LV_PCT(1001));run(vm,code);
    }
    run(vm,"bool cycle=false;try{p.Call(\"lv_obj_set_parent\",child);}catch(ArgumentException e){cycle=true;}if(!cycle)throw new Exception(\"cycle\");bool selector=false;try{p.Call(\"lv_obj_set_style_radius\",5,0x100);}catch(ArgumentException e){selector=true;}if(!selector)throw new Exception(\"selector\");bool fake=false;try{p.Call(\"lv_obj_set_parent\",123);}catch(ArgumentException e){fake=true;}if(!fake)throw new Exception(\"pointer accepted\");child.Call(\"lv_obj_set_parent\",p);p.Call(\"lv_obj_set_width\",Lvgl.Percent(50));p.Call(\"lv_obj_clean\");if(child.IsAlive)throw new Exception(\"clean\");bool stale=false;try{child.Call(\"lv_obj_get_width\");}catch(InvalidOperationException e){stale=true;}if(!stale)throw new Exception(\"stale\");p.Dispose();");
#if LV_USE_FLEX
    run(vm,"var flex=new LvObject(\"Object\");flex.Call(\"lv_obj_set_flex_flow\",Lvgl.Constant(\"LV_FLEX_FLOW_ROW_WRAP\"));flex.Call(\"lv_obj_set_flex_align\",0,0,0);bool flow=false;try{flex.Call(\"lv_obj_set_flex_flow\",2);}catch(ArgumentException e){flow=true;}if(!flow)throw new Exception(\"flow\");flex.Dispose();");
#endif
#if LV_USE_SLIDER
    run(vm,"var slider=new LvObject(\"Slider\");slider.Call(\"lv_slider_set_range\",-50,150);slider.Call(\"lv_slider_set_value\",125,0);if(slider.Call(\"lv_slider_get_value\")!=125)throw new Exception(\"range\");bool range=false;try{slider.Call(\"lv_slider_set_range\",10,0);}catch(ArgumentException e){range=true;}if(!range)throw new Exception(\"bad range\");bool wide=false;try{slider.Call(\"lv_slider_set_range\",-2147483647,2147483647);}catch(ArgumentException e){wide=true;}if(!wide)throw new Exception(\"overflow range\");slider.Dispose();");
#endif
#if LV_USE_DROPDOWN && LV_USE_ROLLER
    run(vm,"var dropdown=new LvObject(\"Dropdown\");dropdown.Call(\"lv_dropdown_set_options\",\"A\\nB\\nC\");dropdown.Call(\"lv_dropdown_set_selected\",2);if(dropdown.Call(\"lv_dropdown_get_selected\")!=2 || dropdown.Call(\"lv_dropdown_get_options\")!=\"A\\nB\\nC\")throw new Exception(\"dropdown\");dropdown.Call(\"lv_dropdown_open\");dropdown.Call(\"lv_dropdown_close\");dropdown.Dispose();var roller=new LvObject(\"Roller\");roller.Call(\"lv_roller_set_options\",\"A\\nB\",0);roller.Call(\"lv_roller_set_selected\",1,0);if(roller.Call(\"lv_roller_get_selected\")!=1)throw new Exception(\"roller\");roller.Dispose();");
#endif
#if LV_USE_TEXTAREA
    run(vm,"var ta=new LvObject(\"TextArea\");ta.Call(\"lv_textarea_set_text\",\"abc\");ta.Call(\"lv_textarea_set_cursor_pos\",3);ta.Call(\"lv_textarea_add_text\",\"def\");if(ta.Call(\"lv_textarea_get_text\")!=\"abcdef\")throw new Exception(\"textarea\");ta.Call(\"lv_textarea_del_char\");ta.Call(\"lv_textarea_set_placeholder_text\",\"hint\");ta.Dispose();");
#endif
#if LV_USE_TEXTAREA
#if LV_TXT_ENC == LV_TXT_ENC_UTF8
    run(vm,"var unicode=new LvObject(\"TextArea\");unicode.Text=\"\";unicode.Call(\"lv_textarea_add_char\",233);unicode.Call(\"lv_textarea_add_char\",1587);unicode.Call(\"lv_textarea_add_char\",128578);if(unicode.Text!=\"éس🙂\")throw new Exception(\"Unicode scalar encoding\");bool surrogate=false;try{unicode.Call(\"lv_textarea_add_char\",0xd800);}catch(ArgumentException e){surrogate=true;}if(!surrogate)throw new Exception(\"Unicode surrogate\");bool nul=false;try{unicode.Call(\"lv_textarea_add_char\",0);}catch(ArgumentException e){nul=true;}if(!nul)throw new Exception(\"Unicode NUL\");unicode.Dispose();");
#endif
#endif
#if LV_USE_TABLE
    run(vm,"var table=new LvObject(\"Table\");table.Call(\"lv_table_set_row_cnt\",2);table.Call(\"lv_table_set_col_cnt\",2);table.Call(\"lv_table_set_cell_value\",1,1,\"cell\");string cell=table.Call(\"lv_table_get_cell_value\",1,1);GC.Collect();if(cell!=\"cell\")throw new Exception(\"table copy\");bool cap=false;try{table.Call(\"lv_table_set_row_cnt\",1023);}catch(ArgumentException e){cap=true;}if(!cap)throw new Exception(\"table cap\");bool index=false;try{table.Call(\"lv_table_get_cell_value\",100,100);}catch(ArgumentException e){index=true;}if(!index)throw new Exception(\"table index\");table.Dispose();");
#endif
#if LV_USE_LED && LV_USE_SPINBOX
    run(vm,"var led=new LvObject(\"Led\");led.Call(\"lv_led_set_color\",0xff0000);led.Call(\"lv_led_set_brightness\",80);if(led.Call(\"lv_led_get_brightness\")!=80)throw new Exception(\"led\");led.Call(\"lv_led_on\");led.Call(\"lv_led_toggle\");led.Dispose();var spin=new LvObject(\"Spinbox\");spin.Call(\"lv_spinbox_set_range\",-100,100);spin.Call(\"lv_spinbox_set_value\",12);spin.Call(\"lv_spinbox_set_step\",1);spin.Call(\"lv_spinbox_increment\");if(spin.Call(\"lv_spinbox_get_value\")!=13)throw new Exception(\"spin\");bool digits=false;try{spin.Call(\"lv_spinbox_set_range\",-999999999,999999999);}catch(ArgumentException e){digits=true;}if(!digits)throw new Exception(\"spin digit guard\");spin.Call(\"lv_spinbox_set_digit_format\",9,0);spin.Call(\"lv_spinbox_set_range\",-999999999,999999999);spin.Call(\"lv_spinbox_set_step\",100000000);spin.Call(\"lv_spinbox_step_prev\");spin.Call(\"lv_spinbox_set_value\",999999998);spin.Call(\"lv_spinbox_increment\");if(spin.Call(\"lv_spinbox_get_value\")!=999999999)throw new Exception(\"spin safe boundary\");bool step=false;try{spin.Call(\"lv_spinbox_set_step\",2147483647);}catch(ArgumentException e){step=true;}if(!step)throw new Exception(\"spin overflow step\");bool shrink=false;try{spin.Call(\"lv_spinbox_set_digit_format\",2,0);}catch(ArgumentException e){shrink=true;}if(!shrink)throw new Exception(\"spin format shrink\");spin.Dispose();");
#endif
#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
    run(vm,"var target=new LvObject(\"TextArea\");var keyboard=new LvObject(\"Keyboard\");keyboard.Call(\"lv_keyboard_set_textarea\",target);target.Dispose();keyboard.Call(\"lv_keyboard_set_mode\",0);keyboard.Call(\"lv_keyboard_set_textarea\",null);keyboard.Dispose();");
#endif
#if MCS_LV_USE_IMAGE
    run(vm,"var image=new LvObject(\"Image\");bool empty=false;try{image.Call(\"lv_img_set_offset_x\",-2);}catch(ArgumentException e){empty=true;}if(!empty)throw new Exception(\"empty image offset\");image.Call(\"lv_img_set_angle\",900);image.Call(\"lv_image_set_scale\",512);image.Call(\"lv_img_set_pivot\",0,0);image.Call(\"lv_image_set_antialias\",true);if(image.Call(\"lv_img_get_offset_x\")!=0 || image.Call(\"lv_image_get_offset_y\")!=0 || image.Call(\"lv_image_get_rotation\")!=900 || image.Call(\"lv_img_get_zoom\")!=512 || !image.Call(\"lv_img_get_antialias\"))throw new Exception(\"image transforms\");bool zero=false;try{image.Call(\"lv_img_set_zoom\",0);}catch(ArgumentException e){zero=true;}if(!zero)throw new Exception(\"zero image scale\");image.Dispose();");
#endif
#if LV_USE_CALENDAR
    run(vm,"var calendar=new LvObject(\"Calendar\");calendar.Call(\"lv_calendar_set_today_date\",2026,10,27);calendar.Call(\"lv_calendar_set_showed_date\",2026,10);calendar.Call(\"lv_calendar_set_month_shown\",2026,11);calendar.Dispose();");
#endif
    mcs_free(vm);test_text_failures(cfg);test_dispatch_cost(cfg);
    cfg->lock(cfg->ctx);CHECK(lv_obj_get_child_count(cfg->root)==0);cfg->unlock(cfg->ctx);
    printf("LVGL native API: %d checks, %d failures; explicit operations=%zu constants=%zu\n",checks,failures,sizeof native_cases/sizeof native_cases[0],sizeof constant_cases/sizeof constant_cases[0]);return failures;
}
