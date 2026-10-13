/* ESP-IDF's esp_bt.h must be included first. Keep dBm conversion independent
 * of the controller: ESP32 has a different enum origin; C6 / C61 start at -15 dBm;
 * newer controllers' top index is +20 dBm, not the extrapolated +21 dBm. */
#ifndef MCS_ESP32_BLE_POWER_H
#define MCS_ESP32_BLE_POWER_H
static inline int mcs_esp32_ble_tx_min(void) {
#if CONFIG_IDF_TARGET_ESP32
    return ESP_PWR_LVL_N12;
#elif CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
    return ESP_PWR_LVL_N15;
#else
    return ESP_PWR_LVL_N24;
#endif
}
static inline int mcs_esp32_ble_tx_max(void) {
#if CONFIG_IDF_TARGET_ESP32
    return ESP_PWR_LVL_P9;
#else
    return ESP_PWR_LVL_P20;
#endif
}
static inline int mcs_esp32_ble_tx_level(int dbm) {
    int level = ESP_PWR_LVL_N0 + (dbm >= 0 ? dbm + 1 : dbm - 1) / 3;
    int lo = mcs_esp32_ble_tx_min(), hi = mcs_esp32_ble_tx_max();
    return level < lo ? lo : level > hi ? hi : level;
}
static inline int mcs_esp32_ble_tx_dbm(int level) {
#if !CONFIG_IDF_TARGET_ESP32
    if (level == ESP_PWR_LVL_P20) return 20;
#endif
    return (level - ESP_PWR_LVL_N0) * 3;
}
#endif
