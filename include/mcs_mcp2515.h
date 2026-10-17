/* Portable classic-CAN controller over SPI. No external SDK/library required.
 * Caller serializes access, configures SPI mode 0, MSB-first, 8 bits, <=1 MHz
 * (safe even with an 8 MHz oscillator) and leaves other chip selects high.
 * try_send/try_recv never wait; ETIMEOUT means full / empty. */
#ifndef MCS_MCP2515_H
#define MCS_MCP2515_H
#include "mcs_hal.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { const mcs_hal_t* hal; int16_t spi_bus, cs; uint8_t opened, mode; } mcs_mcp2515_t;
int mcs_mcp2515_open(mcs_mcp2515_t* dev, const mcs_hal_t* hal, int spi_bus, int cs,
                    uint32_t oscillator_hz, uint32_t bitrate, int mode);
int mcs_mcp2515_close(mcs_mcp2515_t* dev);
int mcs_mcp2515_try_send(mcs_mcp2515_t* dev, const mcs_can_frame_t* frame);
int mcs_mcp2515_try_recv(mcs_mcp2515_t* dev, mcs_can_frame_t* frame);
#ifdef __cplusplus
}
#endif
#endif
