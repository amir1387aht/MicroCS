/* Test the enum layouts in IDF 5.3 / 5.5 without linking a radio controller. */
#include <assert.h>
#if CONFIG_IDF_TARGET_ESP32
enum { ESP_PWR_LVL_N12 = 0, ESP_PWR_LVL_N0 = 4, ESP_PWR_LVL_P9 = 7 };
#elif CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
enum { ESP_PWR_LVL_N15 = 3, ESP_PWR_LVL_N0 = 8, ESP_PWR_LVL_P20 = 15 };
#else
enum { ESP_PWR_LVL_N24 = 0, ESP_PWR_LVL_N15 = 3, ESP_PWR_LVL_N0 = 8, ESP_PWR_LVL_P20 = 15 };
#endif
#include "mcs_esp32_ble_power.h"
int main(void) {
    assert(mcs_esp32_ble_tx_level(0) == ESP_PWR_LVL_N0);
    assert(mcs_esp32_ble_tx_dbm(ESP_PWR_LVL_N0) == 0);
    assert(mcs_esp32_ble_tx_level(-40) == mcs_esp32_ble_tx_min());
    assert(mcs_esp32_ble_tx_level(40) == mcs_esp32_ble_tx_max());
#if CONFIG_IDF_TARGET_ESP32
    assert(mcs_esp32_ble_tx_dbm(mcs_esp32_ble_tx_max()) == 9);
    assert(mcs_esp32_ble_tx_dbm(mcs_esp32_ble_tx_min()) == -12);
#elif CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
    assert(mcs_esp32_ble_tx_dbm(mcs_esp32_ble_tx_min()) == -15);
    assert(mcs_esp32_ble_tx_dbm(mcs_esp32_ble_tx_max()) == 20);
#else
    assert(mcs_esp32_ble_tx_dbm(mcs_esp32_ble_tx_min()) == -24);
    assert(mcs_esp32_ble_tx_dbm(mcs_esp32_ble_tx_max()) == 20);
#endif
    for (int dbm = -40; dbm <= 40; dbm++) {
        int level = mcs_esp32_ble_tx_level(dbm);
        assert(level >= mcs_esp32_ble_tx_min() && level <= mcs_esp32_ble_tx_max());
        assert(mcs_esp32_ble_tx_level(mcs_esp32_ble_tx_dbm(level)) == level);
    }
    return 0;
}
