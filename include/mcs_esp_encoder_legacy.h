/* IDF 4.x / Arduino-ESP32 2.x PCNT backend. The legacy SDK has no unit claim
 * allocator: reserve units [0, min(MCS_HAL_MAX_ENCODERS, PCNT_UNIT_MAX)) for
 * MicroCS and do not use that range from native application code. */
#ifndef MCS_ESP_ENCODER_LEGACY_H
#define MCS_ESP_ENCODER_LEGACY_H
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include <string.h>
typedef struct {pcnt_unit_t unit;volatile uint32_t base;uint8_t used,configured,handler;} mcs_esp_legacy_encoder_t;
static mcs_esp_legacy_encoder_t mcs_esp_legacy_encoders[MCS_HAL_MAX_ENCODERS];
static int mcs_esp_legacy_rc(esp_err_t r){return r==ESP_OK?0:r==ESP_ERR_INVALID_ARG?MCS_HAL_EINVAL:r==ESP_ERR_INVALID_STATE?MCS_HAL_EBUSY:MCS_HAL_ERR;}
static void IRAM_ATTR mcs_esp_legacy_irq(void* data){
    mcs_esp_legacy_encoder_t* e=(mcs_esp_legacy_encoder_t*)data;
    uint32_t events=0;
    if(pcnt_get_event_status(e->unit,&events)!=ESP_OK)return;
    if(events&PCNT_EVT_H_LIM)e->base+=32767u;
    if(events&PCNT_EVT_L_LIM)e->base-=32767u;
}
static void mcs_esp_legacy_close(void* ctx,void* handle){
    (void)ctx;mcs_esp_legacy_encoder_t* e=(mcs_esp_legacy_encoder_t*)handle;
    if(e->configured){pcnt_counter_pause(e->unit);pcnt_intr_disable(e->unit);pcnt_event_disable(e->unit,PCNT_EVT_H_LIM);pcnt_event_disable(e->unit,PCNT_EVT_L_LIM);pcnt_filter_disable(e->unit);}
    if(e->handler)pcnt_isr_handler_remove(e->unit);
    memset(e,0,sizeof *e);
}
static int mcs_esp_legacy_open(void* ctx,int a,int b,void** handle){
    *handle=NULL;
    if(!GPIO_IS_VALID_GPIO(a)||!GPIO_IS_VALID_GPIO(b)||a==b)return MCS_HAL_EINVAL;
    mcs_esp_legacy_encoder_t* e=NULL;
    for(int i=0;i<MCS_HAL_MAX_ENCODERS && i<PCNT_UNIT_MAX;i++)if(!mcs_esp_legacy_encoders[i].used){e=&mcs_esp_legacy_encoders[i];e->used=1;e->unit=(pcnt_unit_t)i;break;}
    if(!e)return MCS_HAL_EBUSY;
    esp_err_t r=pcnt_isr_service_install(0);
    if(r==ESP_ERR_INVALID_STATE)r=ESP_OK; /* share the SDK service, never uninstall somebody else's */
    int pins[2]={a,b};
    for(int i=0;i<2 && r==ESP_OK;i++){
        r=gpio_set_direction((gpio_num_t)pins[i],GPIO_MODE_INPUT);
        if(r==ESP_OK)r=gpio_set_pull_mode((gpio_num_t)pins[i],GPIO_PULLUP_ONLY);
        pcnt_config_t c;memset(&c,0,sizeof c);
        c.pulse_gpio_num=pins[i];c.ctrl_gpio_num=pins[1-i];c.unit=e->unit;c.channel=(pcnt_channel_t)i;
        c.pos_mode=i?PCNT_COUNT_INC:PCNT_COUNT_DEC;c.neg_mode=i?PCNT_COUNT_DEC:PCNT_COUNT_INC;
        c.hctrl_mode=PCNT_MODE_REVERSE;c.lctrl_mode=PCNT_MODE_KEEP;c.counter_h_lim=32767;c.counter_l_lim=-32767;
        if(r==ESP_OK){r=pcnt_unit_config(&c);if(r==ESP_OK)e->configured=1;}
    }
    if(r==ESP_OK)r=pcnt_counter_pause(e->unit);
    if(r==ESP_OK)r=pcnt_set_filter_value(e->unit,80); /* 1 us at the legacy 80 MHz APB clock */
    if(r==ESP_OK)r=pcnt_filter_enable(e->unit);
    if(r==ESP_OK)r=pcnt_event_enable(e->unit,PCNT_EVT_L_LIM);
    if(r==ESP_OK)r=pcnt_event_enable(e->unit,PCNT_EVT_H_LIM);
    if(r==ESP_OK){r=pcnt_isr_handler_add(e->unit,mcs_esp_legacy_irq,e);if(r==ESP_OK)e->handler=1;}
    if(r==ESP_OK)r=pcnt_intr_enable(e->unit);
    if(r==ESP_OK)r=pcnt_counter_clear(e->unit);
    if(r==ESP_OK)r=pcnt_counter_resume(e->unit);
    if(r!=ESP_OK){mcs_esp_legacy_close(ctx,e);return mcs_esp_legacy_rc(r);}
    *handle=e;return 0;
}
static int mcs_esp_legacy_read(void* ctx,void* handle,uint32_t* count){
    (void)ctx;mcs_esp_legacy_encoder_t* e=(mcs_esp_legacy_encoder_t*)handle;
    uint32_t first,last;int16_t low;
    do{first=e->base;esp_err_t r=pcnt_get_counter_value(e->unit,&low);if(r!=ESP_OK)return mcs_esp_legacy_rc(r);last=e->base;}while(first!=last);
    *count=first+(uint32_t)(int32_t)low;return 0;
}
static void mcs_esp_encoder_init(mcs_hal_t* hal){
    hal->encoder_open=mcs_esp_legacy_open;hal->encoder_read=mcs_esp_legacy_read;hal->encoder_close=mcs_esp_legacy_close;hal->encoder_name="PCNT";
}
#endif
