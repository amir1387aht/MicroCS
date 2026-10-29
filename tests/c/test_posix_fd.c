/* ESP-IDF descriptor path: no FILE allocation; native failures remain distinct. */
#include "mcs_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
static int failures, checks, injected, short_write, interrupted;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)
static ssize_t checked_write(int fd,const void* data,size_t n) {
    if (interrupted) { interrupted=0; errno=EINTR; return -1; }
    if (injected) { errno=injected; return -1; }
    if (short_write && n>2) n=2;
    return write(fd,data,n);
}
static ssize_t checked_read(int fd,void* data,size_t n) {
    if (injected) { errno=injected; return -1; }
    return read(fd,data,n);
}
#define ESP_PLATFORM 1
/* Any accidental reintroduction of fdopen must fail to compile. */
#define fdopen(...) forbidden_stdio_allocation
#define write checked_write
#define read checked_read
#include "../../modules/fs/mcs_vfs_posix.c"
#undef write
#undef read
#undef fdopen
int main(void) {
    char root[]="/tmp/mcs-fd-XXXXXX"; CHECK(mkdtemp(root)!=NULL);
    mcs_posixfs_t fs; CHECK(!mcs_posixfs_init(&fs,root)); void* h=NULL;
    CHECK(!p_open(&fs,"/data",MCS_VFS_WRITE,&h)); CHECK(h!=NULL);
    interrupted=1; CHECK(p_write(&fs,h,"abcdef",6)==6);
    injected=ENOMEM; CHECK(p_write(&fs,h,"x",1)==MCS_VFS_ENOMEM);
    injected=EIO; CHECK(p_read(&fs,h,root,1)==MCS_VFS_EIO);
    injected=0; short_write=1; CHECK(p_write(&fs,h,"ghij",4)==2); short_write=0;
#if MCS_ENABLE_FILE_STREAMS
    CHECK(!p_flush(&fs,h));
#endif
    CHECK(!p_close(&fs,h));
    CHECK(!p_open(&fs,"/data",MCS_VFS_READ,&h));
    char buf[16]={0}; CHECK(p_read(&fs,h,buf,sizeof buf)==8); CHECK(!memcmp(buf,"abcdefgh",8));
#if MCS_ENABLE_FILE_STREAMS
    int32_t pos=-1; CHECK(!p_seek(&fs,h,2,0,&pos) && pos==2);
    CHECK(p_read(&fs,h,buf,2)==2 && !memcmp(buf,"cd",2));
    CHECK(!p_seek(&fs,h,-1,2,&pos) && pos==7);
    CHECK(!p_seek(&fs,h,-1,1,&pos) && pos==6);
#endif
    CHECK(!p_close(&fs,h));
    /* Descriptor zero must survive opaque-handle encoding. */
    int saved=dup(0); close(0); CHECK(!p_open(&fs,"/data",MCS_VFS_READ,&h));
    CHECK(h!=NULL && p_fd(h)==0); CHECK(!p_close(&fs,h));
    if(saved>=0){ CHECK(dup2(saved,0)==0);close(saved); }
    CHECK(!p_remove(&fs,"/data")); CHECK(!rmdir(root));
    printf("%s ESP descriptor FS: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures!=0;
}
