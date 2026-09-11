/* NetHack on a 4 MB T800 TRAM: RAM 0x80000000..0x803FFFFF, stack top
   0x803FF000, 128 KB stack reserve. */
__stack = 0x803FF000;
SECTIONS {
  . = 0x80000100;
  text : { *(.entry) *(entry) *(text) *(.text) *(.text.*) }
  /* init table walked by crt0: our stubs, then clang's .init_array entries
     (same 4-byte function-pointer format).  .fini_array is walked backwards
     by exit(). */
  . = ALIGN(4);
  __t800_init_start = .;
  .t800.init : {
    KEEP(*(.t800.init))
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
  }
  __t800_init_end = .;
  . = ALIGN(4);
  __t800_fini_start = .;
  .t800.fini : {
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
  }
  __t800_fini_end = .;
  . = ALIGN(4);
  /* lld's .static area ends in the NOBITS .static.bss, which must lie in
     the range crt0 zeroes */
  data : { *(data) *(rodata) *(.data) *(.data.*) *(.rodata) *(.rodata.*) *(.static) }
  . = ALIGN(4);
  __bss_start = .;
  bss (NOLOAD) : { *(bss) *(.bss) *(.bss.*) *(COMMON) *(.static.bss) . = ALIGN(4); }
  __bss_end = .;
  /* libt800's malloc reads these four through __t800_heap_*() (heapsym.s). */
  _heap_start = .;
  _heap_end = __stack - 0x20000;
  _heap2_start = 0; _heap2_end = 0;
  ASSERT(_heap_end <= 0x803E0000, "image leaves no heap on 4 MB")
  ASSERT(__bss_end <= _heap_end, "image + bss overflow 4 MB")
}
