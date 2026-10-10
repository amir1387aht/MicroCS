/*
 * MicroCS port for Espressif chips with ESP-IDF 5.x:
 * ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-H2, ESP32-P4.
 * (Wi-Fi / Bluetooth classes are not part of this port yet.)
 *
 * Use it as an ESP-IDF component: clone the repository into components/MicroCS
 * of your project (the repository root has the CMakeLists.txt), then
 *
 *     #include "mcs_port_esp32.h"
 *     static mcs_hal_t hal;
 *     mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;    // every pin -1 = unused
 *     pins.uart[1] = (mcs_esp32_uart_pins_t){ 17, 18, -1, -1 };       // TX, RX, RTS, CTS
 *     pins.i2c[0]  = (mcs_esp32_i2c_pins_t){ 8, 9 };                   // SDA, SCL
 *     pins.spi[0]  = (mcs_esp32_spi_pins_t){ 12, 11, 13, -1, -1 };    // SCLK, MOSI, MISO, WP, HD
 *     pins.pwm[0] = 2;                                                  // PWM channel 0 on GPIO2
 *     mcs_esp32_hal_init(&hal, &pins);
 *
 * C# pin numbers are GPIO numbers ("GPIO5", "IO5" and "5" all work).
 * ADC.Read(n) takes the GPIO number of an ADC-capable pin, and ReadMillivolts()
 * uses the chip's eFuse calibration. DAC exists on ESP32 / ESP32-S2 only;
 * I2S and TWAI (CAN) are compiled only where the chip has them (not on the
 * ESP32-C2, which has 2 UARTs, 1 I2C bus, 1 SPI bus, 6 LEDC channels and one
 * gptimer).
 * Interrupts go through a FreeRTOS queue, so they are safe on dual-core chips.
 */
#ifndef MCS_PORT_ESP32_H
#define MCS_PORT_ESP32_H
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Port options: set them in mcs_user_config.h (template: config/mcs_user_config.h) or with -D. */
#ifndef MCS_ESP32_UARTS
#define MCS_ESP32_UARTS 3
#endif
#ifndef MCS_ESP32_I2C_BUSES
#define MCS_ESP32_I2C_BUSES 2
#endif
#ifndef MCS_ESP32_SPI_BUSES
#define MCS_ESP32_SPI_BUSES 2       /* C# bus 0 = SPI2_HOST, 1 = SPI3_HOST (if the chip has it) */
#endif
#ifndef MCS_ESP32_PWM_CHANNELS
#define MCS_ESP32_PWM_CHANNELS 8    /* LEDC channels; 0-3 have their own timer, 4-7 share 0-3 */
#endif
#ifndef MCS_ESP32_TIMERS
#define MCS_ESP32_TIMERS 4          /* gptimer instances for Timer.Start (capped at the chip's
                                       count: 2 on C3/C6/H2, 1 on C2) */
#endif
#ifndef MCS_ESP32_UART_RXBUF
#define MCS_ESP32_UART_RXBUF 1024
#endif
#ifndef MCS_ESP32_EVENT_QUEUE
#define MCS_ESP32_EVENT_QUEUE 32
#endif

typedef struct { int tx, rx, rts, cts; } mcs_esp32_uart_pins_t;
typedef struct { int sda, scl; } mcs_esp32_i2c_pins_t;
typedef struct { int sclk, mosi, miso, quadwp, quadhd; } mcs_esp32_spi_pins_t;
typedef struct { int mclk, bclk, ws, dout, din; } mcs_esp32_i2s_pins_t;
typedef struct { int tx, rx; } mcs_esp32_can_pins_t;

