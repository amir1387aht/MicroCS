/* Shared ESP-IDF 5.x / Arduino-ESP32 native classic-CAN backend.
 * Include after esp_rc(), ms_ticks() and default TX/RX macros are defined.
 * FD-capable controllers use IDF 5.5's node API, but expose classic frames only. */
#ifndef MCS_ESP_CAN_H
#define MCS_ESP_CAN_H
#include "mcs_hal.h"
#include <string.h>
#include "soc/soc_caps.h"
#if SOC_TWAI_SUPPORTED
#include "driver/gpio.h"
#if SOC_TWAI_SUPPORT_FD
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#define MCS_ESP_CAN_NODES SOC_TWAI_CONTROLLER_NUM
#define MCS_ESP_CAN_TX_SLOTS 4
typedef struct {
    twai_node_handle_t node;
    QueueHandle_t rx, free_tx;
    twai_frame_t tx[MCS_ESP_CAN_TX_SLOTS];
    uint8_t tx_data[MCS_ESP_CAN_TX_SLOTS][8];
} mcs_esp_can_node_t;
static mcs_esp_can_node_t g_esp_can[MCS_ESP_CAN_NODES];
static bool IRAM_ATTR esp_can_rx_done(twai_node_handle_t node, const twai_rx_done_event_data_t* ev, void* ctx) {
    (void)ev;
    mcs_esp_can_node_t* d = (mcs_esp_can_node_t*)ctx;
    uint8_t bytes[64]; twai_frame_t f;
    memset(&f, 0, sizeof f); f.buffer = bytes; f.buffer_len = sizeof bytes;
    if (twai_node_receive_from_isr(node, &f) != ESP_OK || f.header.fdf || f.header.dlc > 8) return false;
    mcs_can_frame_t fr; memset(&fr, 0, sizeof fr);
    fr.id = f.header.id; fr.extended = f.header.ide; fr.rtr = f.header.rtr; fr.len = (uint8_t)f.header.dlc;
    if (!fr.rtr) memcpy(fr.data, bytes, fr.len);
    BaseType_t woke = pdFALSE; xQueueSendFromISR(d->rx, &fr, &woke);
    return woke == pdTRUE;
}
static bool IRAM_ATTR esp_can_tx_done(twai_node_handle_t node, const twai_tx_done_event_data_t* ev, void* ctx) {
    (void)node;
    mcs_esp_can_node_t* d = (mcs_esp_can_node_t*)ctx;
    BaseType_t woke = pdFALSE;
    for (int i = 0; i < MCS_ESP_CAN_TX_SLOTS; i++) if (ev->done_tx_frame == &d->tx[i]) {
        xQueueSendFromISR(d->free_tx, &i, &woke); break;
    }
    return woke == pdTRUE;
}
static int esp_can_close(void* ctx, int bus) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP_CAN_NODES) return MCS_HAL_ENOTSUP;
    mcs_esp_can_node_t* d = &g_esp_can[bus];
    if (d->node) {
        esp_err_t e = twai_node_disable(d->node);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return esp_rc(e);
        e = twai_node_delete(d->node);
        if (e != ESP_OK) return esp_rc(e);
        d->node = NULL;
    }
    if (d->rx) { vQueueDelete(d->rx); d->rx = NULL; }
    if (d->free_tx) { vQueueDelete(d->free_tx); d->free_tx = NULL; }
    return 0;
}
static int esp_can_configure(void* ctx, int bus, const mcs_can_config_t* cfg) {
    if (bus < 0 || bus >= MCS_ESP_CAN_NODES) return MCS_HAL_ENOTSUP;
    if (cfg->opts.n) return MCS_HAL_ENOTSUP;
    if (!cfg->bitrate || cfg->bitrate > 1000000 || cfg->mode > MCS_CAN_LOOPBACK) return MCS_HAL_EINVAL;
    int tx = cfg->tx >= 0 ? cfg->tx : bus == 0 ? MCS_CAN_TX_DEFAULT : -1;
    int rx = cfg->rx >= 0 ? cfg->rx : bus == 0 ? MCS_CAN_RX_DEFAULT : -1;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(tx) || !GPIO_IS_VALID_GPIO(rx) || tx == rx) return MCS_HAL_EINVAL;
    int r = esp_can_close(ctx, bus); if (r) return r;
    mcs_esp_can_node_t* d = &g_esp_can[bus];
    d->rx = xQueueCreate(16, sizeof(mcs_can_frame_t)); d->free_tx = xQueueCreate(MCS_ESP_CAN_TX_SLOTS, sizeof(int));
    if (!d->rx || !d->free_tx) { esp_can_close(ctx, bus); return MCS_HAL_ERR; }
    for (int i = 0; i < MCS_ESP_CAN_TX_SLOTS; i++) xQueueSend(d->free_tx, &i, 0);
    twai_onchip_node_config_t c; memset(&c, 0, sizeof c);
    c.io_cfg.tx = (gpio_num_t)tx; c.io_cfg.rx = (gpio_num_t)rx;
    c.io_cfg.quanta_clk_out = (gpio_num_t)-1; c.io_cfg.bus_off_indicator = (gpio_num_t)-1;
    c.bit_timing.bitrate = cfg->bitrate; c.tx_queue_depth = MCS_ESP_CAN_TX_SLOTS;
    c.fail_retry_cnt = 3; /* bounded retries: a disconnected bus must not retain TX forever */
    c.flags.enable_listen_only = cfg->mode == MCS_CAN_LISTEN_ONLY;
    c.flags.enable_self_test = c.flags.enable_loopback = cfg->mode == MCS_CAN_LOOPBACK;
    esp_err_t e = twai_new_node_onchip(&c, &d->node);
    twai_event_callbacks_t callbacks; memset(&callbacks, 0, sizeof callbacks);
    callbacks.on_rx_done = esp_can_rx_done; callbacks.on_tx_done = esp_can_tx_done;
    if (e == ESP_OK) e = twai_node_register_event_callbacks(d->node, &callbacks, d);
    twai_mask_filter_config_t filter; memset(&filter, 0, sizeof filter); filter.no_fd = true;
    if (e == ESP_OK) e = twai_node_config_mask_filter(d->node, 0, &filter);
    filter.is_ext = true;
    if (e == ESP_OK) e = twai_node_config_mask_filter(d->node, 1, &filter);
    if (e == ESP_OK) e = twai_node_enable(d->node);
    if (e != ESP_OK) { esp_can_close(ctx, bus); return esp_rc(e); }
    return 0;
}
static int esp_can_send(void* ctx, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP_CAN_NODES || !g_esp_can[bus].node) return MCS_HAL_ENODEV;
    if (fr->len > 8 || fr->id > (fr->extended ? 0x1fffffffu : 0x7ffu)) return MCS_HAL_EINVAL;
    mcs_esp_can_node_t* d = &g_esp_can[bus]; int slot;
    if (xQueueReceive(d->free_tx, &slot, ms_ticks(timeout_ms)) != pdTRUE) return MCS_HAL_ETIMEOUT;
    twai_frame_t* f = &d->tx[slot]; memset(f, 0, sizeof *f);
    f->buffer = d->tx_data[slot]; f->buffer_len = 8;
    memcpy(f->buffer, fr->data, fr->len);
    f->header.id = fr->id; f->header.ide = fr->extended; f->header.rtr = fr->rtr; f->header.dlc = fr->len;
    /* IDF retains the frame pointer until on_tx_done. Never hand it stack memory. */
    esp_err_t e = twai_node_transmit(d->node, f, (int)timeout_ms);
    if (e != ESP_OK) xQueueSend(d->free_tx, &slot, 0);
    return esp_rc(e);
}
static int esp_can_recv(void* ctx, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP_CAN_NODES || !g_esp_can[bus].node) return MCS_HAL_ENODEV;
    return xQueueReceive(g_esp_can[bus].rx, fr, ms_ticks(timeout_ms)) == pdTRUE ? 0 : MCS_HAL_ETIMEOUT;
}
#else /* Legacy TWAI: ESP32/S2/S3/C3/C6/H2/P4 (where SOC_TWAI_SUPPORTED). */
#include "driver/twai.h"
static bool g_twai;
static uint8_t g_twai_mode;
static int esp_can_close(void* ctx, int bus) {
    (void)ctx;
    if (bus != 0) return MCS_HAL_ENOTSUP;
    if (!g_twai) return 0;
    /* A bus-off controller may already be stopped. Uninstall must still run. */
    (void)twai_stop();
    esp_err_t e = twai_driver_uninstall();
    if (e == ESP_OK) g_twai = false;
    return esp_rc(e);
}
static int esp_can_configure(void* ctx, int bus, const mcs_can_config_t* cfg) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    if (cfg->opts.n) return MCS_HAL_ENOTSUP;
    int tx = cfg->tx >= 0 ? cfg->tx : MCS_CAN_TX_DEFAULT;
    int rx = cfg->rx >= 0 ? cfg->rx : MCS_CAN_RX_DEFAULT;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(tx) || !GPIO_IS_VALID_GPIO(rx) || tx == rx || cfg->mode > MCS_CAN_LOOPBACK) return MCS_HAL_EINVAL;
    twai_timing_config_t t;
    switch (cfg->bitrate) {
    case 25000:   { twai_timing_config_t x = TWAI_TIMING_CONFIG_25KBITS(); t = x; break; }
    case 50000:   { twai_timing_config_t x = TWAI_TIMING_CONFIG_50KBITS(); t = x; break; }
    case 100000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_100KBITS(); t = x; break; }
    case 125000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_125KBITS(); t = x; break; }
    case 250000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_250KBITS(); t = x; break; }
    case 500000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_500KBITS(); t = x; break; }
    case 800000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_800KBITS(); t = x; break; }
    case 1000000: { twai_timing_config_t x = TWAI_TIMING_CONFIG_1MBITS(); t = x; break; }
    default: return MCS_HAL_EINVAL;
    }
    int r = esp_can_close(ctx, bus); if (r) return r;
    twai_mode_t mode = cfg->mode == MCS_CAN_LOOPBACK ? TWAI_MODE_NO_ACK : cfg->mode == MCS_CAN_LISTEN_ONLY ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx, (gpio_num_t)rx, mode);
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t e = twai_driver_install(&g, &t, &f);
    if (e != ESP_OK) return esp_rc(e);
    g_twai = true;
    e = twai_start();
    if (e != ESP_OK) { esp_can_close(ctx, bus); return esp_rc(e); }
    g_twai_mode = cfg->mode; return 0;
}
static int esp_can_send(void* ctx, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (bus != 0 || !g_twai) return MCS_HAL_ENODEV;
    if (fr->len > 8 || fr->id > (fr->extended ? 0x1fffffffu : 0x7ffu)) return MCS_HAL_EINVAL;
    twai_message_t m; memset(&m, 0, sizeof m);
    m.identifier = fr->id; m.extd = fr->extended; m.rtr = fr->rtr;
    m.self = g_twai_mode == MCS_CAN_LOOPBACK;
    m.data_length_code = fr->len; memcpy(m.data, fr->data, fr->len);
    return esp_rc(twai_transmit(&m, ms_ticks(timeout_ms)));
}
static int esp_can_recv(void* ctx, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (bus != 0 || !g_twai) return MCS_HAL_ENODEV;
    twai_message_t m; esp_err_t e = twai_receive(&m, ms_ticks(timeout_ms));
    if (e != ESP_OK) return esp_rc(e);
    memset(fr, 0, sizeof *fr);
    fr->id = m.identifier; fr->extended = m.extd; fr->rtr = m.rtr;
    fr->len = m.data_length_code > 8 ? 8 : m.data_length_code;
    if (!fr->rtr) memcpy(fr->data, m.data, fr->len);
    return 0;
}
#endif
static int esp_can_open(void* ctx, int bus, uint32_t rate) {
    mcs_can_config_t cfg; mcs_can_config_init(&cfg, rate); return esp_can_configure(ctx, bus, &cfg);
}
#endif /* SOC_TWAI_SUPPORTED */
#endif
