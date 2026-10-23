/* Shared ESP-IDF / Arduino-ESP32 PCNT backend. No per-GPIO-edge interrupts.
 * Uses the public IDF 5+ driver, including IDF 5.0 (before accum_count existed).
 * Only limit events extend the 16-bit hardware counter to a modulo-32 total. */
#ifndef MCS_ESP_ENCODER_H
#define MCS_ESP_ENCODER_H
#include "mcs_hal.h"
#if MCS_ENABLE_ENCODER && MCS_ENABLE_ENCODER_HW && SOC_PCNT_SUPPORTED && ESP_IDF_VERSION_MAJOR >= 5
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include <string.h>
#define MCS_ESP_PCNT_ACCUM (ESP_IDF_VERSION_MAJOR > 5 || ESP_IDF_VERSION_MINOR >= 1)
#ifndef MCS_ESP_ENCODER_INVERT
#define MCS_ESP_ENCODER_INVERT(pin) 0
#endif
typedef struct {
    pcnt_unit_handle_t unit;
    pcnt_channel_handle_t channel[2];
#if !MCS_ESP_PCNT_ACCUM
    volatile uint32_t base;
#endif
    uint8_t watches, enabled, started;
} mcs_esp_encoder_t;
static mcs_esp_encoder_t mcs_esp_encoders[MCS_HAL_MAX_ENCODERS];
static int mcs_esp_encoder_rc(esp_err_t rc) {
    return rc == ESP_OK ? 0 : rc == ESP_ERR_INVALID_ARG ? MCS_HAL_EINVAL :
        rc == ESP_ERR_NOT_FOUND || rc == ESP_ERR_INVALID_STATE ? MCS_HAL_EBUSY : MCS_HAL_ERR;
}
#if !MCS_ESP_PCNT_ACCUM
static bool IRAM_ATTR mcs_esp_encoder_limit(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t* event, void* data) {
    (void)unit;
    mcs_esp_encoder_t* e = (mcs_esp_encoder_t*)data;
    e->base += (uint32_t)event->watch_point_value;
    return false;
}
#endif
static void mcs_esp_encoder_close(void* ctx, void* handle) {
    (void)ctx;
    mcs_esp_encoder_t* e = (mcs_esp_encoder_t*)handle;
    if (e->started) pcnt_unit_stop(e->unit);
    if (e->enabled) pcnt_unit_disable(e->unit);
    for (int i = 0; i < 2; i++) if (e->channel[i]) pcnt_del_channel(e->channel[i]);
    if (e->watches & 1) pcnt_unit_remove_watch_point(e->unit, -32767);
    if (e->watches & 2) pcnt_unit_remove_watch_point(e->unit, 32767);
    if (e->unit) pcnt_del_unit(e->unit);
    memset(e, 0, sizeof *e);
}
static int mcs_esp_encoder_open(void* ctx, int a, int b, void** handle) {
    *handle = NULL;
    if (!GPIO_IS_VALID_GPIO(a) || !GPIO_IS_VALID_GPIO(b) || a == b) return MCS_HAL_EINVAL;
    mcs_esp_encoder_t* e = NULL;
    for (int i = 0; i < MCS_HAL_MAX_ENCODERS; i++) if (!mcs_esp_encoders[i].unit) { e = &mcs_esp_encoders[i]; break; }
    if (!e) return MCS_HAL_EBUSY;
    pcnt_unit_config_t config; memset(&config, 0, sizeof config);
    config.low_limit = -32767; config.high_limit = 32767;
#if MCS_ESP_PCNT_ACCUM
    config.flags.accum_count = 1;
#endif
    esp_err_t rc = pcnt_new_unit(&config, &e->unit);
    if (rc != ESP_OK) return mcs_esp_encoder_rc(rc);
    /* PCNT GPIO matrix needs input configuration; do not install GPIO IRQs. */
    int pins[2] = { a, b };
    for (int i = 0; i < 2 && rc == ESP_OK; i++) {
        rc = gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_INPUT);
        if (rc == ESP_OK) rc = gpio_set_pull_mode((gpio_num_t)pins[i], GPIO_PULLUP_ONLY);
    }
    for (int i = 0; i < 2 && rc == ESP_OK; i++) {
        pcnt_chan_config_t channel; memset(&channel, 0, sizeof channel);
        channel.edge_gpio_num = pins[i]; channel.level_gpio_num = pins[1-i];
        channel.flags.invert_edge_input = MCS_ESP_ENCODER_INVERT(pins[i]);
        channel.flags.invert_level_input = MCS_ESP_ENCODER_INVERT(pins[1-i]);
        rc = pcnt_new_channel(e->unit, &channel, &e->channel[i]);
        /* Positive sequence 11 -> 10 -> 00 -> 01 -> 11. */
        if (rc == ESP_OK) rc = pcnt_channel_set_edge_action(e->channel[i],
            i ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_DECREASE,
            i ? PCNT_CHANNEL_EDGE_ACTION_DECREASE : PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        if (rc == ESP_OK) rc = pcnt_channel_set_level_action(e->channel[i], PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    }
    pcnt_glitch_filter_config_t filter; memset(&filter, 0, sizeof filter); filter.max_glitch_ns = 1000;
    if (rc == ESP_OK) rc = pcnt_unit_set_glitch_filter(e->unit, &filter);
#if !MCS_ESP_PCNT_ACCUM
    pcnt_event_callbacks_t callbacks; memset(&callbacks, 0, sizeof callbacks); callbacks.on_reach = mcs_esp_encoder_limit;
    if (rc == ESP_OK) rc = pcnt_unit_register_event_callbacks(e->unit, &callbacks, e);
#endif
    if (rc == ESP_OK) { rc = pcnt_unit_add_watch_point(e->unit, -32767); if (rc == ESP_OK) e->watches |= 1; }
    if (rc == ESP_OK) { rc = pcnt_unit_add_watch_point(e->unit, 32767); if (rc == ESP_OK) e->watches |= 2; }
    if (rc == ESP_OK) { rc = pcnt_unit_enable(e->unit); if (rc == ESP_OK) e->enabled = 1; }
    if (rc == ESP_OK) rc = pcnt_unit_clear_count(e->unit);
    if (rc == ESP_OK) { rc = pcnt_unit_start(e->unit); if (rc == ESP_OK) e->started = 1; }
    if (rc != ESP_OK) { mcs_esp_encoder_close(ctx, e); return mcs_esp_encoder_rc(rc); }
    *handle = e;
    return 0;
}
static int mcs_esp_encoder_read(void* ctx, void* handle, uint32_t* count) {
    (void)ctx;
    mcs_esp_encoder_t* e = (mcs_esp_encoder_t*)handle;
#if MCS_ESP_PCNT_ACCUM
    int value;
    esp_err_t rc = pcnt_unit_get_count(e->unit, &value);
    if (rc != ESP_OK) return mcs_esp_encoder_rc(rc);
    *count = (uint32_t)value;
    return 0;
#else
    uint32_t before, after; int low; esp_err_t rc;
    /* Retake the snapshot if an overflow ISR interrupted the hardware read.
     * No MicroCS critical lock: SDK/ISR locks must be able to make progress. */
    do {
        before = e->base;
        rc = pcnt_unit_get_count(e->unit, &low);
        if (rc != ESP_OK) return mcs_esp_encoder_rc(rc);
        after = e->base;
    } while (before != after);
    *count = before + (uint32_t)low;
    return 0;
#endif
}
static void mcs_esp_encoder_init(mcs_hal_t* hal) {
    hal->encoder_open = mcs_esp_encoder_open;
    hal->encoder_read = mcs_esp_encoder_read;
    hal->encoder_close = mcs_esp_encoder_close;
    hal->encoder_name = "PCNT";
}
#elif MCS_ENABLE_ENCODER && MCS_ENABLE_ENCODER_HW && SOC_PCNT_SUPPORTED && ESP_IDF_VERSION_MAJOR == 4
#include "mcs_esp_encoder_legacy.h"
#else
static inline void mcs_esp_encoder_init(mcs_hal_t* hal) { (void)hal; }
#endif
#endif
