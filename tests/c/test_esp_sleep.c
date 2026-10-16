/* SDK API doubles exercise each wake backend, not physical power behavior. */
#include <assert.h>
#include <stdio.h>
#include "mcs_esp_power.h"
static int wifi, bt, cleared, light, deep, source, selected_pin, selected_level, fail_timer, fail_light;
static uint64_t timer_us;
int esp_wifi_get_mode(wifi_mode_t* mode) { *mode = wifi; return ESP_OK; }
int esp_bt_controller_get_status(void) { return bt; }
int esp_sleep_is_valid_wakeup_gpio(gpio_num_t pin) { return pin >= 0 && pin < 8; }
int esp_sleep_disable_wakeup_source(int s) { assert(s == ESP_SLEEP_WAKEUP_ALL); cleared++; return ESP_OK; }
int esp_sleep_enable_timer_wakeup(uint64_t us) { timer_us = us; return fail_timer ? ESP_ERR_INVALID_ARG : ESP_OK; }
int gpio_wakeup_enable(gpio_num_t p, gpio_int_type_t l) { selected_pin = p; selected_level = l; return ESP_OK; }
int gpio_wakeup_disable(gpio_num_t p) { (void)p; return ESP_OK; }
int gpio_deep_sleep_wakeup_disable(gpio_num_t p) { (void)p; return ESP_OK; }
int esp_sleep_enable_gpio_wakeup(void) { source = 1; return ESP_OK; }
int esp_sleep_enable_ext0_wakeup(gpio_num_t p, int l) { source = 2; selected_pin = p; selected_level = l; return ESP_OK; }
int esp_deep_sleep_enable_gpio_wakeup(uint64_t pins, esp_deepsleep_gpio_wake_up_mode_t l) { source = 3; assert(pins == (UINT64_C(1) << 2)); selected_level = l; return ESP_OK; }
int esp_sleep_enable_ext1_wakeup(uint64_t pins, esp_sleep_ext1_wakeup_mode_t l) { source = 4; assert(pins == (UINT64_C(1) << 2)); selected_level = l; return ESP_OK; }
int esp_light_sleep_start(void) { light++; return fail_light ? ESP_ERR_INVALID_STATE : ESP_OK; }
void esp_deep_sleep_start(void) { deep++; }
int main(void) {
    mcs_hal_t h = {0}; mcs_esp_power_init(&h);
    assert(h.power_modes == (MCS_POWER_LIGHT | MCS_POWER_DEEP));
    assert(!(h.power_modes & MCS_POWER_SHUTDOWN));
    assert(h.power_validate(NULL, 0, 0, 20, 1) == 0); /* light-only pin */
    assert(h.power_validate(NULL, MCS_POWER_LIGHT, 1, -2, 0) == MCS_HAL_EINVAL);
    assert(h.power_validate(NULL, MCS_POWER_DEEP, 1, 20, 1) < 0);
    assert(h.power_enter(NULL, MCS_POWER_SHUTDOWN, 0, -1, 0) == MCS_HAL_ENOTSUP);
    assert(!cleared && !light && !deep);
    wifi = WIFI_MODE_STA;
    assert(h.power_enter(NULL, MCS_POWER_LIGHT, 10, -1, 0) == MCS_HAL_EBUSY);
    wifi = WIFI_MODE_NULL; bt = ESP_BT_CONTROLLER_STATUS_ENABLED;
    assert(h.power_enter(NULL, MCS_POWER_LIGHT, 10, -1, 0) == MCS_HAL_EBUSY);
    assert(!cleared); bt = 0;
    assert(h.power_enter(NULL, MCS_POWER_LIGHT, 2147483647u, 2, 0) == 0);
    assert(timer_us == UINT64_C(2147483647000));
    assert(light == 1 && !deep && source == 1 && selected_pin == 2 && selected_level == GPIO_INTR_LOW_LEVEL && cleared == 2);
    fail_timer = 1;
    assert(h.power_enter(NULL, MCS_POWER_LIGHT, 1, 2, 0) == MCS_HAL_EINVAL);
    assert(light == 1 && cleared == 4); fail_timer = 0;
    fail_light = 1;
    assert(h.power_enter(NULL, MCS_POWER_LIGHT, 1, -1, 0) == MCS_HAL_EBUSY);
    assert(cleared == 6); fail_light = 0;
#if POWER_VARIANT <= 3
    assert(h.power_pin_modes & MCS_POWER_DEEP);
    assert(h.power_enter(NULL, MCS_POWER_DEEP, 0, 2, 1) == 0);
    assert(deep == 1 && light == 2);
    assert(source == POWER_VARIANT + 1);
#if POWER_VARIANT == 1
    assert(selected_level == 1);
#elif POWER_VARIANT == 2
    assert(selected_level == ESP_GPIO_WAKEUP_GPIO_HIGH);
#else
    assert(selected_level == ESP_EXT1_WAKEUP_ANY_HIGH);
#endif
#else
    assert(!(h.power_pin_modes & MCS_POWER_DEEP));
    assert(h.power_enter(NULL, MCS_POWER_DEEP, 1, -1, 0) == 0 && deep == 1);
#endif
    printf("PASS ESP sleep SDK variant %d\n", POWER_VARIANT);
    return 0;
}
