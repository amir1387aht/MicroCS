#include <stdbool.h>
#define GPIO_FUNC_SIO 5
void gpio_pull_up(unsigned pin);
void gpio_set_dir(unsigned pin,bool out);
void gpio_set_function(unsigned pin,int function);
