/* MicroCS Cortex-M reference port - linker script.
 * FLASH_SIZE / RAM_SIZE / STACK_SIZE / SBRK_SIZE are passed with --defsym. */
FLASH_ORIGIN = 0x00000000;
RAM_ORIGIN   = 0x20000000;
MEMORY {
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = FLASH_SIZE
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = RAM_SIZE
}
ENTRY(Reset_Handler)
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .; } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  .sbrk (NOLOAD) : { . = ALIGN(8); _heap_start = .; . += SBRK_SIZE; _heap_end = .; } > RAM
  .stack (NOLOAD) : { . = ALIGN(8); _sstack = .; . += STACK_SIZE; _estack = .; } > RAM
  ASSERT(_estack <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow")
}
