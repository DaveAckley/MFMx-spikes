ENTRY(_start)
SECTIONS {
  . = 0x4000;
  .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) *(.sdata .sdata.*) }
  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) *(.data*) *(.bss*) *(.sbss*) }
}
