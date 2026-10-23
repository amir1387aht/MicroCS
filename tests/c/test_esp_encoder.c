/* Public PCNT SDK double: hardware x4 actions, limit IRQs and exhaustive rollback.
 * Built as both C (ESP-IDF) and C++ (Arduino-ESP32). Not a silicon test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define SOC_PCNT_SUPPORTED 1
#define ESP_IDF_VERSION_MAJOR 5
#ifndef PCNT_TEST_MINOR
#define PCNT_TEST_MINOR 0
#endif
#define ESP_IDF_VERSION_MINOR PCNT_TEST_MINOR
#include "mcs_esp_encoder.h"
struct channel {pcnt_unit_handle_t unit;pcnt_chan_config_t cfg;int pos,neg,hi,lo;};
struct unit {int low,high,count,enabled,started,watches,accumulate;uint32_t accum;pcnt_event_callbacks_t cb;void* data;pcnt_channel_handle_t channels[2];};
static int units,channels,stage,fail_at,irq_count,clear_count;
static esp_err_t call(void) {return ++stage==fail_at ? ESP_FAIL : ESP_OK;}
#define TRY() do {esp_err_t r=call();if(r) return r;}while(0)
esp_err_t pcnt_new_unit(const pcnt_unit_config_t* c,pcnt_unit_handle_t* out) {
 TRY();*out=(pcnt_unit_handle_t)calloc(1,sizeof **out);assert(*out);units++;(*out)->low=c->low_limit;(*out)->high=c->high_limit;
#if PCNT_TEST_MINOR >= 1
 (*out)->accumulate=c->flags.accum_count;assert((*out)->accumulate);
#endif
 return 0;
}
esp_err_t pcnt_del_unit(pcnt_unit_handle_t u) {assert(!u->started && !u->enabled && !u->watches && !u->channels[0] && !u->channels[1]);free(u);units--;return 0;}
esp_err_t pcnt_new_channel(pcnt_unit_handle_t u,const pcnt_chan_config_t* c,pcnt_channel_handle_t* out) {
 TRY();*out=(pcnt_channel_handle_t)calloc(1,sizeof **out);assert(*out);(*out)->unit=u;(*out)->cfg=*c;int i=u->channels[0]?1:0;assert(!u->channels[i]);u->channels[i]=*out;channels++;return 0;
}
esp_err_t pcnt_del_channel(pcnt_channel_handle_t c) {assert(!c->unit->enabled);for(int i=0;i<2;i++)if(c->unit->channels[i]==c)c->unit->channels[i]=NULL;free(c);channels--;return 0;}
esp_err_t gpio_set_direction(gpio_num_t pin,int mode) {(void)pin;assert(mode==GPIO_MODE_INPUT);TRY();return 0;}
esp_err_t gpio_set_pull_mode(gpio_num_t pin,int pull) {(void)pin;assert(pull==GPIO_PULLUP_ONLY);TRY();return 0;}
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t c,int p,int n) {TRY();c->pos=p;c->neg=n;return 0;}
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t c,int h,int l) {TRY();c->hi=h;c->lo=l;return 0;}
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t u,const pcnt_glitch_filter_config_t* f) {(void)u;TRY();assert(f->max_glitch_ns==1000);return 0;}
esp_err_t pcnt_unit_register_event_callbacks(pcnt_unit_handle_t u,const pcnt_event_callbacks_t* c,void* d) {TRY();u->cb=*c;u->data=d;return 0;}
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t u,int p) {TRY();u->watches|=p<0?1:2;return 0;}
esp_err_t pcnt_unit_remove_watch_point(pcnt_unit_handle_t u,int p) {u->watches&=~(p<0?1:2);return 0;}
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t u) {TRY();u->enabled=1;return 0;}
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t u) {assert(!u->started);u->enabled=0;return 0;}
esp_err_t pcnt_unit_start(pcnt_unit_handle_t u) {TRY();assert(u->enabled);u->started=1;return 0;}
esp_err_t pcnt_unit_stop(pcnt_unit_handle_t u) {u->started=0;return 0;}
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t u) {TRY();clear_count++;u->count=0;return 0;}
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t u,int* n) {TRY();*n=(int32_t)(u->accum+(uint32_t)u->count);return 0;}
static unsigned phase=3;
static void edge(pcnt_unit_handle_t u,unsigned next) {
 for(int i=0;i<2;i++) {
  pcnt_channel_handle_t c=u->channels[i];unsigned mask=c->cfg.edge_gpio_num==2?2:1;
  if(!((phase^next)&mask))continue;
  int rising=!!(next&mask)^c->cfg.flags.invert_edge_input;
  int level=!!(next&(mask^3))^c->cfg.flags.invert_level_input;
  int d=rising?c->pos:c->neg;if((level?c->hi:c->lo)==PCNT_CHANNEL_LEVEL_ACTION_INVERSE)d=-d;
  assert(u->started);u->count+=d;
  if(u->count==u->low || u->count==u->high) {
   pcnt_watch_event_data_t event={u->count};u->count=0;irq_count++;if(u->accumulate)u->accum+=(uint32_t)event.watch_point_value;if(u->cb.on_reach)assert(!u->cb.on_reach(u,&event,u->data));
  }
 }
 phase=next;
}
int main(void) {
 mcs_hal_t hal;memset(&hal,0,sizeof hal);mcs_esp_encoder_init(&hal);assert(hal.encoder_open && !strcmp(hal.encoder_name,"PCNT"));
 int cases=0;
 /* Every SDK allocation/configuration failure must leave no unit/channel alive. */
 for(int f=1;f<40;f++) {
  stage=0;fail_at=f;void* h=NULL;int r=hal.encoder_open(NULL,2,3,&h);cases++;
  if(r==0) {assert(h);hal.encoder_close(NULL,h);assert(!units&&!channels);break;}
  assert(r<0 && !h && !units && !channels);
 }
 fail_at=0;stage=0;void* h=NULL;assert(hal.encoder_open(NULL,2,3,&h)==0);
 mcs_esp_encoder_t* e=(mcs_esp_encoder_t*)h;uint32_t n;
 for(int i=0;i<20000;i++){edge(e->unit,2);edge(e->unit,0);edge(e->unit,1);edge(e->unit,3);}
 assert(hal.encoder_read(NULL,h,&n)==0 && n==80000);assert(irq_count==2); /* NOT 80k GPIO ISR calls */
 for(int i=0;i<20000;i++){edge(e->unit,1);edge(e->unit,0);edge(e->unit,2);edge(e->unit,3);}
 assert(hal.encoder_read(NULL,h,&n)==0 && n==0);
#if MCS_ESP_PCNT_ACCUM
 e->unit->accum=UINT32_MAX-1;
#else
 e->base=UINT32_MAX-1;
#endif
 e->unit->count=5;assert(hal.encoder_read(NULL,h,&n)==0 && n==3);
 int previous=clear_count;assert(hal.encoder_read(NULL,h,&n)==0);assert(clear_count==previous);
 hal.encoder_close(NULL,h);assert(!units&&!channels);
 void* all[MCS_HAL_MAX_ENCODERS];for(int i=0;i<MCS_HAL_MAX_ENCODERS;i++)assert(hal.encoder_open(NULL,2,3,&all[i])==0);
 assert(hal.encoder_open(NULL,4,5,&h)==MCS_HAL_EBUSY && !h);
 for(int i=0;i<MCS_HAL_MAX_ENCODERS;i++){hal.encoder_close(NULL,all[i]);}
 assert(!units&&!channels);
 printf("PASS PCNT: %d failure stages, hardware quadrature, overflow extension, wrap and cleanup\n",cases);
 return 0;
}
