/* NetHack on the ATW800/2 FPGA transputer, 6 MB layout.
 *
 * 0x80001000-0x805FFFFF is private RAM.  0x80600000-0x807FFFFF is the
 * shared video RAM (framebuffer, host aperture, request mailbox at
 * 0x1F8800); the image never reaches it. */
__stack = 0x805FF000;

SECTIONS {
  . = 0x80000100;
  entry : { *(entry) }
  text : { *(text) *(.text) *(.text.*) }
  . = ALIGN(8);
  data : { *(data) *(rodata) *(.data) *(.data.*) *(.rodata) *(.rodata.*) }
  . = ALIGN(8);
  __bss_start = .;
  bss (NOLOAD) : { *(bss) *(.bss) *(.bss.*) *(COMMON) . = ALIGN(4); }
  __bss_end = .;
  __heap_start = .;
  __heap_end = __stack - 0x20000;   /* 128 KB stack reserve */
  __heap2_start = 0; __heap2_end = 0;
  /* LLVM libt800 has no C symbol prefix: alias the heap bounds */
  _heap_start = __heap_start; _heap_end = __heap_end; _heap2_start = 0; _heap2_end = 0;
  ASSERT(__heap_end <= 0x805E0000, "image leaves no heap")
}
