/*
 * MicroCS port for Zephyr RTOS (3.4 or newer): every board Zephyr supports -
 * Nordic nRF52/nRF53/nRF54, NXP i.MX RT / Kinetis / LPC, STM32, SAM, RP2040,
 * ESP32, Renesas, Silicon Labs ... - through Zephyr's portable driver APIs.
 *
 * Add MicroCS as a Zephyr module (west.yml or ZEPHYR_EXTRA_MODULES), set
 * CONFIG_MICROCS=y, then
 *
 *     #include "mcs_port_zephyr.h"
 *     static mcs_hal_t hal;
 *     mcs_zephyr_hal_init(&hal, NULL);      // devices from the devicetree, see below
 *
 * Devices are found in the devicetree:
 *   GPIO   every `gpio0`, `gpio1` ... or `gpioa`, `gpiob` ... node label.
 *          C# pin = port * 32 + pin; names: "P0.13" (nRF), "PA5" (STM32), "13"
 *   UART   aliases mcs-uart0 .. mcs-uart3 (UART 0 defaults to zephyr,console)
 *   I2C    aliases mcs-i2c0 .. mcs-i2c1   (fallback: node labels i2c0, i2c1)
 *   SPI    aliases mcs-spi0 .. mcs-spi1   (fallback: spi1, spi2)
 *   ADC    `io-channels` of the /zephyr,user node: ADC.Read(n) = n-th entry
 *   PWM    `pwms` of the /zephyr,user node:        PWM.Set(n, ...) = n-th entry
 *   DAC    alias mcs-dac    CAN  chosen zephyr,canbus    Watchdog  alias watchdog0
 */
#ifndef MCS_PORT_ZEPHYR_H
#define MCS_PORT_ZEPHYR_H
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;           /* Hal.Board; NULL = CONFIG_BOARD */
} mcs_zephyr_cfg_t;

void mcs_zephyr_hal_init(mcs_hal_t* hal, const mcs_zephyr_cfg_t* cfg);

/* Console on the zephyr,console UART (interrupt-driven when CONFIG_UART_INTERRUPT_DRIVEN=y). */
mcs_transport_t mcs_zephyr_console(void);
uint32_t mcs_zephyr_ticks(void* ud);
void mcs_zephyr_delay(void* ud, uint32_t ms);

#ifdef __cplusplus
}
#endif
#endif
