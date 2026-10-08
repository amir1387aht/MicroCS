/* MicroCS - WS2812 / SK6812 ("NeoPixel") drivers shared by the native ports
 * and the Arduino port (hal->ledstrip_write). Include once, from one .c/.cpp
 * file, after defining which one you want:
 *   MCS_LEDSTRIP_RP2_PIO    RP2040 / RP2350 (pico-sdk or Arduino-Pico): one PIO state machine per pin
 *   MCS_LEDSTRIP_ESP32_RMT  ESP32 family with RMT (ESP-IDF >= 5.0 or Arduino-ESP32 3.x)
 * Each sends n bytes MSB first at 800 kHz (T0H 0.4 us, T1H 0.8 us) and
 * returns after the >= 280 us latch gap. 0 or a negative MCS_HAL_E* code. */
#ifndef MCS_LEDSTRIP_DRIVERS_H
#define MCS_LEDSTRIP_DRIVERS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mcs_hal.h"

#if defined(MCS_LEDSTRIP_RP2_PIO)
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
/* the ws2812 program of pico-examples with T1 = 3, T2 = 4, T3 = 3: 10 PIO
 * cycles per bit at 8 MHz -> 0 = 375 ns high, 1 = 875 ns high, 1.25 us per bit */
static const uint16_t mcs_ws2812_insns[] = {
    0x6221, /* 0: out x, 1        side 0 [2] */
    0x1223, /* 1: jmp !x, 3       side 1 [2] */
    0x1300, /* 2: jmp 0           side 1 [3] */
    0xa342, /* 3: nop             side 0 [3] */
};
static struct pio_program mcs_ws2812_prog;      /* filled at first use (field set differs per SDK) */
#ifndef MCS_RP2_LEDSTRIPS
#define MCS_RP2_LEDSTRIPS 4                 /* strips on different pins */
#endif
static struct { int pin; PIO pio; int sm; } mcs_ws_strips[MCS_RP2_LEDSTRIPS];
static int mcs_ws_count;
static int mcs_ws_offset[2] = { -1, -1 };   /* program offset in pio0 / pio1 */
static int mcs_ws_sm(int pin, PIO* pio_out) {
    for (int i = 0; i < mcs_ws_count; i++)
        if (mcs_ws_strips[i].pin == pin) { *pio_out = mcs_ws_strips[i].pio; return mcs_ws_strips[i].sm; }
    if (mcs_ws_count >= MCS_RP2_LEDSTRIPS) return -1;
    if (!mcs_ws2812_prog.instructions) {
        mcs_ws2812_prog.instructions = mcs_ws2812_insns;
        mcs_ws2812_prog.length = 4;
        mcs_ws2812_prog.origin = -1;
    }
    for (int k = 1; k >= 0; k--) {          /* pio1 first: other code usually takes pio0 */
        PIO pio = k ? pio1 : pio0;
        int sm = pio_claim_unused_sm(pio, false);
        if (sm < 0) continue;
        if (mcs_ws_offset[k] < 0) {
            if (!pio_can_add_program(pio, &mcs_ws2812_prog)) { pio_sm_unclaim(pio, (uint)sm); continue; }
            mcs_ws_offset[k] = (int)pio_add_program(pio, &mcs_ws2812_prog);
        }
        uint off = (uint)mcs_ws_offset[k];
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, off, off + 3);
        sm_config_set_sideset(&c, 1, false, false);
        sm_config_set_sideset_pins(&c, (uint)pin);
        sm_config_set_out_shift(&c, false, true, 8);        /* MSB first, autopull every byte */
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (800000.0f * 10.0f));
        pio_gpio_init(pio, (uint)pin);
        pio_sm_set_consecutive_pindirs(pio, (uint)sm, (uint)pin, 1, true);
        pio_sm_init(pio, (uint)sm, off, &c);
        pio_sm_set_enabled(pio, (uint)sm, true);
        mcs_ws_strips[mcs_ws_count].pin = pin; mcs_ws_strips[mcs_ws_count].pio = pio; mcs_ws_strips[mcs_ws_count].sm = sm;
        mcs_ws_count++;
        *pio_out = pio;
        return sm;
    }
    return -1;
}
static int mcs_ledstrip_pio_write(int pin, const uint8_t* data, size_t n) {
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    PIO pio;
    int sm = mcs_ws_sm(pin, &pio);
    if (sm < 0) return MCS_HAL_EBUSY;                      /* no free PIO state machine */
    pio_gpio_init(pio, (uint)pin);                         /* take the pin back after GPIO.* use */
    for (size_t i = 0; i < n; i++) pio_sm_put_blocking(pio, (uint)sm, (uint32_t)data[i] << 24);
    while (!pio_sm_is_tx_fifo_empty(pio, (uint)sm)) tight_loop_contents();
    busy_wait_us(350);                                     /* last byte leaves the OSR, then the latch */
    return 0;
}
#endif /* MCS_LEDSTRIP_RP2_PIO */

