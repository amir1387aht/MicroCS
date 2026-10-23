#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mcs_rp2_encoder.h"
fake_pio_t fake_pios[3];
static int loads,unloads,read_calls,fail_add;
bool pio_can_add_program_at_offset(PIO p,const struct pio_program* code,uint o){assert(o==0 && code->length==24);return !p->loaded;}
#if PICO_SDK_VERSION_MAJOR >= 2
int pio_add_program_at_offset(PIO p,const struct pio_program* code,uint o){if(fail_add)return -1;assert(!p->loaded && o==0 && code->origin==0);p->loaded=1;loads++;return 0;}
#else
void pio_add_program_at_offset(PIO p,const struct pio_program* code,uint o){assert(!p->loaded && o==0 && code->origin==0);p->loaded=1;loads++;}
#endif
int pio_claim_unused_sm(PIO p,bool must){assert(!must);for(int i=0;i<4;i++)if(!(p->claimed&(1u<<i))){p->claimed|=1u<<i;return i;}return -1;}
void pio_sm_unclaim(PIO p,uint s){assert(p->claimed&(1u<<s));p->claimed&=~(1u<<s);}
uint pio_get_gpio_base(PIO p){return p->base;}
void pio_sm_set_enabled(PIO p,uint s,bool on){p->enabled[s]=on;}
void pio_sm_set_consecutive_pindirs(PIO p,uint s,uint pin,uint count,bool out){(void)p;(void)s;assert(pin<32 && count==2 && !out);}
void pio_gpio_init(PIO p,uint pin){(void)p;assert(pin<32);}
pio_sm_config pio_get_default_sm_config(void){pio_sm_config c;memset(&c,0,sizeof c);return c;}
void sm_config_set_wrap(pio_sm_config* c,uint l,uint h){c->lo=l;c->hi=h;}
void sm_config_set_in_pins(pio_sm_config* c,uint pin){c->pin=pin;}
void sm_config_set_in_shift(pio_sm_config* c,bool right,bool automatic,uint threshold){(void)c;assert(!right && !automatic && threshold==32);}
void sm_config_set_fifo_join(pio_sm_config* c,int join){(void)c;assert(join==0);}
void sm_config_set_clkdiv(pio_sm_config* c,float divider){c->divider=divider;}
void pio_sm_init(PIO p,uint s,uint pc,const pio_sm_config* c){assert(pc==15 && c->lo==15 && c->hi==23 && c->divider==125.0f);p->count[s]=0;}
void pio_sm_exec(PIO p,uint s,uint instruction){assert(instruction==0xe040);p->count[s]=0;}
uint pio_encode_set(uint reg,uint value){assert(reg==pio_y && value==0);return 0xe040;}
uint pio_sm_get_rx_fifo_level(PIO p,uint s){assert(p->enabled[s]);return 4;}
uint32_t pio_sm_get_blocking(PIO p,uint s){assert(p->enabled[s]);read_calls++;return p->count[s];}
void pio_sm_clear_fifos(PIO p,uint s){(void)p;(void)s;}
void pio_remove_program(PIO p,const struct pio_program* code,uint o){assert(!p->claimed && code->length==24 && !o);p->loaded=0;unloads++;}
void gpio_pull_up(uint pin){assert(pin<32);}
void gpio_set_dir(uint pin,bool out){assert(pin<32 && !out);}
void gpio_set_function(uint pin,int function){assert(pin<32 && function==GPIO_FUNC_SIO);}
uint32_t clock_get_hz(int clock){assert(clock==clk_sys);return 125000000;}
int main(void){
 mcs_hal_t hal;memset(&hal,0,sizeof hal);mcs_rp2_encoder_init(&hal);assert(!strcmp(hal.encoder_name,"PIO"));
 void *a=NULL,*b=NULL;uint32_t n;
 assert(hal.encoder_open(NULL,2,4,&a)==MCS_HAL_ENOTSUP && !a);
 assert(hal.encoder_open(NULL,2,3,&a)==0);assert(hal.encoder_open(NULL,5,4,&b)==0);assert(loads==1);
 mcs_rp2_encoder_t* e=(mcs_rp2_encoder_t*)a;e->pio->count[e->sm]=80000;
 assert(hal.encoder_read(NULL,a,&n)==0 && n==80000 && read_calls==5);
 e=(mcs_rp2_encoder_t*)b;e->pio->count[e->sm]=7;assert(hal.encoder_read(NULL,b,&n)==0 && n==0u-7u);
 hal.encoder_close(NULL,a);assert(!unloads);hal.encoder_close(NULL,b);assert(unloads==1);
 for(int i=0;i<MCS_RP2_ENCODER_BANKS;i++)fake_pios[i].claimed=15;
 assert(hal.encoder_open(NULL,2,3,&a)==MCS_HAL_EBUSY && !a);
 memset(fake_pios,0,sizeof fake_pios);
#if PICO_SDK_VERSION_MAJOR >= 2
 fail_add=1;assert(hal.encoder_open(NULL,2,3,&a)==MCS_HAL_EBUSY && !a);for(int i=0;i<MCS_RP2_ENCODER_BANKS;i++)assert(!fake_pios[i].claimed);fail_add=0;
#else
 (void)fail_add;
#endif
 assert(hal.encoder_open(NULL,2,3,&a)==0);hal.encoder_close(NULL,a);
 puts("PASS PIO SDK: hardware SM/program ownership, sharing, fresh samples, reverse, busy and rollback");return 0;
}
