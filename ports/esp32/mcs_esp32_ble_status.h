/* Include esp_err.h and mcs_hal.h first. SDK init errors are not connection
 * errors; in particular ESP_ERR_NO_MEM (257) must not become ENODEV. */
#ifndef MCS_ESP32_BLE_STATUS_H
#define MCS_ESP32_BLE_STATUS_H
static inline int mcs_esp32_ble_init_status(esp_err_t e) {
    if (e == ESP_OK) return 0;
    if (e == ESP_ERR_NO_MEM) return MCS_HAL_ENOMEM;
    if (e == ESP_ERR_TIMEOUT) return MCS_HAL_ETIMEOUT;
    if (e == ESP_ERR_INVALID_ARG) return MCS_HAL_EINVAL;
    if (e == ESP_ERR_INVALID_STATE) return MCS_HAL_EBUSY;
    if (e == ESP_ERR_NOT_SUPPORTED) return MCS_HAL_ENOTSUP;
    if (e == ESP_ERR_NOT_FOUND) return MCS_HAL_ENODEV;
    return MCS_HAL_ERR;
}
#endif
