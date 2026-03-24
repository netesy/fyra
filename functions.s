.att_syntax prefix
.text
.globl main

my_add:
  push %rbp
  mov %rbp, %rsp
  push %r15
  subq $16, %rsp
  movq %rdi, -8(%rbp)
  movq %rsi, -16(%rbp)
my_add_start:
  movl -8(%rbp), %r15d
  addl -16(%rbp), %r15d
  movl %r15d, %eax
  leaq -8(%rbp), %rsp
  pop %r15
  pop %rbp
  ret

main:
  push %rbp
  mov %rbp, %rsp
  push %r15
main_start:
  movq $10, %rdi
  movq $20, %rsi
  call my_add
  movq %rax, %r15d
  movl %r15d, %eax
  leaq -8(%rbp), %rsp
  pop %r15
  pop %rbp
  ret
