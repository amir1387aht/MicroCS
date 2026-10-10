/*
 * MicroCS - device drivers (optional, MCS_ENABLE_DRIVERS; default = MCS_ENABLE_HAL).
 *
 * A driver adds C# classes for one kind of device (an LED strip, a display, a
 * sensor, a motor controller ...) on top of the board. It has two halves:
 *
 *   front end  chip-independent: the C# API, argument checks, buffers. It is
 *              the `open` function, called for every VM the HAL library is
 *              opened on (mcs_hal_open_lib / mcs_runtime), which registers
 *              the classes with mcs_register_class / mcs_register_module.
 *   backend    the MCU-specific part: a table of C functions (`ops`) plus a
 *              context pointer, chosen by the port or by your firmware.
 *
 * Drivers live in a small global table, filled before the VM starts:
 *
 *   static const my_motor_ops_t ops = { pwm_out, read_encoder };
 *   static const mcs_driver_t motor = { "motor", "Motor", motor_open, &ops, NULL };
 *   mcs_driver_register(&motor);                 // add, or replace the one with that name
 *
 * The ports register their built-in drivers with mcs_driver_register_default()
 * (keeps one you registered first), so a firmware can swap a backend - e.g. its
 * own WS2812 driver on a SPI bus - without editing the port, or remove one
 * with mcs_driver_unregister("ws2812"). C#: Drivers.Has("ws2812"),
 * Drivers.List (names), Hal.Has("LedStrip") (also true for driver class names).
 *
 * Built-in drivers (each with its own MCS_ENABLE_* switch):
 *   servo   C# Servo - hobby servos / ESCs on a PWM channel (MCS_ENABLE_SERVO).
 *           Default backend: the board's HAL PWM at 50 Hz (every port and the
 *           simulator); register your own for a PCA9685 or a servo bus.
 *   ws2812  C# LedStrip - WS2812 / WS2812B / SK6812 / WS2811 strips (MCS_ENABLE_WS2812).
 *           Backends: RP2040/RP2350 PIO, ESP32 RMT, STM32 bit-bang, Zephyr
 *           led_strip, Arduino (RMT / PIO / Adafruit_NeoPixel), simulator.
 * See docs/DRIVERS.md for a complete example of writing your own.
 */
#ifndef MCS_DRIVER_H
#define MCS_DRIVER_H
#include "mcs.h"
#include "mcs_hal.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef MCS_MAX_DRIVERS
#define MCS_MAX_DRIVERS 8          /* registered drivers (built-in + yours) */
#endif

typedef struct mcs_driver mcs_driver_t;
struct mcs_driver {
    const char* name;      /* registry key, lower case: "ws2812" (Drivers.Has / Drivers.List) */
    const char* classes;   /* C# names it adds, space separated: "LedStrip" (Hal.Has) - may be NULL */
    void (*open)(mcs_vm_t* vm, const mcs_driver_t* drv);   /* register the C# API on vm (may be NULL) */
    const void* ops;       /* backend function table - its type is defined by the driver */
    void* ctx;             /* backend context, passed back to the ops */
};

#if MCS_ENABLE_DRIVERS
/* The descriptor must outlive every VM (static / const). Return 0, or
 * MCS_HAL_EBUSY when MCS_MAX_DRIVERS are registered, MCS_HAL_EINVAL for a bad descriptor. */
int mcs_driver_register(const mcs_driver_t* drv);          /* add, or replace the one with the same name */
int mcs_driver_register_default(const mcs_driver_t* drv);  /* add unless that name is registered (ports) */
int mcs_driver_unregister(const char* name);               /* 0, or MCS_HAL_ENODEV */
const mcs_driver_t* mcs_driver_find(const char* name);     /* by name, NULL if absent */
bool mcs_driver_provides(const char* name);                /* a driver name or one of its class names */
int mcs_driver_count(void);
const mcs_driver_t* mcs_driver_at(int i);
/* Register the C# Drivers class and run every driver's open(). Called by
 * mcs_hal_open_lib; call it yourself only when you open drivers on a VM
 * without the HAL library. */
