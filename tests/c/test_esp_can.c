/* ESP32 CAN SDK doubles: failure cleanup, modes and async TX pointer lifetime.
 * Compiled both as C (ESP-IDF) and C++ (Arduino-ESP32). */
#include "mcs_hal.h"
#include "sdk.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static int esp_rc(esp_err_t e) { return e == ESP_OK ? 0 : e == ESP_ERR_TIMEOUT ? MCS_HAL_ETIMEOUT : e == ESP_ERR_INVALID_ARG ? MCS_HAL_EINVAL : MCS_HAL_ERR; }
static TickType_t ms_ticks(uint32_t ms) { return ms; }
#define MCS_CAN_TX_DEFAULT 4
#define MCS_CAN_RX_DEFAULT 5
#include "mcs_esp_can.h"
void mcs_can_config_init(mcs_can_config_t* c, uint32_t rate) { memset(c, 0, sizeof *c); c->bitrate = rate ? rate : 500000; c->tx = c->rx = -1; }
static int installed, fail_start;
struct queue { unsigned cap, size, head, count; uint8_t data[512]; };
static int queues;
QueueHandle_t xQueueCreate(unsigned n, unsigned size) { struct queue* q = (struct queue*)calloc(1, sizeof *q); assert(q && n * size <= sizeof q->data); q->cap = n; q->size = size; queues++; return q; }
BaseType_t xQueueReceive(QueueHandle_t q, void* p, TickType_t ticks) { (void)ticks; if (!q->count) return pdFALSE; memcpy(p, q->data + q->head * q->size, q->size); q->head = (q->head + 1) % q->cap; q->count--; return pdTRUE; }
BaseType_t xQueueSend(QueueHandle_t q, const void* p, TickType_t ticks) { (void)ticks; if (q->count == q->cap) return pdFALSE; memcpy(q->data + (q->head + q->count) % q->cap * q->size, p, q->size); q->count++; return pdTRUE; }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void* p, BaseType_t* woke) { *woke = pdFALSE; return xQueueSend(q, p, 0); }
void vQueueDelete(QueueHandle_t q) { assert(q); free(q); queues--; }
#if CAN_VARIANT
struct node { twai_event_callbacks_t cb; void* ctx; const twai_frame_t* tx[4]; int count; };
static int filters, deletes, fail_filter;
static uint32_t bitrate;
esp_err_t twai_new_node_onchip(const twai_onchip_node_config_t* c, twai_node_handle_t* out) {
    assert(c->io_cfg.tx == 4 && c->io_cfg.rx == 5 && c->io_cfg.quanta_clk_out == -1 && c->io_cfg.bus_off_indicator == -1);
    assert(c->bit_timing.bitrate == 250000 || c->bit_timing.bitrate == 500000);
    bitrate = c->bit_timing.bitrate;
    *out = (struct node*)calloc(1, sizeof(struct node)); installed++; return ESP_OK;
}
esp_err_t twai_node_disable(twai_node_handle_t n) { assert(n); return ESP_OK; }
esp_err_t twai_node_delete(twai_node_handle_t n) { assert(n); free(n); installed--; deletes++; return ESP_OK; }
esp_err_t twai_node_enable(twai_node_handle_t n) { assert(n); return fail_start ? ESP_FAIL : ESP_OK; }
esp_err_t twai_node_register_event_callbacks(twai_node_handle_t n, const twai_event_callbacks_t* cb, void* ctx) { n->cb = *cb; n->ctx = ctx; return ESP_OK; }
esp_err_t twai_node_config_mask_filter(twai_node_handle_t n, uint8_t id, const twai_mask_filter_config_t* f) { assert(n && id < 2 && f->is_ext == (id == 1) && !f->mask && f->no_fd); filters++; return fail_filter ? ESP_FAIL : ESP_OK; }
esp_err_t twai_node_transmit(twai_node_handle_t n, const twai_frame_t* f, int timeout) { assert(timeout == 0 && n->count < 4); n->tx[n->count++] = f; return ESP_OK; }
esp_err_t twai_node_receive_from_isr(twai_node_handle_t n, twai_frame_t* f) { (void)n; f->header.id = 0x123; f->header.dlc = 2; f->buffer[0] = 0xaa; f->buffer[1] = 0xbb; return ESP_OK; }
#else
static twai_general_config_t last;
static twai_message_t last_tx;
esp_err_t twai_driver_install(const twai_general_config_t* g, const twai_timing_config_t* t, const twai_filter_config_t* f) { assert(!installed && t->bitrate && f->accept_all); last = *g; installed++; return ESP_OK; }
esp_err_t twai_driver_uninstall(void) { assert(installed); installed--; return ESP_OK; }
esp_err_t twai_start(void) { return fail_start ? ESP_FAIL : ESP_OK; }
esp_err_t twai_stop(void) { return ESP_OK; }
esp_err_t twai_transmit(const twai_message_t* f, TickType_t ticks) { assert(!ticks); last_tx = *f; return ESP_OK; }
esp_err_t twai_receive(twai_message_t* f, TickType_t ticks) { (void)f; assert(!ticks); return ESP_ERR_TIMEOUT; }
#endif
int main(void) {
    mcs_can_config_t c; mcs_can_config_init(&c, 250000);
    assert(esp_can_open(NULL, -1, 500000) == MCS_HAL_ENOTSUP);
    c.tx = 100; assert(esp_can_configure(NULL, 0, &c) == MCS_HAL_EINVAL && !installed); c.tx = -1;
    fail_start = 1; assert(esp_can_configure(NULL, 0, &c) < 0 && !installed && !queues); fail_start = 0;
    assert(esp_can_configure(NULL, 0, &c) == 0 && installed == 1);
    mcs_can_frame_t fr; memset(&fr, 0, sizeof fr); fr.id = 0x1abcde; fr.extended = 1; fr.len = 8; fr.data[0] = 42;
    assert(esp_can_send(NULL, 0, &fr, 0) == 0);
#if CAN_VARIANT
    twai_node_handle_t node = g_esp_can[0].node;
    assert(bitrate == 250000 && filters >= 4);
    fr.data[0] = 99;
    assert(node->tx[0]->header.id == 0x1abcde && node->tx[0]->buffer[0] == 42); /* no borrowed caller memory */
    for (int i = 0; i < 3; i++) assert(esp_can_send(NULL, 0, &fr, 0) == 0);
    assert(esp_can_send(NULL, 0, &fr, 0) == MCS_HAL_ETIMEOUT);
    twai_tx_done_event_data_t done; done.is_tx_success = true; done.done_tx_frame = node->tx[0];
    node->count = 0; node->cb.on_tx_done(node, &done, node->ctx);
    assert(esp_can_send(NULL, 0, &fr, 0) == 0);
    twai_rx_done_event_data_t rx; memset(&rx, 0, sizeof rx); node->cb.on_rx_done(node, &rx, node->ctx);
    assert(esp_can_recv(NULL, 0, &fr, 0) == 0 && fr.id == 0x123 && fr.data[1] == 0xbb);
    assert(esp_can_recv(NULL, 0, &fr, 0) == MCS_HAL_ETIMEOUT);
    assert(esp_can_close(NULL, 0) == 0 && !installed && !queues && deletes >= 2);
    fail_filter = 1; assert(esp_can_configure(NULL, 0, &c) < 0 && !installed && !queues); fail_filter = 0;
    assert(esp_can_configure(NULL, 1, &c) == MCS_HAL_EINVAL); /* no default pins for node 1 */
    c.tx = 4; c.rx = 5; assert(esp_can_configure(NULL, 1, &c) == 0); assert(esp_can_close(NULL, 1) == 0);
#else
    assert(last.mode == TWAI_MODE_NORMAL && last_tx.extd && last_tx.data[0] == 42);
    c.mode = MCS_CAN_LOOPBACK; assert(esp_can_configure(NULL, 0, &c) == 0 && installed == 1 && last.mode == TWAI_MODE_NO_ACK);
    assert(esp_can_send(NULL, 0, &fr, 0) == 0 && last_tx.self);
    assert(esp_can_recv(NULL, 0, &fr, 0) == MCS_HAL_ETIMEOUT);
    assert(esp_can_close(NULL, 0) == 0 && !installed);
#endif
    assert(esp_can_close(NULL, 0) == 0 && !installed && !queues);
    puts("PASS ESP CAN SDK lifecycle and frame tests"); return 0;
}
