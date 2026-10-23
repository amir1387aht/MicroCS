#include "esp_err.h"
#include <stdint.h>
typedef int pcnt_unit_t;
typedef int pcnt_channel_t;
#define PCNT_UNIT_MAX 8
#define PCNT_COUNT_INC 1
#define PCNT_COUNT_DEC -1
#define PCNT_MODE_REVERSE 1
#define PCNT_MODE_KEEP 0
#define PCNT_EVT_L_LIM (1<<4)
#define PCNT_EVT_H_LIM (1<<5)
typedef struct {int pulse_gpio_num,ctrl_gpio_num;pcnt_unit_t unit;pcnt_channel_t channel;int pos_mode,neg_mode,hctrl_mode,lctrl_mode;int16_t counter_h_lim,counter_l_lim;} pcnt_config_t;
esp_err_t pcnt_isr_service_install(int);
esp_err_t pcnt_unit_config(const pcnt_config_t*);
esp_err_t pcnt_counter_pause(pcnt_unit_t);
esp_err_t pcnt_counter_resume(pcnt_unit_t);
esp_err_t pcnt_set_filter_value(pcnt_unit_t,uint16_t);
esp_err_t pcnt_filter_enable(pcnt_unit_t);
esp_err_t pcnt_filter_disable(pcnt_unit_t);
esp_err_t pcnt_event_enable(pcnt_unit_t,int);
esp_err_t pcnt_event_disable(pcnt_unit_t,int);
esp_err_t pcnt_isr_handler_add(pcnt_unit_t,void(*)(void*),void*);
esp_err_t pcnt_isr_handler_remove(pcnt_unit_t);
esp_err_t pcnt_intr_enable(pcnt_unit_t);
esp_err_t pcnt_intr_disable(pcnt_unit_t);
esp_err_t pcnt_counter_clear(pcnt_unit_t);
esp_err_t pcnt_get_counter_value(pcnt_unit_t,int16_t*);
esp_err_t pcnt_get_event_status(pcnt_unit_t,uint32_t*);
