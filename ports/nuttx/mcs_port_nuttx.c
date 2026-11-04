/* NuttX GPIO character devices. The board registers /dev/gpioN and chooses
 * each pin's initial mode; no assumptions are made about board pin muxing. */
#include <nuttx/config.h>
#include "mcs_port_nuttx.h"
#if MCS_ENABLE_HAL && defined(CONFIG_INTERPRETERS_MICROCS_GPIO)
#include <nuttx/ioexpander/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int gpio_error(int e) {
    switch (e) {
    case ENOENT: case ENODEV: return MCS_HAL_ENODEV;
    case EINVAL: return MCS_HAL_EINVAL;
    case ENOTTY: case ENOSYS: case EACCES: return MCS_HAL_ENOTSUP;
    case EBUSY: return MCS_HAL_EBUSY;
    case ENOMEM: return MCS_HAL_ENOMEM;
    default: return MCS_HAL_ERR;
    }
}
static int gpio_open_pin(int pin) {
    char path[32];
    if (pin < 0) return MCS_HAL_EINVAL;
    int n = snprintf(path, sizeof path, "/dev/gpio%d", pin);
    if (n < 0 || (size_t)n >= sizeof path) return MCS_HAL_EINVAL;
    int fd = open(path, O_RDWR);
    return fd < 0 ? gpio_error(errno) : fd;
}
static int gpio_mode(void* ctx, int pin, int mode) {
    (void)ctx;
    enum gpio_pintype_e want;
    switch (mode) {
    case MCS_GPIO_INPUT: want = GPIO_INPUT_PIN; break;
    case MCS_GPIO_INPUT_PULLUP: want = GPIO_INPUT_PIN_PULLUP; break;
    case MCS_GPIO_INPUT_PULLDOWN: want = GPIO_INPUT_PIN_PULLDOWN; break;
    case MCS_GPIO_OUTPUT: want = GPIO_OUTPUT_PIN; break;
    case MCS_GPIO_OPEN_DRAIN: want = GPIO_OUTPUT_PIN_OPENDRAIN; break;
    default: return MCS_HAL_ENOTSUP;
    }
    int fd = gpio_open_pin(pin); if (fd < 0) return fd;
    enum gpio_pintype_e actual;
    int rc = ioctl(fd, GPIOC_PINTYPE, (unsigned long)(uintptr_t)&actual);
    int e = rc < 0 ? gpio_error(errno) : (actual == want ? 0 : MCS_HAL_ENOTSUP);
    close(fd); return e;
}
static int gpio_write(void* ctx, int pin, int value) {
    (void)ctx;
    int fd = gpio_open_pin(pin); if (fd < 0) return fd;
    int rc = ioctl(fd, GPIOC_WRITE, (unsigned long)(value != 0));
    int e = rc < 0 ? gpio_error(errno) : 0;
    close(fd); return e;
}
static int gpio_read(void* ctx, int pin) {
    (void)ctx;
    int fd = gpio_open_pin(pin); if (fd < 0) return fd;
    bool value = false;
    int rc = ioctl(fd, GPIOC_READ, (unsigned long)(uintptr_t)&value);
    int e = rc < 0 ? gpio_error(errno) : (value ? 1 : 0);
    close(fd); return e;
}
void mcs_nuttx_hal_init(mcs_hal_t* hal) {
    memset(hal, 0, sizeof *hal);
    hal->board = "NuttX";
    hal->gpio_mode = gpio_mode; hal->gpio_write = gpio_write; hal->gpio_read = gpio_read;
}
#endif
