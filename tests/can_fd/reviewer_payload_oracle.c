/* Independent ordinary-source/native loopback oracle, no physical CAN traffic. */
#include "mcs.h"
#include "mcs_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned count,failed;
#define T(x) do{count++;if(!(x)){failed++;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);}}while(0)
static void output(void*u,const char*s,size_t n){(void)u;fwrite(s,1,n,stderr);}
static int run(mcs_vm_t*v,const char*s){return mcs_exec_source(v,"review-can.cs",s);}
int main(void){
 mcs_hal_t h;mcs_hal_sim_t sim;mcs_hal_sim_init(&h,&sim);
 mcs_config_t c;mcs_config_default(&c);c.heap_limit=262144;c.stack_slots=512;c.write_fn=c.error_fn=output;
 mcs_vm_t*v=mcs_new(&c);T(v!=NULL);if(!v)return 1;mcs_hal_open_lib(v,&h);
 T(run(v,"CANFD.Open(0);")==MCS_OK);
 const unsigned wire[]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
 for(unsigned n=0;n<=64;n++)for(unsigned extended=0;extended<2;extended++)for(unsigned brs=0;brs<2;brs++){
  unsigned len=0;while(wire[len]<n)len++;
  char source[4096];size_t k=(size_t)snprintf(source,sizeof source,"var d = new int[%u];",n);
  for(unsigned i=0;i<n;i++)k+=(size_t)snprintf(source+k,sizeof source-k,"d[%u]=%u;",i,(n*17+i*29)&255);
  k+=(size_t)snprintf(source+k,sizeof source-k,"var f=new CanFdFrame(%u,d,%s,%s);CANFD.Send(0,f);",extended?0x1fffffff:0x7ff,extended?"true":"false",brs?"true":"false");
  if(n)snprintf(source+k,sizeof source-k,"d[0]=999;GC.Collect();");
  T(run(v,source)==MCS_OK);
  mcs_can_fd_frame_t got;memset(&got,0xa5,sizeof got);T(h.can_fd_recv(h.ctx,0,&got,0)==0);
  T(got.id==(extended?0x1fffffff:0x7ff));T(got.extended==extended);T(got.len==wire[len]);T(got.flags==(brs?MCS_CAN_FD_BRS:0));T(got.reserved==0);
  for(unsigned i=0;i<got.len&&i<64;i++)T(got.data[i]==(i<n?((n*17+i*29)&255):0));
 }
 T(run(v,"CANFD.Close(0);")==MCS_OK);T(!sim.can_fd_opened);
 /* Native FIFO copy/order/fullness, independent patterned payloads. */
 T(h.can_fd_open(h.ctx,0,500000,2000000)==0);
 for(unsigned j=0;j<8;j++){mcs_can_fd_frame_t f={0};f.id=j;f.len=64;for(unsigned i=0;i<64;i++)f.data[i]=(unsigned char)(j*31+i);T(h.can_fd_send(h.ctx,0,&f,0)==0);memset(&f,0xff,sizeof f);}
 mcs_can_fd_frame_t f={0};T(h.can_fd_send(h.ctx,0,&f,0)==MCS_HAL_EBUSY);
 for(unsigned j=0;j<8;j++){T(h.can_fd_recv(h.ctx,0,&f,0)==0);T(f.id==j);for(unsigned i=0;i<64;i++)T(f.data[i]==(unsigned char)(j*31+i));}
 T(h.can_fd_recv(h.ctx,0,&f,0)==MCS_HAL_ETIMEOUT);
 /* Unowned VM Close/free must not release embedding ownership. */
 T(run(v,"CANFD.Close(0);")==MCS_OK);T(sim.can_fd_opened);mcs_free(v);T(sim.can_fd_opened);T(h.can_fd_close(h.ctx,0)==0);
 v=mcs_new(&c);T(v!=NULL);mcs_hal_open_lib(v,&h);T(run(v,"CANFD.Open(0);")==MCS_OK);mcs_free(v);T(!sim.can_fd_opened);
 printf("%u/%u independent CAN payload/padding/flags/FIFO/ownership checks; software only\n",count-failed,count);return failed?1:0;
}
