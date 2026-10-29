/* Generic upload faults: preserve errno, drain payload and accept next command. */
#include "mcs.h"
#include "mcs_shell.h"
#include "mcs_vfs.h"
#include <stdio.h>
#include <string.h>
#include "../../modules/shell/mcs_shell.c"
static int checks,failures,fault,short_writes;
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);}}while(0)
static const uint8_t payload[]={1,2,3,4,5,6,7,8,9};
static size_t input_pos,output_len;
static char output[2048];
static int receive(void* ud,uint8_t* dst,size_t n,uint32_t ms){
 (void)ud;(void)ms;if(n>sizeof payload-input_pos)n=sizeof payload-input_pos;
 memcpy(dst,payload+input_pos,n);input_pos+=n;return (int)n;
}
static void transmit(void* ud,const char* data,size_t n){(void)ud;if(output_len+n<sizeof output){memcpy(output+output_len,data,n);output_len+=n;output[output_len]=0;}}
static int write_fault(void* ctx,void* handle,const void* data,size_t n){
 if(fault)return fault;
 if(short_writes && n>2)n=2;
 return mcs_ramfs_ops.write(ctx,handle,data,n);
}
static void scenario(int error,int partial){
 mcs_ramfs_t ram; mcs_vfs_t vfs;mcs_ramfs_init(&ram,8192,NULL,NULL);
 mcs_vfs_init(&vfs);mcs_vfs_ops_t ops=mcs_ramfs_ops;ops.write=write_fault;
 CHECK(!mcs_vfs_mount(&vfs,"/",&ops,&ram,0));
 fault=0;short_writes=0;CHECK(!mcs_vfs_write_file(&vfs,"/generic.cs","old",3,false));
 mcs_shell_t sh;memset(&sh,0,sizeof sh);sh.vfs=&vfs;sh.t=(mcs_transport_t){receive,transmit,NULL};
 input_pos=output_len=0;output[0]=0;fault=error;short_writes=partial;
 cmd_put(&sh,"/generic.cs","9","16");
 CHECK(input_pos==sizeof payload);
 CHECK(strstr(output,error?mcs_vfs_strerror(error):"\004OK")!=NULL);
 fault=0;short_writes=0;
 char* data=NULL;size_t len=0;CHECK(!mcs_vfs_read_file(&vfs,"/generic.cs",&data,&len));
 CHECK(len==(error?3:9));CHECK(!memcmp(data,error?(const void*)"old":payload,len));mcs_vfs_free(&vfs,data,len);
 mcs_vfs_stat_t st;CHECK(mcs_vfs_stat(&vfs,"/generic.cs.part",&st)==MCS_VFS_ENOENT);
 input_pos=output_len=0;output[0]=0;cmd_put(&sh,"/retry.cs","9",NULL);
 CHECK(strstr(output,"\004OK")!=NULL);CHECK(input_pos==sizeof payload);
 CHECK(!mcs_vfs_umount(&vfs,"/"));mcs_ramfs_free(&ram);
}
int main(void){scenario(MCS_VFS_ENOMEM,0);scenario(MCS_VFS_EIO,0);scenario(0,1);
 printf("%s shell upload faults: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures!=0;}
