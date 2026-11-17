ENTRY(Reset_Handler)
EXTERN(g_pfnVectors)

MEMORY
{
  APP_META (rx) : ORIGIN = 0x08010000, LENGTH = 512
  FLASH (rx) : ORIGIN = 0x08010200, LENGTH = 0x6FE00
  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K - 8
  CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x1000;
_Min_Stack_Size = 0x2000;

SECTIONS
{
  .fwversion :
  {
    KEEP(*(.fwversion))
  } > APP_META

  .isr_vector :
  {
    . = ALIGN(512);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text*)
    *(.glue_7 .glue_7t .eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata : { *(.rodata .rodata*) . = ALIGN(4); } > FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > FLASH
  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH
  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data* .RamFunc .RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH
  _sidata = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = .;
    *(.bss .bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = .;
  } > RAM

  .ccmram :
  {
    . = ALIGN(4);
    _sccmram = .;
    *(.ccmram .ccmram*)
    . = ALIGN(4);
    _eccmram = .;
  } > CCMRAM AT> FLASH
  _siccmram = LOADADDR(.ccmram);

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . += _Min_Heap_Size;
    . += _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
  /DISCARD/ : { *(.note.GNU-stack) }
}

ASSERT(SIZEOF(.fwversion) == 64, "PMC legacy package version must be exactly 64 bytes")
ASSERT(ADDR(.isr_vector) == 0x08010200, "PMC legacy application vectors are misplaced")
ASSERT((_estack & 0x2FFE0000) == 0x20000000, "Stack rejected by the immutable PMC bootloader")
ASSERT(end + _Min_Heap_Size + _Min_Stack_Size <= _estack, "PMC RAM heap/stack reservation overflow")