/*
 * MicroCS port for Raspberry Pi RP2040 / RP2350 (Pico, Pico W, Pico 2, ...)
 * with the official pico-sdk (1.5 or 2.x).
 *
 *     #include "mcs_port_rp2.h"
 *     static mcs_hal_t hal;
 *     mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;   // Pico pin-out defaults below
 *     mcs_rp2_hal_init(&hal, &pins);
 *
 * C# numbering: pins are GPIO numbers ("GP15" works too), PWM channels are
 * GPIO numbers (every pin can do PWM), ADC.Read(0..3) = GP26..GP29 (RP2350B:
 * GP40..47), ADC.Read(4) / (8) = the internal temperature sensor.
 * UART 0/1, I2C 0/1 and SPI 0/1 use the pins in mcs_rp2_cfg_t.
 * Not on this chip: DAC, CAN, QSPI bus for user devices, I2S (needs PIO).
 */
#ifndef MCS_PORT_RP2_H
#define MCS_PORT_RP2_H
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Port options: set them in mcs_user_config.h (template: config/mcs_user_config.h) or with -D. */
#ifndef MCS_RP2_UART_RXBUF
#define MCS_RP2_UART_RXBUF 256     /* interrupt-driven receive ring (power of two) */
#endif
#ifndef MCS_RP2_TIMERS
#define MCS_RP2_TIMERS 4
#endif

typedef struct { int tx, rx; } mcs_rp2_uart_pins_t;
typedef struct { int sda, scl; } mcs_rp2_i2c_pins_t;
typedef struct { int sck, mosi, miso; } mcs_rp2_spi_pins_t;
typedef struct {
    const char* name;                 /* Hal.Board; NULL = "RP2040" / "RP2350" */
    mcs_rp2_uart_pins_t uart[2];      /* -1 = not used */
    mcs_rp2_i2c_pins_t i2c[2];
    mcs_rp2_spi_pins_t spi[2];
} mcs_rp2_cfg_t;

/* Raspberry Pi Pico defaults: UART0 GP0/GP1, UART1 GP4/GP5, I2C0 GP8/GP9 (SDA/SCL),
 * I2C1 GP6/GP7, SPI0 GP18/GP19/GP16 (SCK/MOSI/MISO), SPI1 GP10/GP11/GP12 */
#define MCS_RP2_CFG_DEFAULT { NULL, { { 0, 1 }, { 4, 5 } }, { { 8, 9 }, { 6, 7 } }, \
                              { { 18, 19, 16 }, { 10, 11, 12 } } }

void mcs_rp2_hal_init(mcs_hal_t* hal, const mcs_rp2_cfg_t* cfg);

/* Console over pico_stdio (USB CDC or UART, whatever stdio_init_all() enabled). */
mcs_transport_t mcs_rp2_console_stdio(void);
uint32_t mcs_rp2_ticks(void* ud);
void mcs_rp2_delay(void* ud, uint32_t ms);

#if MCS_ENABLE_FLASH
/* The on-board QSPI flash as a mcs_flash_t (4 KB erase blocks), for
 * mcs_flashfs_mount() = LittleFS or YAFFS2 for scripts. offset/size = the
 * region (bytes from the start of flash, 4 KB aligned); 0, 0 = the default:
 * the last MCS_RP2_FS_SIZE bytes (Pico: 1 MB of 2 MB, Pico 2: 3 MB of 4 MB).
 * Fails (MCS_FLASH_EINVAL) if the region overlaps the firmware. Erase/program
 * run through flash_safe_execute (interrupts and the other core paused). */
#include "mcs_flash.h"
#ifndef MCS_RP2_FS_SIZE
#  if defined(PICO_FLASH_SIZE_BYTES) && PICO_FLASH_SIZE_BYTES >= (4u << 20)
#    define MCS_RP2_FS_SIZE (PICO_FLASH_SIZE_BYTES - (1u << 20))
#  elif defined(PICO_FLASH_SIZE_BYTES)
#    define MCS_RP2_FS_SIZE (PICO_FLASH_SIZE_BYTES / 2)
#  else
#    define MCS_RP2_FS_SIZE (1u << 20)
#  endif
#endif
typedef struct { mcs_flash_t flash; uint32_t offset; uint32_t size; } mcs_rp2_flash_t;
int mcs_rp2_flash_init(mcs_rp2_flash_t* f, uint32_t offset, uint32_t size);
#endif

#ifdef __cplusplus
}
#endif
#endif
