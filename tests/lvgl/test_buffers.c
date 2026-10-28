#include "mcs_lvgl.h"
#include "mcs_lvgl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"buffer FAIL %d: %s\n",__LINE__,#x);}}while(0)
static void quiet(void* p,const char* s,size_t n){(void)p;(void)s;(void)n;}
static int run(mcs_vm_t* vm,const char* code){int rc=mcs_exec_source(vm,"buffers.cs",code);if(rc)fprintf(stderr,"%s\nsource: %.300s\n",mcs_last_error(vm),code);CHECK(rc==MCS_OK);return rc;}
typedef struct { mcs_lvgl_config_t base;unsigned live,peak;bool fail,owner;void* ptrs[128];size_t sizes[128];size_t last_size; } host_t;
static void lock(void* p){host_t* h=p;h->base.lock(h->base.ctx);}
static void unlock(void* p){host_t* h=p;h->base.unlock(h->base.ctx);}
static bool owner(void* p){host_t* h=p;return h->owner && h->base.is_vm_thread(h->base.ctx);}
static void* alloc(void* p,size_t n){host_t* h=p;h->last_size=n;if(h->fail)return NULL;void* ptr=h->base.alloc(h->base.ctx,n);if(ptr){unsigned i;for(i=0;i<128 && h->ptrs[i];++i){}if(i==128)abort();h->ptrs[i]=ptr;h->sizes[i]=n;if(++h->live>h->peak)h->peak=h->live;}return ptr;}
static void release(void* p,void* ptr){host_t* h=p;unsigned i;for(i=0;i<128 && h->ptrs[i]!=ptr;++i){}if(i==128 || !h->live)abort();memset(ptr,0xa5,h->sizes[i]);h->ptrs[i]=NULL;--h->live;h->base.free(h->base.ctx,ptr);}
static mcs_lvgl_config_t config(host_t* h,const mcs_lvgl_config_t* base){memset(h,0,sizeof(*h));h->base=*base;h->owner=true;mcs_lvgl_config_t c=*base;c.ctx=h;c.lock=lock;c.unlock=unlock;c.is_vm_thread=owner;c.alloc=alloc;c.free=release;return c;}
static mcs_vm_t* new_vm(const mcs_lvgl_config_t* cfg){mcs_config_t c;mcs_config_default(&c);c.write_fn=c.error_fn=quiet;mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(vm)CHECK(mcs_lvgl_attach(vm,cfg)==0);return vm;}
#if LV_USE_GRID || LV_USE_LINE || LV_USE_TEXTAREA || LV_USE_DROPDOWN || MCS_LV_USE_BUTTONMATRIX
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
#if LV_USE_TEXTAREA
          "var ta=new LvObject(\"TextArea\",panel);ta.Call(\"lv_textarea_set_accepted_chars\",\"0123\");ta.Call(\"lv_textarea_set_accepted_chars\",null);ta.Call(\"lv_textarea_set_accepted_chars\",\"abc\");"
#endif
#if LV_USE_DROPDOWN
          "var dd=new LvObject(\"Dropdown\",panel);dd.Call(\"lv_dropdown_set_text\",\"Title\");dd.Call(\"lv_dropdown_set_symbol\",\"▼\");dd.Call(\"lv_dropdown_set_text\",null);dd.Call(\"lv_dropdown_set_text\",\"New\");string snap=dd.Call(\"lv_dropdown_get_selected_str\");string copy=dd.Call(\"lv_dropdown_get_text\");"
#endif
#if MCS_LV_USE_BUTTONMATRIX
          "var buttons=new LvObject(\"ButtonMatrix\",panel);buttons.SetButtonMap(new string[]{\"A\",\"B\"});buttons.SetButtonControls(new int[]{1,2});buttons.SetButtonMap(new string[]{\"C\"});"
