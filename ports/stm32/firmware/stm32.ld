/* MicroCS STM32 firmware - linker script.
 * --defsym: VECT_SIZE   bytes at 0x08000000 for the vector table (+ start-up code)
 *           FS_SIZE     bytes right after it kept free for files (F4: the 16 KB sectors 1-3; else 0)
 *           FLASH_SIZE  bytes for the program after that
 *           RAM_ORIGIN, RAM_SIZE, STACK_SIZE   everything else in RAM is the VM heap */
MEMORY {
  VECT  (rx)  : ORIGIN = 0x08000000, LENGTH = VECT_SIZE
  FLASH (rx)  : ORIGIN = 0x08000000 + VECT_SIZE + FS_SIZE, LENGTH = FLASH_SIZE
  RAM   (rwx) : ORIGIN = RAM_ORIGIN, LENGTH = RAM_SIZE
}
ENTRY(Reset_Handler)
__mcs_fs_flash_start = 0x08000000 + VECT_SIZE;
__mcs_fs_flash_end = __mcs_fs_flash_start + FS_SIZE;
SECTIONS {
  .isr_vector : {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    *startup_stm32*.o(.text*)
    *system_stm32*.o(.text* .rodata*)
    . = ALIGN(4);
  } > VECT
  .text : {
    . = ALIGN(4);
    *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .; } > FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) *(.RamFunc*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } > RAM
  .sbrk (NOLOAD) : { . = ALIGN(8); _sbrk_start = .; end = .; . += 2048; _sbrk_end = .; } > RAM
  .heap (NOLOAD) : { . = ALIGN(8); _heap_start = .; . = ORIGIN(RAM) + LENGTH(RAM) - STACK_SIZE; _heap_end = .; } > RAM
  .stack (NOLOAD) : { . = ALIGN(8); _sstack = .; . += STACK_SIZE; _estack = .; } > RAM
  ASSERT(_heap_end - _heap_start >= 16384, "less than 16 KB of RAM left for the MicroCS heap")
  ASSERT(_estack <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow")
}
