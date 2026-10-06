/*
 * MicroCS Cortex-M reference port - board definitions.
 *
 * A minimal bare-metal SoC (UART data/status registers + millisecond tick)
 * used by the CI size/speed checks (tools/cm_emu.py). For a real chip use one
 * of the vendor ports (ports/stm32, ports/rp2, ports/esp32, ports/zephyr,
 * ports/arduino) or replace board.c with your SDK's UART/tick drivers.
 */
#ifndef MCS_CM_BOARD_H
#define MCS_CM_BOARD_H
#include <stdint.h>

#define EMU_BASE      0x40000000u
#define EMU_UART_TX   (*(volatile uint32_t*)(EMU_BASE + 0x00)) /* write: byte out           */
#define EMU_EXIT      (*(volatile uint32_t*)(EMU_BASE + 0x04)) /* write: stop emulation      */
#define EMU_TICKS_MS  (*(volatile uint32_t*)(EMU_BASE + 0x08)) /* read: ms (from instr count)*/
#define EMU_INSNS     (*(volatile uint32_t*)(EMU_BASE + 0x0C)) /* read: executed instructions*/
#define EMU_UART_RXST (*(volatile uint32_t*)(EMU_BASE + 0x10)) /* read: 0 empty, 1 data, 2 closed */
#define EMU_UART_RX   (*(volatile uint32_t*)(EMU_BASE + 0x14)) /* read: next byte            */
#define EMU_MARK      (*(volatile uint32_t*)(EMU_BASE + 0x18)) /* write: phase marker (profiling) */

void board_putc(char c);
void board_write(const char* s, unsigned n);
void board_puts(const char* s);
uint32_t board_ticks_ms(void);
uint32_t board_insns(void);
int board_getc(uint32_t timeout_ms); /* -1 timeout, -2 closed */
void board_exit(int code) __attribute__((noreturn));
uint32_t board_stack_high_water(void);
#endif