#endif
          "panel.Dispose();Lvgl.Dispatch(0);";
        int rc=mcs_exec_source(vm,"buffer-fault.cs",code);f.inject=false;
        if(!f.injected){CHECK(rc==MCS_OK);done=true;}
        mcs_free(vm);CHECK(!h.live && !f.pool.used);CHECK(mcs_lv_child_count(base->root)==0);
    }
    CHECK(done);
}
#endif
static void test_retained_text(const mcs_lvgl_config_t* base) {
    host_t h;mcs_lvgl_config_t cfg=config(&h,base);mcs_vm_t* vm=new_vm(&cfg);if(!vm)return;
#if LV_USE_TEXTAREA
    run(vm,"var ta=new LvObject(\"TextArea\");string chars=\"0\"+\"123\";ta.Call(\"lv_textarea_set_accepted_chars\",chars);chars=null;GC.Collect();ta.Text=\"a102b\";if(ta.Text!=\"102\" || ta.Call(\"lv_textarea_get_accepted_chars\")!=\"0123\")throw new Exception(\"filter copy\");");CHECK(h.live==2);
    h.fail=true;run(vm,"bool oom=false;try{ta.Call(\"lv_textarea_set_accepted_chars\",\"bad\");}catch(OutOfMemoryException e){oom=true;}if(!oom)throw new Exception(\"filter OOM\");");h.fail=false;CHECK(h.live==2);
    run(vm,"if(ta.Call(\"lv_textarea_get_accepted_chars\")!=\"0123\")throw new Exception(\"filter preserved\");for(int n=0;n<40;n++)ta.Call(\"lv_textarea_set_accepted_chars\",\"123\");ta.Call(\"lv_textarea_set_accepted_chars\",\"\");ta.Text=\"123\";if(ta.Text!=\"123\" || ta.Call(\"lv_textarea_get_accepted_chars\")!=null)throw new Exception(\"empty filter reset\");ta.Call(\"lv_textarea_set_accepted_chars\",null);ta.Text=\"abc\";if(ta.Text!=\"abc\" || ta.Call(\"lv_textarea_get_accepted_chars\")!=null)throw new Exception(\"clear filter\");ta.Call(\"lv_textarea_set_align\",3);if(ta.Call(\"lv_obj_get_style_text_align\",0)!=3)throw new Exception(\"text alignment\");ta.Call(\"lv_textarea_set_accepted_chars\",\"abc\");ta.Dispose();Lvgl.Dispatch(0);");CHECK(h.live==1);
#endif
#if LV_USE_DROPDOWN
    run(vm,"var dd=new LvObject(\"Dropdown\");dd.Call(\"lv_dropdown_set_text\",\"Dynamic \"+\"title\");dd.Call(\"lv_dropdown_set_symbol\",\"▼\");GC.Collect();if(dd.Call(\"lv_dropdown_get_text\")!=\"Dynamic title\" || dd.Call(\"lv_dropdown_get_symbol\")!=\"▼\")throw new Exception(\"dropdown owned text\");");CHECK(h.live==3);
    h.fail=true;run(vm,"bool oom=false;try{dd.Call(\"lv_dropdown_set_text\",\"bad\");}catch(OutOfMemoryException e){oom=true;}if(!oom)throw new Exception(\"dropdown OOM\");");h.fail=false;CHECK(h.live==3);
    h.fail=true;run(vm,"bool oom=false;try{dd.Call(\"lv_dropdown_get_selected_str\");}catch(OutOfMemoryException e){oom=true;}if(!oom)throw new Exception(\"selected snapshot OOM\");");h.fail=false;CHECK(h.live==3);
    run(vm,"dd.Call(\"lv_dropdown_set_options\",\"One\\nTwo\");dd.Call(\"lv_dropdown_set_selected\",1);if(dd.Call(\"lv_dropdown_get_selected_str\")!=\"Two\")throw new Exception(\"small snapshot\");");CHECK(h.last_size==8); /* not the default 4098-byte maximum */
    /* Deliberately supply a host-native overlong option to exercise getter
     * rejection independent of the script setter's own input length bound. */
    char* long_option=malloc(MCS_LVGL_MAX_TEXT_BYTES+2u);CHECK(long_option!=NULL);
    if(long_option) {
        memset(long_option,'x',MCS_LVGL_MAX_TEXT_BYTES+1u);long_option[MCS_LVGL_MAX_TEXT_BYTES+1u]=0;
        lock(&h);lv_obj_t* dd=lv_obj_get_child(lv_obj_get_child(base->root,0),-1);lv_dropdown_set_options(dd,long_option);unlock(&h);
        run(vm,"bool longText=false;try{dd.Call(\"lv_dropdown_get_selected_str\");}catch(ArgumentException e){longText=true;}if(!longText)throw new Exception(\"selected truncation\");");CHECK(h.live==3);
        long_option[MCS_LVGL_MAX_TEXT_BYTES]=0;lock(&h);lv_dropdown_set_options(dd,long_option);unlock(&h);
        char code[256];snprintf(code,sizeof code,"if(dd.Call(\"lv_dropdown_get_selected_str\").Length!=%u)throw new Exception(\"selected boundary\");",(unsigned)MCS_LVGL_MAX_TEXT_BYTES);run(vm,code);free(long_option);CHECK(h.live==3);
    }
    run(vm,"if(dd.Call(\"lv_dropdown_get_text\")!=\"Dynamic title\")throw new Exception(\"dropdown preserved\");bool image=false;try{dd.Call(\"lv_dropdown_set_symbol\",\"S:/image.bin\");}catch(ArgumentException e){image=true;}if(!image)throw new Exception(\"symbol image pointer\");for(int n=0;n<40;n++){dd.Call(\"lv_dropdown_set_text\",\"New title\");dd.Call(\"lv_dropdown_set_symbol\",\"▲\");}dd.Call(\"lv_dropdown_set_options\",\"Red\\nGreen\\nBlue\");dd.Call(\"lv_dropdown_set_selected\",1);string selected=dd.Call(\"lv_dropdown_get_selected_str\");dd.Call(\"lv_dropdown_set_text\",null);dd.Call(\"lv_dropdown_set_symbol\",null);if(dd.Call(\"lv_dropdown_get_text\")!=null || dd.Call(\"lv_dropdown_get_symbol\")!=null)throw new Exception(\"reset dropdown\");for(int empty=0;empty<3;empty++){dd.Call(\"lv_dropdown_clear_options\");string emptyOptions=dd.Call(\"lv_dropdown_get_options\");if(dd.Call(\"lv_dropdown_get_option_cnt\")!=0 || (emptyOptions!=null && emptyOptions!=\"\") || dd.Call(\"lv_dropdown_get_selected_str\")!=\"\")throw new Exception(\"empty selection\");}dd.Dispose();GC.Collect();if(selected!=\"Green\")throw new Exception(\"selection copy\");Lvgl.Dispatch(0);");CHECK(h.live==1);
#endif
#if LV_USE_ROLLER
    run(vm,"var roller=new LvObject(\"Roller\");roller.Call(\"lv_roller_set_options\",\"Red\\nGreen\\nBlue\",0);roller.Call(\"lv_roller_set_selected\",2,0);if(roller.Call(\"lv_roller_get_selected_str\")!=\"Blue\")throw new Exception(\"roller selected string\");roller.Dispose();");CHECK(h.live==1);
#endif
#if LV_USE_TEXTAREA || LV_USE_DROPDOWN
    run(vm,"var doomed=new LvObject(\"Object\");"
#if LV_USE_TEXTAREA
           "var retired=new LvObject(\"TextArea\",doomed);retired.Call(\"lv_textarea_set_accepted_chars\",\"abc\");"
#endif
#if LV_USE_DROPDOWN
           "var retiredDropdown=new LvObject(\"Dropdown\",doomed);retiredDropdown.Call(\"lv_dropdown_set_text\",\"retained\");"
#endif
           );CHECK(h.live>1);
    lock(&h);mcs_lv_obj_delete(lv_obj_get_child(lv_obj_get_child(base->root,0),-1));unlock(&h);
    run(vm,"Lvgl.Dispatch(0);if(doomed.IsAlive)throw new Exception(\"retained native deletion\");");CHECK(h.live==1);
#if LV_USE_TEXTAREA
    run(vm,"bool stale=false;try{retired.Call(\"lv_textarea_set_accepted_chars\",\"bad\");}catch(InvalidOperationException e){stale=true;}if(!stale)throw new Exception(\"retired filter\");");CHECK(h.live==1);
#endif
#endif
    mcs_free(vm);CHECK(!h.live);CHECK(h.peak<=4);
}
static void test_button_maps(const mcs_lvgl_config_t* base) {
    host_t h;mcs_lvgl_config_t cfg=config(&h,base);mcs_vm_t* vm=new_vm(&cfg);if(!vm)return;
#if MCS_LV_USE_BUTTONMATRIX
    run(vm,"var matrix=new LvObject(\"ButtonMatrix\");string[] map={\"Red\",\"Gr\"+\"een\",\"\\n\",\"Blue\"};matrix.SetButtonMap(map);map[1]=\"changed\";map=null;GC.Collect();if(matrix.ButtonCount!=3 || matrix.Call(\"lv_btnmatrix_get_btn_text\",1)!=\"Green\")throw new Exception(\"map copy\");matrix.Call(\"lv_btnmatrix_set_selected_btn\",2);if(matrix.Call(\"lv_buttonmatrix_get_selected_button\")!=2)throw new Exception(\"selected alias\");");CHECK(h.live==2);
    lock(&h);lv_obj_t* native=lv_obj_get_child(lv_obj_get_child(base->root,0),0);const char* const* map=mcs_lv_buttonmatrix_map(native);
    CHECK((uintptr_t)map%sizeof(void*)==0);CHECK(!strcmp(map[1],"Green") && !strcmp(map[2],"\n") && !strcmp(map[4],"") && map[5]==NULL);
    lv_tick_inc(40);lv_timer_handler();unlock(&h);
    run(vm,"int checkedFlag=Lvgl.Constant(\"LV_BTNMATRIX_CTRL_CHECKED\");int checkable=Lvgl.Constant(\"LV_BTNMATRIX_CTRL_CHECKABLE\");int[] controls={1,checkable|checkedFlag|2,1};matrix.SetButtonControls(controls);controls[1]=0;controls=null;GC.Collect();if(!matrix.Call(\"lv_btnmatrix_has_btn_ctrl\",1,checkedFlag))throw new Exception(\"controls copy\");");CHECK(h.live==2);
    h.fail=true;run(vm,"bool mapOom=false;try{matrix.SetButtonMap(new string[]{\"replacement\"});}catch(OutOfMemoryException e){mapOom=true;}if(!mapOom || matrix.ButtonCount!=3 || matrix.Call(\"lv_btnmatrix_get_selected_btn\")!=2)throw new Exception(\"map OOM\");bool ctrlOom=false;try{matrix.SetButtonControls(new int[]{1,1,1});}catch(OutOfMemoryException e){ctrlOom=true;}if(!ctrlOom)throw new Exception(\"controls OOM\");");h.fail=false;CHECK(h.live==2);
    run(vm,"if(matrix.Call(\"lv_btnmatrix_get_btn_text\",1)!=\"Green\" || !matrix.Call(\"lv_btnmatrix_has_btn_ctrl\",1,checkedFlag))throw new Exception(\"OOM preserved\");");
    h.owner=false;run(vm,"bool owner=false;try{matrix.SetButtonMap(new string[]{\"bad\"});}catch(InvalidOperationException e){owner=true;}if(!owner)throw new Exception(\"map owner\");");h.owner=true;CHECK(h.live==2);
    run(vm,"int rejected=0;try{matrix.SetButtonMap(new string[0]);}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[]{\"\"});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[]{\"\\n\",\"A\"});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[]{\"A\",\"\\n\"});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[]{\"A\",\"\\n\",\"\\n\",\"B\"});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new object[]{1});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[]{null});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[]{\"a\\0b\"});}catch(ArgumentException e){rejected++;}try{matrix.SetButtonMap(new string[257]);}catch(ArgumentException e){rejected++;}if(rejected!=9 || matrix.ButtonCount!=3)throw new Exception(\"map rejection\");");CHECK(h.live==2);
    run(vm,"int badControls=0;try{matrix.SetButtonControls(new int[]{1,1});}catch(ArgumentException e){badControls++;}try{matrix.SetButtonControls(new int[]{1,-1,1});}catch(ArgumentException e){badControls++;}try{matrix.SetButtonControls(new object[]{1,\"bad\",1});}catch(ArgumentException e){badControls++;}try{matrix.SetButtonControls(new int[]{1,65536,1});}catch(ArgumentException e){badControls++;}try{matrix.SetButtonControls(new int[0]);}catch(ArgumentException e){badControls++;}if(badControls!=5)throw new Exception(\"controls rejection\");bool selected=false;try{matrix.Call(\"lv_btnmatrix_set_selected_btn\",3);}catch(ArgumentException e){selected=true;}if(!selected)throw new Exception(\"selected bounds\");matrix.Call(\"lv_btnmatrix_set_selected_btn\",-1);if(matrix.Call(\"lv_btnmatrix_get_selected_btn\")!=-1)throw new Exception(\"none selection\");matrix.Call(\"lv_buttonmatrix_set_selected_button\",Lvgl.Constant(\"LV_BUTTONMATRIX_BUTTON_NONE\"));");CHECK(h.live==2);
#if LV_USE_KEYBOARD
    run(vm,"var kb=new LvObject(\"Keyboard\");bool keyboard=false;try{kb.SetButtonMap(new string[]{\"unsafe\"});}catch(NotSupportedException e){keyboard=true;}if(!keyboard || kb.ButtonCount<1)throw new Exception(\"keyboard map\");kb.Dispose();");CHECK(h.live==2);
#endif
    {char code[1024];snprintf(code,sizeof code,
        "string edge=new string('x',%u);matrix.SetButtonMap(new string[]{edge});if(matrix.Call(\"lv_btnmatrix_get_btn_text\",0).Length!=%u)throw new Exception(\"map text boundary\");bool tooLong=false;try{matrix.SetButtonMap(new string[]{edge+\"x\"});}catch(ArgumentException e){tooLong=true;}if(!tooLong || matrix.ButtonCount!=1)throw new Exception(\"map byte cap\");edge=null;GC.Collect();string[] many=new string[256];for(int i=0;i<256;i++)many[i]=\"x\";matrix.SetButtonMap(many);if(matrix.ButtonCount!=256)throw new Exception(\"map entry boundary\");matrix.SetButtonControls(new int[256]);many=null;GC.Collect();matrix.SetButtonMap(new List<string>{\"A\",\"B\"});matrix.SetButtonControls(new List<int>{1,2});",
        (unsigned)MCS_LVGL_MAX_TEXT_BYTES-2u,(unsigned)MCS_LVGL_MAX_TEXT_BYTES-2u);run(vm,code);CHECK(h.live==2);}
    run(vm,"var objectOnly=new LvObject(\"Object\");bool wrong=false;try{objectOnly.SetButtonMap(new string[]{\"bad\"});}catch(NotSupportedException e){wrong=true;}if(!wrong)throw new Exception(\"map kind\");objectOnly.Dispose();for(int repeat=0;repeat<50;repeat++){matrix.SetButtonMap(new string[]{\"A\",\"B\"});matrix.SetButtonControls(new int[]{1,2});matrix.Call(\"lv_btnmatrix_set_selected_btn\",1);matrix.SetButtonMap(new string[]{\"Green\"});if(matrix.ButtonCount!=1 || matrix.Call(\"lv_btnmatrix_get_selected_btn\")!=-1)throw new Exception(\"map shrink\");}string saved=matrix.Call(\"lv_btnmatrix_get_btn_text\",0);matrix.Dispose();GC.Collect();if(saved!=\"Green\")throw new Exception(\"button text snapshot\");Lvgl.Dispatch(0);");CHECK(h.live==1);
    run(vm,"var parent=new LvObject(\"Object\");var doomed=new LvObject(\"ButtonMatrix\",parent);doomed.SetButtonMap(new string[]{\"native lifetime\"});");CHECK(h.live==2);
    lock(&h);mcs_lv_obj_delete(lv_obj_get_child(lv_obj_get_child(base->root,0),0));unlock(&h);
    run(vm,"Lvgl.Dispatch(0);bool stale=false;try{doomed.SetButtonMap(new string[]{\"bad\"});}catch(InvalidOperationException e){stale=true;}if(!stale)throw new Exception(\"stale map\");");CHECK(h.live==1);
    CHECK(h.peak<=3);
#else
    run(vm,"var probe=new LvObject(\"Object\");int disabled=0;try{probe.SetButtonMap(new string[]{\"bad\"});}catch(NotSupportedException e){disabled++;}try{probe.SetButtonControls(new int[]{1});}catch(NotSupportedException e){disabled++;}try{int n=probe.ButtonCount;}catch(NotSupportedException e){disabled++;}if(disabled!=3)throw new Exception(\"disabled maps\");probe.Dispose();");
#endif
    mcs_free(vm);CHECK(!h.live);
}
int test_lvgl_buffers(const mcs_lvgl_config_t* base){
    test_retained_text(base);test_button_maps(base);
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
#if LV_USE_GRID || LV_USE_LINE || LV_USE_TEXTAREA || LV_USE_DROPDOWN || MCS_LV_USE_BUTTONMATRIX
    test_faults(base);
#endif
    printf("LVGL copied buffers: %d checks, %d failures; peak wrapper blocks=%u\n",checks,failures,h.peak);return failures;
}
