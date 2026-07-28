OUTPUT_ARCH(m68k)
ENTRY(_start)
MEMORY {
  rom (rx) : ORIGIN = 0x00880800, LENGTH = 0x00007000
  ram (rw) : ORIGIN = 0x00FF0000, LENGTH = 0x00010000
}
SECTIONS {
  .text 0x00880800 : { *(.text) *(.text.*) *(.rodata) *(.rodata.*) } > rom
  .data : AT (LOADADDR(.text) + SIZEOF(.text)) { __data_start = .; *(.data) *(.data.*) __data_end = .; } > ram
  .bss  : { __bss_start = .; *(.bss) *(.bss.*) *(COMMON) __bss_end = .; } > ram
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