#if defined(MCS_LEDSTRIP_ESP32_RMT)
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "soc/soc_caps.h"
/* one RMT TX channel + bytes encoder per pin, 10 MHz ticks:
 * 0 = 0.4 us high + 0.9 us low, 1 = 0.8 us high + 0.5 us low */
#ifndef MCS_ESP32_LEDSTRIPS
#define MCS_ESP32_LEDSTRIPS 2
#endif
static struct { int pin; rmt_channel_handle_t ch; rmt_encoder_handle_t enc; } mcs_rmt_strips[MCS_ESP32_LEDSTRIPS];
static int mcs_rmt_count;
static int mcs_ledstrip_rmt_write(int pin, const uint8_t* data, size_t n) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return MCS_HAL_EINVAL;
    int k = 0;
    while (k < mcs_rmt_count && mcs_rmt_strips[k].pin != pin) k++;
    if (k == mcs_rmt_count) {
        if (mcs_rmt_count >= MCS_ESP32_LEDSTRIPS) return MCS_HAL_EBUSY;
        rmt_tx_channel_config_t tc;
        memset(&tc, 0, sizeof tc);
        tc.gpio_num = (gpio_num_t)pin;
        tc.clk_src = RMT_CLK_SRC_DEFAULT;
        tc.resolution_hz = 10000000;
        tc.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
        tc.trans_queue_depth = 2;
        rmt_bytes_encoder_config_t bc;
        memset(&bc, 0, sizeof bc);
        bc.bit0.duration0 = 4; bc.bit0.level0 = 1; bc.bit0.duration1 = 9; bc.bit0.level1 = 0;
        bc.bit1.duration0 = 8; bc.bit1.level0 = 1; bc.bit1.duration1 = 5; bc.bit1.level1 = 0;
        bc.flags.msb_first = 1;
        rmt_channel_handle_t ch = NULL;
        rmt_encoder_handle_t enc = NULL;
        if (rmt_new_tx_channel(&tc, &ch) != ESP_OK) return MCS_HAL_EBUSY;   /* no free RMT channel */
        if (rmt_new_bytes_encoder(&bc, &enc) != ESP_OK || rmt_enable(ch) != ESP_OK) {
            if (enc) rmt_del_encoder(enc);
            rmt_del_channel(ch);
            return MCS_HAL_ERR;
        }
        mcs_rmt_strips[k].pin = pin; mcs_rmt_strips[k].ch = ch; mcs_rmt_strips[k].enc = enc;
        mcs_rmt_count++;
    }
    rmt_transmit_config_t xc;
    memset(&xc, 0, sizeof xc);
    if (rmt_transmit(mcs_rmt_strips[k].ch, mcs_rmt_strips[k].enc, data, n, &xc) != ESP_OK) return MCS_HAL_ERR;
    if (rmt_tx_wait_all_done(mcs_rmt_strips[k].ch, 1000) != ESP_OK) return MCS_HAL_ETIMEOUT;
    esp_rom_delay_us(300);                                 /* latch */
    return 0;
}
#endif /* MCS_LEDSTRIP_ESP32_RMT */
#endif
