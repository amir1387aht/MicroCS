/* RP2040 / RP2350 PIO hardware counting, no CPU interrupt per edge or rollover.
 * PIO instructions derived from Raspberry Pi pico-examples quadrature_encoder:
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd. SPDX-License-Identifier: BSD-3-Clause
 * See ports/rp2/LICENSE.encoder and encoder.pio for the original program source. */
#ifndef MCS_RP2_ENCODER_H
#define MCS_RP2_ENCODER_H
#include "mcs_hal.h"
#if MCS_ENABLE_ENCODER && MCS_ENABLE_ENCODER_HW
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/version.h"
#ifndef NUM_PIOS
#define MCS_RP2_ENCODER_BANKS 2
#else
#define MCS_RP2_ENCODER_BANKS NUM_PIOS
#endif
#include <string.h>
#ifndef MCS_RP2_ENCODER_INVERT
#define MCS_RP2_ENCODER_INVERT(pin) 0
#endif
static const uint16_t mcs_rp2_encoder_code[] = {
    0x000f,0x000e,0x0015,0x000f, 0x0015,0x000f,0x000f,0x000e,
    0x000e,0x000f,0x000f,0x0015, 0x000f,0x0015,0x008f,0xa0c2,
    0x8000,0x60c2,0x4002,0xa0e6, 0xa0a6,0xa04a,0x0097,0xa04a
};
static const struct pio_program mcs_rp2_encoder_program = {
    .instructions = mcs_rp2_encoder_code, .length = 24, .origin = 0,
#if PICO_SDK_VERSION_MAJOR >= 2
    .pio_version = 0,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0,
#endif
#endif
};
typedef struct { PIO pio; uint sm, bank, pin; uint8_t reverse; } mcs_rp2_encoder_t;
static mcs_rp2_encoder_t mcs_rp2_encoders[MCS_HAL_MAX_ENCODERS];
static unsigned mcs_rp2_encoder_refs[MCS_RP2_ENCODER_BANKS];
static int mcs_rp2_encoder_open(void* ctx, int a, int b, void** handle) {
    (void)ctx;
    *handle = NULL;
    /* The PIO sampling instruction reads a consecutive input pair. */
    if (a < 0 || b < 0 || a >= 32 || b >= 32 || (a != b+1 && b != a+1)) return MCS_HAL_ENOTSUP;
    mcs_rp2_encoder_t* e = NULL;
    for (int i=0;i<MCS_HAL_MAX_ENCODERS;i++) if (!mcs_rp2_encoders[i].pio) { e=&mcs_rp2_encoders[i]; break; }
    if (!e) return MCS_HAL_EBUSY;
    PIO banks[] = { pio0, pio1,
#if MCS_RP2_ENCODER_BANKS > 2
        pio2,
#endif
    };
    for (uint bank=0;bank<MCS_RP2_ENCODER_BANKS;bank++) {
        PIO pio = banks[bank];
#if defined(PICO_PIO_VERSION) && PICO_PIO_VERSION > 0
        if (pio_get_gpio_base(pio) != 0) continue;
#endif
        if (!mcs_rp2_encoder_refs[bank] && !pio_can_add_program_at_offset(pio,&mcs_rp2_encoder_program,0)) continue;
        int sm=pio_claim_unused_sm(pio,false);
        if (sm < 0) continue;
        if (!mcs_rp2_encoder_refs[bank]) {
#if PICO_SDK_VERSION_MAJOR >= 2
            if (pio_add_program_at_offset(pio,&mcs_rp2_encoder_program,0) < 0) { pio_sm_unclaim(pio,(uint)sm); continue; }
#else
            pio_add_program_at_offset(pio,&mcs_rp2_encoder_program,0);
#endif
        }
        mcs_rp2_encoder_refs[bank]++;
        e->pio=pio; e->sm=(uint)sm; e->bank=bank; e->pin=(uint)(a<b?a:b);
        e->reverse=(uint8_t)((a>b)^!!MCS_RP2_ENCODER_INVERT(a)^!!MCS_RP2_ENCODER_INVERT(b));
        pio_sm_set_enabled(pio,e->sm,false);
        pio_sm_set_consecutive_pindirs(pio,e->sm,e->pin,2,false);
        pio_gpio_init(pio,e->pin); pio_gpio_init(pio,e->pin+1);
        gpio_pull_up(e->pin); gpio_pull_up(e->pin+1);
        pio_sm_config config=pio_get_default_sm_config();
        sm_config_set_wrap(&config,15,23);
        sm_config_set_in_pins(&config,e->pin);
        sm_config_set_in_shift(&config,false,false,32);
        sm_config_set_fifo_join(&config,PIO_FIFO_JOIN_NONE);
        /* At most 10 PIO cycles/transition: sample 100k transitions/s.
         * The hardware counter continues while the VM is busy. */
        float divider=(float)clock_get_hz(clk_sys)/1000000.0f;
        sm_config_set_clkdiv(&config,divider<1.0f?1.0f:divider);
        pio_sm_init(pio,e->sm,15,&config);
        pio_sm_exec(pio,e->sm,pio_encode_set(pio_y,0));
        pio_sm_set_enabled(pio,e->sm,true);
        *handle=e;
        return 0;
    }
    return MCS_HAL_EBUSY; /* do not silently switch to GPIO when PIO is occupied */
}
static int mcs_rp2_encoder_read(void* ctx, void* handle, uint32_t* count) {
    (void)ctx;
    mcs_rp2_encoder_t* e=(mcs_rp2_encoder_t*)handle;
    /* Drain buffered snapshots plus one guaranteed-fresh sample. PUSH noblock
     * means a full FIFO never stops the state machine's quadrature counter. */
    uint n=pio_sm_get_rx_fifo_level(e->pio,e->sm)+1; uint32_t value=0;
    while (n--) value=pio_sm_get_blocking(e->pio,e->sm);
    *count=e->reverse?0u-value:value;
    return 0;
}
static void mcs_rp2_encoder_close(void* ctx, void* handle) {
    (void)ctx;
    mcs_rp2_encoder_t* e=(mcs_rp2_encoder_t*)handle;
    pio_sm_set_enabled(e->pio,e->sm,false);
    pio_sm_clear_fifos(e->pio,e->sm);
    pio_sm_unclaim(e->pio,e->sm);
    if (--mcs_rp2_encoder_refs[e->bank]==0) pio_remove_program(e->pio,&mcs_rp2_encoder_program,0);
    gpio_set_dir(e->pin,false); gpio_set_dir(e->pin+1,false);
    gpio_set_function(e->pin,GPIO_FUNC_SIO); gpio_set_function(e->pin+1,GPIO_FUNC_SIO);
    memset(e,0,sizeof *e);
}
static void mcs_rp2_encoder_init(mcs_hal_t* hal) {
    hal->encoder_open=mcs_rp2_encoder_open; hal->encoder_read=mcs_rp2_encoder_read;
    hal->encoder_close=mcs_rp2_encoder_close; hal->encoder_name="PIO";
}
#else
static inline void mcs_rp2_encoder_init(mcs_hal_t* hal) { (void)hal; }
#endif
#endif
