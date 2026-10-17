/* Minimal public-IDF CAN contracts for lifecycle tests, not an SDK replacement.
 * Signatures/layout fields follow IDF 5.5.2; real SDK builds validate integration. */
#ifndef TEST_CAN_SDK_H
#define TEST_CAN_SDK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define SOC_TWAI_SUPPORTED 1
#define SOC_TWAI_SUPPORT_FD CAN_VARIANT
#define SOC_TWAI_CONTROLLER_NUM 2
#define IRAM_ATTR
#define GPIO_IS_VALID_GPIO(p) ((p) >= 0 && (p) < 40)
#define GPIO_IS_VALID_OUTPUT_GPIO(p) ((p) >= 0 && (p) < 34)
typedef int gpio_num_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_FAIL 4
#define ESP_ERR_NO_MEM 5
typedef unsigned TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
struct queue;
typedef struct queue* QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned n, unsigned size);
BaseType_t xQueueReceive(QueueHandle_t q, void* p, TickType_t ticks);
BaseType_t xQueueSend(QueueHandle_t q, const void* p, TickType_t ticks);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void* p, BaseType_t* woke);
void vQueueDelete(QueueHandle_t q);
#if CAN_VARIANT
struct node;
typedef struct node* twai_node_handle_t;
typedef struct { uint32_t id; uint16_t dlc; uint32_t ide:1, rtr:1, fdf:1, brs:1, esi:1; uint64_t timestamp; } twai_frame_header_t;
typedef struct { twai_frame_header_t header; uint8_t* buffer; size_t buffer_len; } twai_frame_t;
typedef struct { bool is_tx_success; const twai_frame_t* done_tx_frame; } twai_tx_done_event_data_t;
typedef struct { int unused; } twai_rx_done_event_data_t;
typedef struct {
    bool (*on_tx_done)(twai_node_handle_t, const twai_tx_done_event_data_t*, void*);
    bool (*on_rx_done)(twai_node_handle_t, const twai_rx_done_event_data_t*, void*);
} twai_event_callbacks_t;
typedef struct {
    struct { gpio_num_t tx, rx, quanta_clk_out, bus_off_indicator; } io_cfg;
    struct { uint32_t bitrate; uint16_t sp_permill, ssp_permill; } bit_timing, data_timing;
    int8_t fail_retry_cnt; uint32_t tx_queue_depth;
    struct { uint32_t enable_self_test:1, enable_loopback:1, enable_listen_only:1, no_receive_rtr:1; } flags;
} twai_onchip_node_config_t;
typedef struct { uint32_t id, mask; bool is_ext, no_classic, no_fd, dual_filter; } twai_mask_filter_config_t;
esp_err_t twai_new_node_onchip(const twai_onchip_node_config_t*, twai_node_handle_t*);
esp_err_t twai_node_disable(twai_node_handle_t);
esp_err_t twai_node_delete(twai_node_handle_t);
esp_err_t twai_node_enable(twai_node_handle_t);
esp_err_t twai_node_register_event_callbacks(twai_node_handle_t, const twai_event_callbacks_t*, void*);
esp_err_t twai_node_config_mask_filter(twai_node_handle_t, uint8_t, const twai_mask_filter_config_t*);
esp_err_t twai_node_transmit(twai_node_handle_t, const twai_frame_t*, int);
esp_err_t twai_node_receive_from_isr(twai_node_handle_t, twai_frame_t*);
#else
typedef int twai_mode_t;
#define TWAI_MODE_NORMAL 0
#define TWAI_MODE_NO_ACK 1
#define TWAI_MODE_LISTEN_ONLY 2
typedef struct { gpio_num_t tx_io, rx_io; twai_mode_t mode; } twai_general_config_t;
#define TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, mode) {tx,rx,mode}
typedef struct { int accept_all; } twai_filter_config_t;
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() {1}
typedef struct { int bitrate; } twai_timing_config_t;
#define TWAI_TIMING_CONFIG_25KBITS() {25000}
#define TWAI_TIMING_CONFIG_50KBITS() {50000}
#define TWAI_TIMING_CONFIG_100KBITS() {100000}
#define TWAI_TIMING_CONFIG_125KBITS() {125000}
#define TWAI_TIMING_CONFIG_250KBITS() {250000}
#define TWAI_TIMING_CONFIG_500KBITS() {500000}
#define TWAI_TIMING_CONFIG_800KBITS() {800000}
#define TWAI_TIMING_CONFIG_1MBITS() {1000000}
typedef struct { uint32_t identifier; unsigned extd:1, rtr:1, self:1; uint8_t data_length_code, data[8]; } twai_message_t;
esp_err_t twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*, const twai_filter_config_t*);
esp_err_t twai_driver_uninstall(void);
esp_err_t twai_start(void);
esp_err_t twai_stop(void);
esp_err_t twai_transmit(const twai_message_t*, TickType_t);
esp_err_t twai_receive(twai_message_t*, TickType_t);
#endif
#endif
