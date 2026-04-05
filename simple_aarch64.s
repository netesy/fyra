.text
.globl main

main:
  // Simple leaf function: frame pointer omitted
main_start:
  ldr x0, #42
  b main_epilogue
main_epilogue:
  ret
