.section .rodata
.Lproc_environ:
  .string "/proc/self/environ"
.Lproc_cmdline:
  .string "/proc/self/cmdline"
.section .data
.align 8
heap_ptr:
  .quad __fyra_heap
.section .bss
.align 16
__fyra_heap:
  .zero 1048576
.text
.text
.globl main

  # DWARF Debug Information Section
  .section .debug_info
  # Compile Unit: simple.fyra
  # Producer: fyra compiler
  # Language: 4

  # DWARF Debug Abbreviation Section
  .section .debug_abbrev
  # Abbreviation table for debug info entries

  # DWARF String Section
  .section .debug_str
  # String ID 3: "double"
  # String ID 2: "float"
  # String ID 6: "fyra compiler"
  # String ID 1: "int"
  # String ID 5: "simple.fyra"
  # String ID 4: "void"
  # DWARF Line Table (simplified)
  .section .debug_line

  # DWARF Call Frame Information
  .section .debug_frame
  .long .Lcie_end - .Lcie_start # Length
.Lcie_start:
  .long 0xffffffff # CIE ID
  .byte 1 # Version
  .asciz "" # Augmentation
  .byte 1 # Code alignment factor
  .byte -8 # Data alignment factor
  .byte 16 # Return address register
  .align 8
.Lcie_end:
