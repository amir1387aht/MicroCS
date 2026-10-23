#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_IS_VALID_GPIO(pin) ((pin)>=0 && (pin)<40)
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ONLY 1
esp_err_t gpio_set_direction(gpio_num_t pin,int mode);
esp_err_t gpio_set_pull_mode(gpio_num_t pin,int pull);
