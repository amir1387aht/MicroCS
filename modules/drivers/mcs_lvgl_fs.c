#include "mcs_lvgl_fs.h"
#if MCS_ENABLE_LVGL && MCS_ENABLE_FS
#include <string.h>
#include <limits.h>
static lv_fs_res_t result(int rc) {
    switch(rc) {
    case 0: return LV_FS_RES_OK;
    case MCS_VFS_ENOENT: return LV_FS_RES_NOT_EX;
    case MCS_VFS_EACCES: return LV_FS_RES_DENIED;
    case MCS_VFS_ENOMEM: return LV_FS_RES_OUT_OF_MEM;
    case MCS_VFS_ENOSPC: return LV_FS_RES_FULL;
    case MCS_VFS_EINVAL: case MCS_VFS_ENAMETOOLONG: return LV_FS_RES_INV_PARAM;
    default: return LV_FS_RES_FS_ERR;
    }
}
static mcs_lvgl_fs_file_t* checked(mcs_lvgl_fs_t* b, void* p) {
    /* Pointer equality, not relational arithmetic on untrusted pointers. */
    for(unsigned i=0;i<MCS_LVGL_FS_MAX_FILES;++i)if(p==&b->files[i] && b->files[i].used)return &b->files[i];
    return NULL;
}
static bool ready(lv_fs_drv_t* drv) { return ((mcs_lvgl_fs_t*)drv->user_data)->active; }
static void* open_file(lv_fs_drv_t* drv,const char* path,lv_fs_mode_t mode) {
    mcs_lvgl_fs_t* b=drv->user_data;
    if(!b->active || mode!=LV_FS_MODE_RD || !path)return NULL;
    char relative[MCS_VFS_PATH_MAX],joined[MCS_VFS_PATH_MAX];
    if(mcs_vfs_normalize(path,relative,sizeof relative))return NULL;
    size_t root_len=strlen(b->root),n=strlen(relative);
    if(root_len==1)root_len=0; /* '/' + normalized absolute path */
    if(root_len+n>=sizeof joined)return NULL;
    memcpy(joined,b->root,root_len);memcpy(joined+root_len,relative,n+1);
    for(unsigned i=0;i<MCS_LVGL_FS_MAX_FILES;++i) {
        mcs_lvgl_fs_file_t* f=&b->files[i];if(f->used)continue;
        if(mcs_vfs_open(b->vfs,joined,MCS_VFS_READ,&f->file))return NULL;
        if(!f->file.ops->seek){mcs_vfs_close(&f->file);return NULL;}
        f->used=true;f->position=0;return f;
    }
    return NULL;
}
static lv_fs_res_t close_file(lv_fs_drv_t* drv,void* file) {
    mcs_lvgl_fs_t* b=drv->user_data;mcs_lvgl_fs_file_t* f=checked(b,file);
    if(!f)return LV_FS_RES_INV_PARAM;
    int rc=mcs_vfs_close(&f->file);f->used=false;return result(rc);
}
static lv_fs_res_t read_file(lv_fs_drv_t* drv,void* file,void* buffer,uint32_t count,uint32_t* read) {
    mcs_lvgl_fs_t* b=drv->user_data;*read=0;
    mcs_lvgl_fs_file_t* f=checked(b,file);
    if(!b->active || !f || count>INT32_MAX || count>INT32_MAX-f->position)return LV_FS_RES_INV_PARAM;
    int n=mcs_vfs_read(&f->file,buffer,count);
    if(n<0)return result(n);
    f->position+=(uint32_t)n;*read=(uint32_t)n;return LV_FS_RES_OK;
}
static lv_fs_res_t seek_file(lv_fs_drv_t* drv,void* file,uint32_t position,lv_fs_whence_t origin) {
    mcs_lvgl_fs_t* b=drv->user_data;mcs_lvgl_fs_file_t* f=checked(b,file);
    if(!b->active || !f || position>INT32_MAX)return LV_FS_RES_INV_PARAM;
    int whence=origin==LV_FS_SEEK_SET ? 0 : origin==LV_FS_SEEK_CUR ? 1 : origin==LV_FS_SEEK_END ? 2 : -1;
    if(whence<0)return LV_FS_RES_INV_PARAM;
    int32_t out=0;int rc=mcs_vfs_seek(&f->file,(int32_t)position,whence,&out);
    if(!rc)f->position=(uint32_t)out;
    return result(rc);
}
static lv_fs_res_t tell_file(lv_fs_drv_t* drv,void* file,uint32_t* position) {
    mcs_lvgl_fs_t* b=drv->user_data;mcs_lvgl_fs_file_t* f=checked(b,file);
    if(!b->active || !f)return LV_FS_RES_INV_PARAM;
    *position=f->position;return LV_FS_RES_OK;
}
int mcs_lvgl_fs_register(mcs_lvgl_fs_t* b,mcs_vfs_t* vfs,char letter,const char* root) {
    if(!b || !vfs || !root || root[0]!='/' || letter<'A' || letter>'Z')return MCS_LVGL_INVALID;
    if(b->registered)return MCS_LVGL_ALREADY_ATTACHED;
    if(lv_fs_get_drv(letter))return MCS_LVGL_ALREADY_ATTACHED;
    if(mcs_vfs_normalize(root,b->root,sizeof b->root))return MCS_LVGL_INVALID;
    lv_fs_drv_init(&b->driver);b->driver.letter=letter;b->driver.user_data=b;
    b->driver.ready_cb=ready;b->driver.open_cb=open_file;b->driver.close_cb=close_file;
    b->driver.read_cb=read_file;b->driver.seek_cb=seek_file;b->driver.tell_cb=tell_file;
    /* No filesystem cache, write API, directory snapshot, heap buffer or VM state. */
    b->vfs=vfs;b->active=true;lv_fs_drv_register(&b->driver);
    if(lv_fs_get_drv(letter)!=&b->driver){b->active=false;b->vfs=NULL;return MCS_LVGL_NO_MEMORY;}
    b->registered=true;return MCS_LVGL_OK;
}
void mcs_lvgl_fs_disable(mcs_lvgl_fs_t* b) {
    if(!b || !b->registered || !b->active)return;
    b->active=false;
    for(unsigned i=0;i<MCS_LVGL_FS_MAX_FILES;++i)if(b->files[i].used) {
        mcs_vfs_close(&b->files[i].file);b->files[i].used=false;
    }
    b->vfs=NULL;
}
#endif
