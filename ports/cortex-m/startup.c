/* MicroCS Cortex-M reference port - minimal startup (vector table, C runtime init). */
#include <stdint.h>
#include <string.h>
#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack, _sstack;
int main(void);

#define STACK_PAINT 0xC5C5C5C5u

void Reset_Handler(void) {
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;) *dst++ = 0;
    /* paint the unused stack so the high-water mark can be measured */
    volatile uint32_t* sp; __asm volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t* p = &_sstack; p < (uint32_t*)sp - 16; p++) *p = STACK_PAINT;
#if defined(__ARM_FP)
    /* enable CP10/CP11 (FPU) */
    *(volatile uint32_t*)0xE000ED88 |= (0xFu << 20);
    __asm volatile("dsb\n isb");
#endif
    board_exit(main());
}

uint32_t board_stack_high_water(void) {
    uint32_t* p = &_sstack;
    while (p < &_estack && *p == STACK_PAINT) p++;
    return (uint32_t)((uintptr_t)&_estack - (uintptr_t)p);
}

static void Fault_Handler(void) { board_puts("\n*** HardFault\n"); board_exit(99); }
static void Default_Handler(void) { board_puts("\n*** unexpected IRQ\n"); board_exit(98); }

__attribute__((section(".isr_vector"), used))
const void* const g_vectors[16] = {
    &_estack, (void*)Reset_Handler, (void*)Default_Handler, (void*)Fault_Handler,
    (void*)Fault_Handler, (void*)Fault_Handler, (void*)Fault_Handler, 0, 0, 0, 0,
    (void*)Default_Handler, (void*)Default_Handler, 0, (void*)Default_Handler, (void*)Default_Handler,
};
