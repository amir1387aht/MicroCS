#include "mcs_lvgl_fs.h"
#include "mcs_lvgl_compat.h"
#include "mcs_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if LVGL_VERSION_MAJOR == 8
#define lv_obj_get_child_count lv_obj_get_child_cnt
#endif
#if MCS_ENABLE_FS
static int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"asset FAIL %d: %s\n",__LINE__,#x);}}while(0)
/* Registered driver must outlive this test function and every later LVGL call. */
static mcs_lvgl_fs_t bridge;
static void quiet(void* ud,const char* s,size_t n){(void)ud;(void)s;(void)n;}
static void run(mcs_vm_t* vm,const char* code){int rc=mcs_exec_source(vm,"assets.cs",code);if(rc)fprintf(stderr,"%s\n",mcs_last_error(vm));CHECK(rc==MCS_OK);}
static unsigned opened(void){unsigned n=0;for(unsigned i=0;i<MCS_LVGL_FS_MAX_FILES;++i)n+=bridge.files[i].used;return n;}
static int vfs_locked;
static void vfs_enter(void* ud){(void)ud;if(vfs_locked)abort();vfs_locked=1;}
static void vfs_leave(void* ud){(void)ud;if(vfs_locked!=1)abort();vfs_locked=0;}
static void fixture_font(mcs_vfs_t* vfs){
    FILE* f=fopen(MCS_TEST_FONT_FILE,"rb");CHECK(f!=NULL);if(!f)return;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);CHECK(n>0 && n<65536);
    if(n>0 && n<65536){char* p=malloc((size_t)n);CHECK(p!=NULL);if(p){CHECK(fread(p,1,(size_t)n,f)==(size_t)n);CHECK(!mcs_vfs_write_file(vfs,"/assets/font.fnt",p,(size_t)n,false));CHECK(!mcs_vfs_write_file(vfs,"/assets/fonts/ui.fnt",p,(size_t)n,false));free(p);}}
    fclose(f);
}
static void fixture_image(mcs_vfs_t* vfs){
#if LVGL_VERSION_MAJOR == 8
    lv_img_header_t header={0};header.cf=LV_IMG_CF_TRUE_COLOR;header.w=2;header.h=2;
    uint16_t pixels[4]={0xf800,0x07e0,0x001f,0xffff};
#else
    lv_image_header_t header={0};header.magic=LV_IMAGE_HEADER_MAGIC;header.cf=LV_COLOR_FORMAT_ARGB8888;header.w=2;header.h=2;header.stride=8;
    uint32_t pixels[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
#endif
    CHECK(!mcs_vfs_write_file(vfs,"/assets/logo.bin",&header,sizeof header,false));
    CHECK(!mcs_vfs_write_file(vfs,"/assets/logo.bin",pixels,sizeof pixels,true));
    CHECK(!mcs_vfs_write_file(vfs,"/assets/images/logo.bin",&header,sizeof header,false));
    CHECK(!mcs_vfs_write_file(vfs,"/assets/images/logo.bin",pixels,sizeof pixels,true));
}
#if MCS_ENABLE_HAL && LV_USE_LABEL && MCS_LV_USE_BUTTON && MCS_LV_USE_IMAGE
static void example(const mcs_lvgl_config_t* cfg,const char* path,unsigned panels) {
    FILE* f=fopen(path,"rb");CHECK(f!=NULL);if(!f)return;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);CHECK(n>0 && n<16384);
    if(n<=0 || n>=16384){fclose(f);return;}
    char* code=malloc((size_t)n+1);CHECK(code!=NULL);if(!code){fclose(f);return;}
    CHECK(fread(code,1,(size_t)n,f)==(size_t)n);code[n]=0;fclose(f);
    mcs_config_t c;mcs_config_default(&c);c.write_fn=quiet;c.error_fn=quiet;
    mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);
    if(vm) {
        mcs_hal_t hal={0};mcs_hal_open_lib(vm,&hal);CHECK(!mcs_lvgl_attach(vm,cfg));run(vm,code);
        cfg->lock(cfg->ctx);
        lv_obj_t* container=lv_obj_get_child(cfg->root,0);CHECK(container!=NULL);
        if(container)CHECK(lv_obj_get_child_count(container)==panels);
        lv_tick_inc(40);lv_timer_handler();cfg->unlock(cfg->ctx);
        mcs_free(vm);CHECK(opened()==0);CHECK(lv_obj_get_child_count(cfg->root)==0);
    }
    free(code);
}
#endif
int test_lvgl_assets(const mcs_lvgl_config_t* base){
    mcs_vfs_t vfs;mcs_vfs_init(&vfs);mcs_ramfs_t fs;mcs_ramfs_init(&fs,65536,NULL,NULL);
    mcs_vfs_lock_t vl={vfs_enter,vfs_leave,NULL};vfs.lock=&vl;
    CHECK(!mcs_vfs_mount(&vfs,"/",&mcs_ramfs_ops,&fs,0));CHECK(!mcs_vfs_mkdir(&vfs,"/assets"));
    CHECK(!mcs_vfs_mkdir(&vfs,"/assets/fonts"));CHECK(!mcs_vfs_mkdir(&vfs,"/assets/images"));
    CHECK(!mcs_vfs_write_file(&vfs,"/assets/data","abcdef",6,false));
    CHECK(!mcs_vfs_write_file(&vfs,"/secret","outside",7,false));
    fixture_font(&vfs);fixture_image(&vfs);
    base->lock(base->ctx);
    CHECK(!mcs_lvgl_fs_register(&bridge,&vfs,'S',"/assets"));
    CHECK(mcs_lvgl_fs_register(&bridge,&vfs,'S',"/assets")==MCS_LVGL_ALREADY_ATTACHED);
    CHECK(lv_fs_is_ready('S'));
    lv_fs_file_t f={0};CHECK(lv_fs_open(&f,"S:/data",LV_FS_MODE_RD)==LV_FS_RES_OK);
    char bytes[8]={0};uint32_t n=0,pos=0;
    CHECK(lv_fs_read(&f,bytes,2,&n)==LV_FS_RES_OK && n==2 && !memcmp(bytes,"ab",2));
    CHECK(lv_fs_tell(&f,&pos)==LV_FS_RES_OK && pos==2);
    CHECK(lv_fs_seek(&f,4,LV_FS_SEEK_SET)==LV_FS_RES_OK);
    CHECK(lv_fs_read(&f,bytes,8,&n)==LV_FS_RES_OK && n==2 && !memcmp(bytes,"ef",2));
    CHECK(lv_fs_read(&f,bytes,1,&n)==LV_FS_RES_OK && n==0);
    CHECK(lv_fs_close(&f)==LV_FS_RES_OK);CHECK(opened()==0);
    CHECK(lv_fs_open(&f,"S:/data",LV_FS_MODE_WR)!=LV_FS_RES_OK);
    CHECK(lv_fs_open(&f,"S:/../secret",LV_FS_MODE_RD)!=LV_FS_RES_OK);
    lv_fs_file_t held[MCS_LVGL_FS_MAX_FILES]={0};
    for(unsigned i=0;i<MCS_LVGL_FS_MAX_FILES;++i)CHECK(lv_fs_open(&held[i],"S:/data",LV_FS_MODE_RD)==LV_FS_RES_OK);
    CHECK(lv_fs_open(&f,"S:/data",LV_FS_MODE_RD)!=LV_FS_RES_OK);
    for(unsigned i=0;i<MCS_LVGL_FS_MAX_FILES;++i)CHECK(lv_fs_close(&held[i])==LV_FS_RES_OK);
    base->unlock(base->ctx);
    static const mcs_lvgl_font_t fonts[]={{"TestNative",LV_FONT_DEFAULT}};
    mcs_lvgl_config_t cfg=*base;cfg.fonts=fonts;cfg.font_count=1;
    for(int i=0;i<20;++i){
        mcs_config_t c;mcs_config_default(&c);c.write_fn=quiet;c.error_fn=quiet;mcs_vm_t* vm=mcs_new(&c);CHECK(vm!=NULL);if(!vm)break;
        CHECK(mcs_lvgl_attach(vm,&cfg)==MCS_LVGL_OK);
#if LV_USE_FONT_COMPRESSED
        run(vm,"var obj=new LvObject(\"Object\");obj.SetFont(\"TestNative\");string fp=\"S:/font\"+\".fnt\";obj.SetFont(fp);obj.SetFont(\"Default\");obj.SetFont(fp);fp=null;GC.Collect();bool missing=false;try{obj.SetFont(\"S:/missing.fnt\");}catch(Exception e){missing=true;}if(!missing)throw new Exception(\"font missing\");");
#else
        run(vm,"var obj=new LvObject(\"Object\");obj.SetFont(\"TestNative\");bool rejected=false;try{obj.SetFont(\"S:/font.fnt\");}catch(NotSupportedException e){rejected=true;}if(!rejected)throw new Exception(\"compressed font accepted\");obj.SetFont(\"Default\");");
#endif
#if LV_USE_LABEL
        run(vm,"var label=new LvObject(\"Label\",obj);string text=\"hello \"+obj.IsAlive.ToString();label.Text=text;text=null;GC.Collect();");
#if LV_USE_FONT_COMPRESSED
        run(vm,"obj.SetFont(\"Default\");obj.SetFont(\"S:/font.fnt\");");
#endif
#endif
#if MCS_LV_USE_IMAGE
        run(vm,"var image=new LvObject(\"Image\",obj);string path=\"S:/logo\"+\".bin\";image.Source=path;image.Source=path;path=null;GC.Collect();bool bad=false;try{image.Source=\"S:/missing.bin\";}catch(Exception e){bad=true;}if(!bad)throw new Exception(\"image missing\");");
#endif
        base->lock(base->ctx);lv_tick_inc(40);lv_timer_handler();base->unlock(base->ctx);
        if(i%2)run(vm,"obj.Dispose();Lvgl.Dispatch(0);");
        mcs_free(vm);CHECK(opened()==0 && !vfs_locked);CHECK(lv_obj_get_child_count(base->root)==0);
    }
#if MCS_ENABLE_HAL && LV_USE_LABEL && MCS_LV_USE_BUTTON && MCS_LV_USE_IMAGE
    example(&cfg,MCS_LVGL_EXAMPLE_DIR "/button.cs",1);
    example(&cfg,MCS_LVGL_EXAMPLE_DIR "/assets.cs",LV_USE_FONT_COMPRESSED ? 1 : 0);
#endif
    base->lock(base->ctx);
    CHECK(lv_fs_open(&f,"S:/data",LV_FS_MODE_RD)==LV_FS_RES_OK);
    mcs_lvgl_fs_disable(&bridge);CHECK(opened()==0 && !lv_fs_is_ready('S'));
    CHECK(lv_fs_read(&f,bytes,1,&n)!=LV_FS_RES_OK);(void)lv_fs_close(&f);
    CHECK(lv_fs_open(&f,"S:/data",LV_FS_MODE_RD)!=LV_FS_RES_OK);
    CHECK(mcs_lvgl_fs_register(&bridge,&vfs,'S',"/assets")==MCS_LVGL_ALREADY_ATTACHED);
    base->unlock(base->ctx);
    mcs_ramfs_free(&fs);
    printf("LVGL assets/VFS: %d checks, %d failures; bounded bridge %zu native bytes\n",checks,failures,sizeof bridge);
    return failures;
}
#else
int test_lvgl_assets(const mcs_lvgl_config_t* base){(void)base;return 0;}
#endif