void mcs_drivers_open(mcs_vm_t* vm);
#endif

/* ------------------------------------------------------------ ws2812 driver
 * C# LedStrip: new LedStrip(pin, count[, LedStrip.GRB | RGB | GRBW]).
 * The front end keeps 0xWWRRGGBB pixels, applies Brightness and hands the
 * backend the frame already in wire order. A backend implements one function:
 *
 *   write(ctx, pin, data, n, order): send n bytes MSB first on `pin`
 *       (T0H 0.4 us, T1H 0.8 us, 1.25 us per bit), then keep the line low
 *       >= 280 us so the strip latches. `order` (MCS_LED_GRB / _RGB / _GRBW)
 *       tells drivers that need it (Zephyr's led_strip API) the layout.
 *       Return 0 or a negative MCS_HAL_E* code.
 *
 *   static int my_write(void* ctx, int pin, const uint8_t* d, size_t n, int order) { ... }
 *   static const mcs_ws2812_ops_t my_ops = { my_write };
 *   static const mcs_driver_t my_ws2812 = MCS_WS2812_DRIVER(&my_ops, NULL);
 *   mcs_driver_register(&my_ws2812);
 */
enum { MCS_LED_GRB = 0, MCS_LED_RGB = 1, MCS_LED_GRBW = 2 };
typedef struct {
    int (*write)(void* ctx, int pin, const uint8_t* data, size_t n, int order);
} mcs_ws2812_ops_t;
#if MCS_ENABLE_WS2812
void mcs_ws2812_open(mcs_vm_t* vm, const mcs_driver_t* drv);
#define MCS_WS2812_DRIVER(ops, ctx) { "ws2812", "LedStrip", mcs_ws2812_open, (ops), (ctx) }
#endif

/* ------------------------------------------------------------ servo driver
 * C# Servo: new Servo(channel[, minUs = 500, maxUs = 2500[, maxAngle = 180]]),
 * s.Angle / s.Write(deg), s.Pulse / s.WritePulse(us), s.MoveTo(deg, ms), s.Detach().
 * The front end converts angles to pulse widths and checks every argument; a
 * backend implements two functions (channel = the number given to `new Servo`):
 *
 *   set_pulse(ctx, channel, pulse_us, period_us): output a pulse of pulse_us
 *       microseconds every period_us (20000 = 50 Hz) until told otherwise.
 *       Return 0 or a negative MCS_HAL_E* code.
 *   stop(ctx, channel): stop the signal (the servo goes limp). May be NULL.
 *
 *   static int my_pulse(void* ctx, int ch, uint32_t us, uint32_t period) { return pca9685_set(ctx, ch, us, period); }
 *   static const mcs_servo_ops_t my_ops = { my_pulse, NULL };
 *   static const mcs_driver_t my_servo = MCS_SERVO_DRIVER(&my_ops, &pca);
 *   mcs_driver_register(&my_servo);                 // replaces the default HAL-PWM backend
 */
typedef struct {
    int (*set_pulse)(void* ctx, int channel, uint32_t pulse_us, uint32_t period_us);
    int (*stop)(void* ctx, int channel);
} mcs_servo_ops_t;
#if MCS_ENABLE_SERVO
void mcs_servo_open(mcs_vm_t* vm, const mcs_driver_t* drv);
#define MCS_SERVO_DRIVER(ops, ctx) { "servo", "Servo", mcs_servo_open, (ops), (ctx) }
/* Register the default backend (HAL PWM of `hal`) unless a "servo" driver exists.
 * Called by mcs_hal_open_lib, so every port and the simulator get it. */
void mcs_servo_use_hal(const mcs_hal_t* hal);
#endif

#ifdef __cplusplus
}
#endif
#endif
