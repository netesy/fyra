.intel_syntax noprefix
.text
.globl main

fibonacci:
  push rbp
  mov rbp, rsp
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  push r14
  push r15
  sub rsp, 144
  mov [rbp + 16], rcx
fibonacci_start:
  mov rax, [rbp + 16]
  cmp rax, 1
  setle al
  movzx eax, al
  mov [rbp - 64], eax
  mov rax, [rbp - 64]
  test rax, rax
  jne fibonacci_base_case
  jmp fibonacci_recursive_case
fibonacci_base_case:
  mov rax, [rbp + 16]
  jmp fibonacci_epilogue
fibonacci_recursive_case:
  mov rax, [rbp + 16]
  sub rax, 1
  mov [rbp - 72], rax
  mov rcx, [rbp - 72]
  call fibonacci
  mov [rbp - 80], rax
  mov rax, [rbp + 16]
  sub rax, 2
  mov [rbp - 88], rax
  mov rcx, [rbp - 88]
  call fibonacci
  mov [rbp - 96], rax
  mov rax, [rbp - 80]
  add rax, [rbp - 96]
  mov [rbp - 104], rax
  mov rax, [rbp - 104]
  jmp fibonacci_epilogue
fibonacci_epilogue:
  lea rsp, [rbp - 56]
  pop r15
  pop r14
  pop r13
  pop r12
  pop rdi
  pop rsi
  pop rbx
  pop rbp
  ret

main:
  push rbp
  mov rbp, rsp
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  push r14
  push r15
  sub rsp, 96
main_start:
  mov rcx, 10
  call fibonacci
  mov [rbp - 64], rax
  mov rax, [rbp - 64]
  jmp main_epilogue
main_epilogue:
  lea rsp, [rbp - 56]
  pop r15
  pop r14
  pop r13
  pop r12
  pop rdi
  pop rsi
  pop rbx
  pop rbp
  ret
