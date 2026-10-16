#ifndef TEST_POWER_SDK_H
#define TEST_POWER_SDK_H
#include <stdint.h>
#define SOC_WIFI_SUPPORTED 1
#define SOC_LIGHT_SLEEP_SUPPORTED 1
#define SOC_DEEP_SLEEP_SUPPORTED 1
#define SOC_PM_SUPPORT_EXT0_WAKEUP (POWER_VARIANT == 1)
#define SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP (POWER_VARIANT == 2)
#define SOC_PM_SUPPORT_EXT1_WAKEUP (POWER_VARIANT == 3)
#define GPIO_NUM_MAX 32
#define GPIO_IS_VALID_GPIO(p) ((unsigned)(p) < GPIO_NUM_MAX)
#define CONFIG_BT_CONTROLLER_ENABLED 1
#define CONFIG_IDF_TARGET_ESP32 0
typedef int esp_err_t;
typedef int gpio_num_t;
typedef int gpio_int_type_t;
typedef int wifi_mode_t;
typedef int esp_sleep_ext1_wakeup_mode_t;
typedef int esp_deepsleep_gpio_wake_up_mode_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_STATE, ESP_ERR_WIFI_NOT_INIT };
enum { WIFI_MODE_NULL, WIFI_MODE_STA };
enum { ESP_SLEEP_WAKEUP_ALL, GPIO_INTR_HIGH_LEVEL, GPIO_INTR_LOW_LEVEL, ESP_EXT1_WAKEUP_ANY_HIGH,
       ESP_EXT1_WAKEUP_ANY_LOW, ESP_GPIO_WAKEUP_GPIO_HIGH, ESP_GPIO_WAKEUP_GPIO_LOW,
       ESP_BT_CONTROLLER_STATUS_ENABLED };
int esp_wifi_get_mode(wifi_mode_t* mode);
int esp_bt_controller_get_status(void);
int esp_sleep_is_valid_wakeup_gpio(gpio_num_t pin);
int esp_sleep_disable_wakeup_source(int source);
int esp_sleep_enable_timer_wakeup(uint64_t us);
int gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t level);
int gpio_wakeup_disable(gpio_num_t pin);
int gpio_deep_sleep_wakeup_disable(gpio_num_t pin);
int esp_sleep_enable_gpio_wakeup(void);
int esp_sleep_enable_ext0_wakeup(gpio_num_t pin, int level);
int esp_deep_sleep_enable_gpio_wakeup(uint64_t pins, esp_deepsleep_gpio_wake_up_mode_t level);
int esp_sleep_enable_ext1_wakeup(uint64_t pins, esp_sleep_ext1_wakeup_mode_t level);
int esp_light_sleep_start(void);
void esp_deep_sleep_start(void);
#endif
