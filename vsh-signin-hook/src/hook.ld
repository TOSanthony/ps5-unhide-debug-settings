/* Freestanding hook: keep executable and writable segments on separate PS5 pages. */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(vshsig_hook_entry)

PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS {
  . = 0;
  .text : { *(.text .text.*) *(.plt .plt.*) } :text
  .rodata : { *(.rodata .rodata.*) } :text
  .eh_frame_hdr : { *(.eh_frame_hdr) } :text
  .eh_frame : { *(.eh_frame) } :text

  . = ALIGN(0x4000);
  .data : { *(.data .data.*) *(.got .got.*) } :data
  .dynamic : { *(.dynamic) } :data :dynamic
  .dynsym : { *(.dynsym) } :data
  .dynstr : { *(.dynstr) } :data
  .gnu.hash : { *(.gnu.hash) } :data
  .rela.dyn : { *(.rela.dyn) *(.rela.plt) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
}
