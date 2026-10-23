#ifndef ENC_PIO_H
#define ENC_PIO_H
#include <stdint.h>
#include <stdbool.h>
#ifndef PICO_SDK_VERSION_MAJOR
#define PICO_SDK_VERSION_MAJOR 2
#endif
#if PICO_SDK_VERSION_MAJOR >= 2
#define NUM_PIOS 3
#define PICO_PIO_VERSION 1
#endif
typedef unsigned uint;
typedef struct {unsigned claimed,loaded,base;bool enabled[4];uint32_t count[4];} fake_pio_t;
typedef fake_pio_t* PIO;
extern fake_pio_t fake_pios[3];
#define pio0 (&fake_pios[0])
#define pio1 (&fake_pios[1])
#define pio2 (&fake_pios[2])
struct pio_program {const uint16_t* instructions;uint8_t length;int8_t origin;
#if PICO_SDK_VERSION_MAJOR >= 2
uint8_t pio_version,used_gpio_ranges;
#endif
};
typedef struct {uint pin,lo,hi;float divider;} pio_sm_config;
#define PIO_FIFO_JOIN_NONE 0
#define pio_y 2
bool pio_can_add_program_at_offset(PIO,const struct pio_program*,uint);
#if PICO_SDK_VERSION_MAJOR >= 2
int pio_add_program_at_offset(PIO,const struct pio_program*,uint);
#else
void pio_add_program_at_offset(PIO,const struct pio_program*,uint);
#endif
int pio_claim_unused_sm(PIO,bool);
void pio_sm_unclaim(PIO,uint);
uint pio_get_gpio_base(PIO);
void pio_sm_set_enabled(PIO,uint,bool);
void pio_sm_set_consecutive_pindirs(PIO,uint,uint,uint,bool);
void pio_gpio_init(PIO,uint);
pio_sm_config pio_get_default_sm_config(void);
void sm_config_set_wrap(pio_sm_config*,uint,uint);
void sm_config_set_in_pins(pio_sm_config*,uint);
void sm_config_set_in_shift(pio_sm_config*,bool,bool,uint);
void sm_config_set_fifo_join(pio_sm_config*,int);
void sm_config_set_clkdiv(pio_sm_config*,float);
void pio_sm_init(PIO,uint,uint,const pio_sm_config*);
void pio_sm_exec(PIO,uint,uint);
uint pio_encode_set(uint,uint);
uint pio_sm_get_rx_fifo_level(PIO,uint);
uint32_t pio_sm_get_blocking(PIO,uint);
void pio_sm_clear_fifos(PIO,uint);
void pio_remove_program(PIO,const struct pio_program*,uint);
#endif
