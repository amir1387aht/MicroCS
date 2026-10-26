/* A synthetic 64 MiB file proves the byte-range API never reads/allocates it all.
 * Also verifies explicit close, GC/VM teardown, backend faults and VFS locking. */
#include "mcs_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_stream_smoke.h"
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n", __LINE__,#c); } } while (0)
typedef struct { uint32_t pos; size_t bytes, largest; int handles, locked, calls, fail_read; } fake_t;
static int fo(void* ctx,const char* p,int flags,void** out) { (void)p;(void)flags;fake_t* f=ctx;CHECK(f->locked==1); f->pos=0;f->handles++;*out=f;return 0; }
static int fr(void* ctx,void* h,void* buf,size_t n) { (void)h;fake_t* f=ctx;CHECK(f->locked==1);f->calls++;if(f->fail_read)return MCS_VFS_EIO;if(n>f->largest)f->largest=n;if(n>64u*1024*1024-f->pos)n=64u*1024*1024-f->pos;for(size_t i=0;i<n;i++)((uint8_t*)buf)[i]=(uint8_t)(f->pos+i);f->pos+=(uint32_t)n;f->bytes+=n;return (int)n; }
static int fc(void* ctx,void* h) { (void)h;fake_t* f=ctx;CHECK(f->locked==1);f->handles--;return 0; }
static int fst(void* ctx,const char* p,mcs_vfs_stat_t* st) { (void)p;fake_t* f=ctx;CHECK(f->locked==1);st->size=64u*1024*1024;st->is_dir=false;return 0; }
static int fsk(void* ctx,void* h,int32_t off,int origin,int32_t* out) { (void)h;fake_t* f=ctx;CHECK(f->locked==1);int64_t p=(origin==0?0:origin==1?(int64_t)f->pos:64*1024*1024)+off;if(p<0||p>0x7fffffff)return MCS_VFS_EINVAL;f->pos=(uint32_t)p;*out=(int32_t)p;return 0; }
static const mcs_vfs_ops_t ops={ .open=fo,.read=fr,.close=fc,.stat=fst,.seek=fsk };
static void enter(void* ctx){fake_t* f=ctx;CHECK(f->locked==0);f->locked++;}
static void leave(void* ctx){fake_t* f=ctx;CHECK(f->locked==1);f->locked--;}
static void print(void* ud,const char* p,size_t n){(void)ud;fwrite(p,1,n,stderr);}
static void test_large(void) {
    uint8_t heap[64*1024]; mcs_pool_t pool;mcs_pool_init(&pool,heap,sizeof heap);
    fake_t f={0};mcs_vfs_t v;mcs_vfs_init(&v);mcs_vfs_lock_t lock={enter,leave,&f};v.lock=&lock;
    CHECK(!mcs_vfs_mount(&v,"/",&ops,&f,0));
    mcs_config_t cfg;mcs_config_default(&cfg);cfg.realloc_fn=mcs_pool_realloc;cfg.alloc_ud=&pool;cfg.heap_limit=sizeof heap-2048;cfg.write_fn=print;cfg.error_fn=print;
    mcs_vm_t* vm=mcs_new(&cfg);CHECK(vm!=NULL);if(!vm)return;mcs_fs_open_lib(vm,&v);
    CHECK(mcs_exec_source(vm,"ranges.cs", "var f=File.OpenRead(\"/huge\"); byte[] b=new byte[50]; if(f.Read(b,0,50)!=50 || b[49]!=49) throw new Exception(\"first\"); if(f.Read(b,0,50)!=50 || b[0]!=50) throw new Exception(\"second\"); f.Seek(1000000,SeekOrigin.Begin); if(f.ReadByte()!=64) throw new Exception(\"seek\"); f.Close();") == MCS_OK);
    CHECK(f.handles==0 && f.bytes==101 && f.largest<=50);
    CHECK(mcs_exec_source(vm,"fault.cs","var f=File.OpenRead(\"/huge\");") == MCS_OK);CHECK(f.handles==1);f.fail_read=1;
    CHECK(mcs_exec_source(vm,"fault2.cs","bool caught=false;try { f.ReadByte(); } catch(IOException e) { caught=true; } if(!caught)throw new Exception(\"fault\");f.Close();") == MCS_OK);
    CHECK(f.handles==0);f.fail_read=0;
    CHECK(mcs_exec_source(vm,"unclosed.cs","var leftover=File.OpenRead(\"/huge\");") == MCS_OK);CHECK(f.handles==1);
    mcs_free(vm);CHECK(f.handles==0 && f.locked==0 && pool.used==0);
    /* Reordering/reusing mount slots cannot change an open handle's dispatch. */
    fake_t second={0};mcs_vfs_lock_t second_lock={enter,leave,&second};
    CHECK(!mcs_vfs_mount(&v,"/other",&ops,&second,0));
    v.lock=&second_lock; mcs_vfs_file_t held;
    CHECK(!mcs_vfs_open(&v,"/other/file",MCS_VFS_READ,&held));
    CHECK(!mcs_vfs_umount(&v,"/")); CHECK(!mcs_vfs_mount(&v,"/replacement",&ops,&f,0));
    CHECK(!mcs_vfs_close(&held)); CHECK(second.handles==0 && f.handles==0);

}
static void test_ram(void) {
    mcs_ramfs_t fs; mcs_ramfs_init(&fs, 8192, NULL, NULL);
    mcs_vfs_t v; mcs_vfs_init(&v); CHECK(!mcs_vfs_mount(&v,"/",&mcs_ramfs_ops,&fs,0));
    CHECK(file_stream_smoke(&v,"",true)==MCS_OK);
    CHECK(!mcs_vfs_write_file(&v,"/busy","a",1,false));
    mcs_vfs_file_t f; CHECK(!mcs_vfs_open(&v,"/busy",MCS_VFS_READ,&f));
    CHECK(mcs_vfs_remove(&v,"/busy")==MCS_VFS_EACCES); CHECK(!mcs_vfs_close(&f));
    CHECK(!mcs_vfs_remove(&v,"/busy"));
    CHECK(!mcs_vfs_mount(&v,"/ro",&mcs_ramfs_ops,&fs,MCS_VFS_RDONLY));
    CHECK(mcs_vfs_open(&v,"/ro/no",MCS_VFS_UPDATE|MCS_VFS_CREATE,&f)==MCS_VFS_EACCES);
    mcs_ramfs_free(&fs);
}
int main(void){test_large();test_ram();printf("file streams: %d checks, %d failures\n",checks,failures);return failures!=0;}
