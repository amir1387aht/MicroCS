#include <stdio.h>
#include <assert.h>
#include <string.h>
#define SOC_PCNT_SUPPORTED 1
#define ESP_IDF_VERSION_MAJOR 4
#include "mcs_esp_encoder.h"
static struct {int running,irq,filter;int16_t count;uint32_t events;void(*cb)(void*);void* data;} units[PCNT_UNIT_MAX];
static int stage,fail_at,service;
static int call(void){return ++stage==fail_at?ESP_FAIL:ESP_OK;}
#define TRY() do{int r=call();if(r)return r;}while(0)
esp_err_t gpio_set_direction(gpio_num_t pin,int mode){assert(pin==2||pin==3);assert(mode==GPIO_MODE_INPUT);TRY();return 0;}
esp_err_t gpio_set_pull_mode(gpio_num_t pin,int pull){assert(pin==2||pin==3);assert(pull==GPIO_PULLUP_ONLY);TRY();return 0;}
esp_err_t pcnt_isr_service_install(int flags){assert(!flags);TRY();if(service)return ESP_ERR_INVALID_STATE;service=1;return 0;}
esp_err_t pcnt_unit_config(const pcnt_config_t* c){TRY();assert(c->counter_h_lim==32767 && c->counter_l_lim==-32767);assert(c->pulse_gpio_num==(c->channel?3:2) && c->ctrl_gpio_num==(c->channel?2:3));assert(c->pos_mode==(c->channel?1:-1) && c->neg_mode==(c->channel?-1:1));assert(c->hctrl_mode==PCNT_MODE_REVERSE && c->lctrl_mode==PCNT_MODE_KEEP);return 0;}
esp_err_t pcnt_counter_pause(pcnt_unit_t u){units[u].running=0;return 0;}
esp_err_t pcnt_counter_resume(pcnt_unit_t u){TRY();units[u].running=1;return 0;}
esp_err_t pcnt_set_filter_value(pcnt_unit_t u,uint16_t value){(void)u;TRY();assert(value==80);return 0;}
esp_err_t pcnt_filter_enable(pcnt_unit_t u){TRY();units[u].filter=1;return 0;}
esp_err_t pcnt_filter_disable(pcnt_unit_t u){units[u].filter=0;return 0;}
esp_err_t pcnt_event_enable(pcnt_unit_t u,int event){(void)u;(void)event;TRY();return 0;}
esp_err_t pcnt_event_disable(pcnt_unit_t u,int event){(void)u;(void)event;return 0;}
esp_err_t pcnt_isr_handler_add(pcnt_unit_t u,void(*cb)(void*),void* data){TRY();units[u].cb=cb;units[u].data=data;return 0;}
esp_err_t pcnt_isr_handler_remove(pcnt_unit_t u){units[u].cb=NULL;units[u].data=NULL;return 0;}
esp_err_t pcnt_intr_enable(pcnt_unit_t u){TRY();units[u].irq=1;return 0;}
esp_err_t pcnt_intr_disable(pcnt_unit_t u){units[u].irq=0;return 0;}
esp_err_t pcnt_counter_clear(pcnt_unit_t u){TRY();units[u].count=0;return 0;}
esp_err_t pcnt_get_counter_value(pcnt_unit_t u,int16_t* n){TRY();*n=units[u].count;return 0;}
esp_err_t pcnt_get_event_status(pcnt_unit_t u,uint32_t* n){*n=units[u].events;units[u].events=0;return 0;}
static void clean(void){for(int i=0;i<PCNT_UNIT_MAX;i++)assert(!units[i].running&&!units[i].irq&&!units[i].filter&&!units[i].cb);}
int main(void){
 mcs_hal_t hal;memset(&hal,0,sizeof hal);mcs_esp_encoder_init(&hal);assert(hal.encoder_open && !strcmp(hal.encoder_name,"PCNT"));
 int cases=0;
 for(int f=1;f<40;f++){
  stage=0;fail_at=f;void* h=NULL;int r=hal.encoder_open(NULL,2,3,&h);cases++;
  if(r==0){assert(h);hal.encoder_close(NULL,h);clean();break;}
  assert(r<0 && !h);clean();
 }
 stage=0;fail_at=0;void* h=NULL;uint32_t n;assert(hal.encoder_open(NULL,2,3,&h)==0);
 mcs_esp_legacy_encoder_t* e=(mcs_esp_legacy_encoder_t*)h;
 for(int i=0;i<2;i++){units[e->unit].events=PCNT_EVT_H_LIM;units[e->unit].cb(units[e->unit].data);}
 units[e->unit].count=14466;assert(hal.encoder_read(NULL,h,&n)==0 && n==80000);
 units[e->unit].count=0;for(int i=0;i<2;i++){units[e->unit].events=PCNT_EVT_L_LIM;units[e->unit].cb(units[e->unit].data);}
 assert(hal.encoder_read(NULL,h,&n)==0 && n==0);hal.encoder_close(NULL,h);clean();
 printf("PASS legacy PCNT: %d fault stages, x4 configuration, overflow extension and cleanup\n",cases);return 0;
}
