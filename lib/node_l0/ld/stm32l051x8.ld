/* STM32L051C8: 64 KB flash, 8 KB RAM, 2 KB data EEPROM (home-radio-net ADR-0002 T3).
 * The data EEPROM is a NOLOAD region: nothing in the image is placed there, so
 * flashing firmware never touches node_id, the key, counters or the rain total. */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 8K
  EEPROM (r)   : ORIGIN = 0x08080000, LENGTH = 2K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x400;   /* checked at link time below */
_Min_Heap_Size  = 0;       /* no malloc */

PROVIDE(__eeprom_start = ORIGIN(EEPROM));
PROVIDE(__eeprom_size  = LENGTH(EEPROM));

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array    : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data) *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  /* Reserve heap + minimum stack; the link fails if RAM is too full. */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  /* Data EEPROM: addressed by node_l0's eeprom driver, never loaded. */
  .eeprom (NOLOAD) : { KEEP(*(.eeprom*)) } > EEPROM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
