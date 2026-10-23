#ifndef ENC_PCNT_H
#define ENC_PCNT_H
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct unit* pcnt_unit_handle_t;
typedef struct channel* pcnt_channel_handle_t;
typedef struct {int watch_point_value;} pcnt_watch_event_data_t;
typedef bool (*pcnt_watch_cb_t)(pcnt_unit_handle_t,const pcnt_watch_event_data_t*,void*);
typedef struct {pcnt_watch_cb_t on_reach;} pcnt_event_callbacks_t;
typedef struct {int low_limit,high_limit;
#if ESP_IDF_VERSION_MINOR >= 1
struct {unsigned accum_count:1;} flags;
#endif
} pcnt_unit_config_t;
typedef struct {int edge_gpio_num,level_gpio_num;struct {unsigned invert_edge_input:1;unsigned invert_level_input:1;} flags;} pcnt_chan_config_t;
typedef struct {uint32_t max_glitch_ns;} pcnt_glitch_filter_config_t;
#define PCNT_CHANNEL_EDGE_ACTION_INCREASE 1
#define PCNT_CHANNEL_EDGE_ACTION_DECREASE -1
#define PCNT_CHANNEL_LEVEL_ACTION_INVERSE 1
#define PCNT_CHANNEL_LEVEL_ACTION_KEEP 0
esp_err_t pcnt_new_unit(const pcnt_unit_config_t*,pcnt_unit_handle_t*);
esp_err_t pcnt_del_unit(pcnt_unit_handle_t);
esp_err_t pcnt_new_channel(pcnt_unit_handle_t,const pcnt_chan_config_t*,pcnt_channel_handle_t*);
esp_err_t pcnt_del_channel(pcnt_channel_handle_t);
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t,int,int);
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t,int,int);
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t,const pcnt_glitch_filter_config_t*);
esp_err_t pcnt_unit_register_event_callbacks(pcnt_unit_handle_t,const pcnt_event_callbacks_t*,void*);
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t,int);
esp_err_t pcnt_unit_remove_watch_point(pcnt_unit_handle_t,int);
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t);
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t);
esp_err_t pcnt_unit_start(pcnt_unit_handle_t);
esp_err_t pcnt_unit_stop(pcnt_unit_handle_t);
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t);
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t,int*);
#endif