typedef struct {
    const char* name;                                   /* Hal.Board; NULL = chip name */
    mcs_esp32_uart_pins_t uart[MCS_ESP32_UARTS];        /* -1 = keep the default pin */
    mcs_esp32_i2c_pins_t i2c[MCS_ESP32_I2C_BUSES];
    mcs_esp32_spi_pins_t spi[MCS_ESP32_SPI_BUSES];      /* also used by QSPI.Open(bus) */
    int pwm[MCS_ESP32_PWM_CHANNELS];                    /* GPIO per LEDC channel */
    mcs_esp32_i2s_pins_t i2s[2];
    mcs_esp32_can_pins_t can;                           /* TWAI */
    int qspi_cs[MCS_ESP32_SPI_BUSES];                   /* chip select for QSPI.Open(bus) */
    int led;                                            /* GPIO for the pin name "LED"; -1 = none */
} mcs_esp32_cfg_t;

#define MCS_ESP32_PINS4 { -1, -1, -1, -1 }
#define MCS_ESP32_CFG_DEFAULT { NULL, \
    { MCS_ESP32_PINS4, MCS_ESP32_PINS4, MCS_ESP32_PINS4 }, \
    { { -1, -1 }, { -1, -1 } }, \
    { { -1, -1, -1, -1, -1 }, { -1, -1, -1, -1, -1 } }, \
    { -1, -1, -1, -1, -1, -1, -1, -1 }, \
    { { -1, -1, -1, -1, -1 }, { -1, -1, -1, -1, -1 } }, \
    { -1, -1 }, { -1, -1 }, -1 }

/* Fill *hal; cfg is copied. */
void mcs_esp32_hal_init(mcs_hal_t* hal, const mcs_esp32_cfg_t* cfg);

/* Console for the REPL / shell / mcs_runtime. */
mcs_transport_t mcs_esp32_console_uart(int port, uint32_t baud);  /* UART0 = the USB-UART bridge on most boards */
mcs_transport_t mcs_esp32_console_usb(void);   /* USB-Serial-JTAG (C3, C6, S3, H2, P4); falls back to UART0 */
mcs_transport_t mcs_esp32_console(void);
/* LittleFS on a flash data partition (default label "storage", see the example's
 * partitions.csv), formatted on first use and mounted in the ESP-IDF VFS at
 * MCS_ESP32_FS_PATH (C code can fopen() the same files). Fills ops/ctx for
 * mcs_runtime_cfg_t.fs_ops/fs_ctx or mcs_vfs_mount(). false = no such partition
 * or no LittleFS component - fall back to the RAM disk. */
#ifndef MCS_ESP32_FS_PATH
#define MCS_ESP32_FS_PATH "/mcs"
#endif
#if MCS_ENABLE_FS
#include <stdbool.h>
#include "mcs_vfs.h"
bool mcs_esp32_littlefs(const char* partition_label, const mcs_vfs_ops_t** ops, void** ctx);
bool mcs_esp32_littlefs_info(const char* partition_label, size_t* total, size_t* used);
#if MCS_ENABLE_FLASH
/* A flash data partition as a raw mcs_flash_t (4 KB blocks, esp_partition API)
 * for mcs_flashfs_mount(): YAFFS2 (menuconfig > MicroCS > Filesystem) or any
 * other filesystem layout. false = no partition with that label. */
#include "mcs_flash.h"
typedef struct { mcs_flash_t flash; const void* part; } mcs_esp32_flash_t;
bool mcs_esp32_partition_flash(mcs_esp32_flash_t* f, const char* partition_label);
#endif
/* The filesystem chosen in menuconfig (LittleFS via esp_littlefs, or YAFFS2)
 * on the partition, formatted on first use. false = no partition / no
 * filesystem compiled in: use a RAM disk. */
bool mcs_esp32_flash_fs(const char* partition_label, const mcs_vfs_ops_t** ops, void** ctx);
#endif       /* both: UART0 + USB-Serial-JTAG (use this if unsure) */
uint32_t mcs_esp32_ticks(void* ud);            /* milliseconds since boot */
void mcs_esp32_delay(void* ud, uint32_t ms);   /* vTaskDelay, at least one tick */

#ifdef __cplusplus
}
#endif
#endif
