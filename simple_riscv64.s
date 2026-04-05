.text
.globl main

main:
  # Simple leaf function: frame pointer and stack adjustment omitted
main_start:
  li a0, 42
  j main_epilogue
main_epilogue:
  ld ra, -8(s0)
  ld s0, -16(s0)
  addi sp, s0, 0
  jr ra
