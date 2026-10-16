/* Shared ESP-IDF / Arduino-ESP32 adapter. Owns SDK wake-source settings. */
#ifndef MCS_ESP_POWER_H
#define MCS_ESP_POWER_H
#include "mcs_hal.h"
#include "soc/soc_caps.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#if SOC_WIFI_SUPPORTED && (defined(ARDUINO) || MCS_ESP32_WIFI)
#include "esp_wifi.h"
#endif
#if defined(CONFIG_BT_CONTROLLER_ENABLED) && CONFIG_BT_CONTROLLER_ENABLED && (defined(ARDUINO) || MCS_ENABLE_BLE)
#include "esp_bt.h"
#endif
static int mcs_esp_power_rc(esp_err_t rc) {
    if (rc == ESP_OK) return 0;
    if (rc == ESP_ERR_INVALID_ARG) return MCS_HAL_EINVAL;
    if (rc == ESP_ERR_NOT_SUPPORTED) return MCS_HAL_ENOTSUP;
    if (rc == ESP_ERR_INVALID_STATE) return MCS_HAL_EBUSY;
    return MCS_HAL_ERR;
}
static int mcs_esp_power_validate(void* ctx, int mode, uint32_t ms, int pin, int high) {
    (void)ctx; (void)ms; (void)high;
    if (mode == MCS_POWER_SHUTDOWN) return MCS_HAL_ENOTSUP;
    if (pin < -1 || (pin >= 0 && !GPIO_IS_VALID_GPIO(pin))) return MCS_HAL_EINVAL;
    if (mode == MCS_POWER_DEEP && pin >= 0) {
#if SOC_PM_SUPPORT_EXT0_WAKEUP || SOC_PM_SUPPORT_EXT1_WAKEUP || SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
        if (pin >= 64 || !esp_sleep_is_valid_wakeup_gpio((gpio_num_t)pin)) return MCS_HAL_EINVAL;
#else
        return MCS_HAL_ENOTSUP;
#endif
    }
    return 0;
}
static int mcs_esp_power_enter(void* ctx, int mode, uint32_t ms, int pin, int high) {
    int rc = mcs_esp_power_validate(ctx, mode, ms, pin, high);
    if (rc < 0) return rc;
    /* Explicit shutdown of live radios, never silently lose a connection. */
#if SOC_WIFI_SUPPORTED && (defined(ARDUINO) || MCS_ESP32_WIFI)
    wifi_mode_t wifi_mode;
    esp_err_t w = esp_wifi_get_mode(&wifi_mode);
    if (w != ESP_ERR_WIFI_NOT_INIT && (w != ESP_OK || wifi_mode != WIFI_MODE_NULL)) return MCS_HAL_EBUSY;
#endif
#if defined(CONFIG_BT_CONTROLLER_ENABLED) && CONFIG_BT_CONTROLLER_ENABLED && (defined(ARDUINO) || MCS_ENABLE_BLE)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) return MCS_HAL_EBUSY;
#endif
    esp_err_t e = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (e != ESP_OK) return mcs_esp_power_rc(e);
    /* ALL clears SDK trigger bits, not previously armed GPIO pad registers. */
    for (int p = 0; p < GPIO_NUM_MAX; p++) {
        if (!GPIO_IS_VALID_GPIO(p)) continue;
        if (mode == MCS_POWER_LIGHT) gpio_wakeup_disable((gpio_num_t)p);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
        if (mode == MCS_POWER_DEEP && esp_sleep_is_valid_wakeup_gpio((gpio_num_t)p))
            gpio_deep_sleep_wakeup_disable((gpio_num_t)p);
#endif
    }
    if (ms) e = esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000u);
    if (e == ESP_OK && pin >= 0) {
        if (mode == MCS_POWER_LIGHT) {
            e = gpio_wakeup_enable((gpio_num_t)pin, high ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
            if (e == ESP_OK) e = esp_sleep_enable_gpio_wakeup();
        } else {
#if SOC_PM_SUPPORT_EXT0_WAKEUP
            e = esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, high);
#elif SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
            e = esp_deep_sleep_enable_gpio_wakeup(UINT64_C(1) << pin,
                high ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#elif SOC_PM_SUPPORT_EXT1_WAKEUP
#if CONFIG_IDF_TARGET_ESP32
            e = esp_sleep_enable_ext1_wakeup(UINT64_C(1) << pin,
                high ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ALL_LOW);
#else
            e = esp_sleep_enable_ext1_wakeup(UINT64_C(1) << pin,
                high ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#endif
#else
            e = ESP_ERR_NOT_SUPPORTED;
#endif
        }
    }
    if (e == ESP_OK) {
        if (mode == MCS_POWER_DEEP) esp_deep_sleep_start();
        else e = esp_light_sleep_start();
    }
    if (pin >= 0 && mode == MCS_POWER_LIGHT) gpio_wakeup_disable((gpio_num_t)pin);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
    if (pin >= 0 && mode == MCS_POWER_DEEP) gpio_deep_sleep_wakeup_disable((gpio_num_t)pin);
#endif
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    return mcs_esp_power_rc(e);
}
static void mcs_esp_power_init(mcs_hal_t* h) {
    h->power_modes = h->power_timer_modes = h->power_pin_modes = 0;
#if SOC_LIGHT_SLEEP_SUPPORTED
    h->power_modes |= MCS_POWER_LIGHT;
    h->power_timer_modes |= MCS_POWER_LIGHT;
    h->power_pin_modes |= MCS_POWER_LIGHT;
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
    h->power_modes |= MCS_POWER_DEEP;
    h->power_timer_modes |= MCS_POWER_DEEP;
#if SOC_PM_SUPPORT_EXT0_WAKEUP || SOC_PM_SUPPORT_EXT1_WAKEUP || SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
    h->power_pin_modes |= MCS_POWER_DEEP;
#endif
#endif
    h->power_validate = mcs_esp_power_validate;
    h->power_enter = mcs_esp_power_enter;
}
#endif
