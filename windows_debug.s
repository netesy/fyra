.intel_syntax noprefix
.text
.globl main

fibonacci_with_jumps:
  push rbp
  mov rbp, rsp
  push rbx
  push rsi
  push rdi
  push r12
  push r13
  push r14
  push r15
  sub rsp, 192
  mov [rbp + 16], rcx
fibonacci_with_jumps_entry:
  mov rax, 0
  mov [rbp - 64], rax
  mov rax, 1
  mov [rbp - 72], rax
  mov rax, 2
  mov [rbp - 80], rax
  mov rax, [rbp + 16]
  cmp rax, [rbp - 64]
  sete al
  movzx eax, al
  mov [rbp - 88], eax
  mov rax, [rbp - 88]
  test rax, rax
  jne fibonacci_with_jumps_return_zero
  jmp fibonacci_with_jumps_check_one
fibonacci_with_jumps_return_zero:
  mov rax, [rbp - 64]
  jmp fibonacci_with_jumps_epilogue
fibonacci_with_jumps_check_one:
  mov rax, [rbp + 16]
  cmp rax, [rbp - 72]
  sete al
  movzx eax, al
  mov [rbp - 96], eax
  mov rax, [rbp - 96]
  test rax, rax
  jne fibonacci_with_jumps_return_one
  jmp fibonacci_with_jumps_calculate
fibonacci_with_jumps_return_one:
  mov rax, [rbp - 72]
  jmp fibonacci_with_jumps_epilogue
fibonacci_with_jumps_calculate:
  mov rax, [rbp - 64]
  mov [rbp - 104], rax
  mov rax, [rbp - 72]
  mov [rbp - 112], rax
  mov rax, [rbp - 80]
  mov [rbp - 120], rax
  jmp fibonacci_with_jumps_loop_start
fibonacci_with_jumps_loop_start:
  mov rax, [rbp - 120]
  cmp rax, [rbp + 16]
  setle al
  movzx eax, al
  mov [rbp - 128], eax
  mov rax, [rbp - 128]
  test rax, rax
  jne fibonacci_with_jumps_loop_body
  jmp fibonacci_with_jumps_done
fibonacci_with_jumps_loop_body:
  mov rax, [rbp - 104]
  add rax, [rbp - 112]
  mov [rbp - 136], rax
  mov rax, [rbp - 112]
  mov [rbp - 144], rax
  mov rax, [rbp - 136]
  mov [rbp - 152], rax
  mov rax, [rbp - 120]
  add rax, [rbp - 72]
  mov [rbp - 160], rax
  jmp fibonacci_with_jumps_loop_start
fibonacci_with_jumps_done:
  mov rax, [rbp - 152]
  jmp fibonacci_with_jumps_epilogue
fibonacci_with_jumps_epilogue:
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

test_conditions:
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
test_conditions_start:
  mov rax, 5
  mov [rbp - 64], rax
  mov rax, 10
  mov [rbp - 72], rax
  mov rax, [rbp + 16]
  cmp rax, [rbp - 64]
  setl al
  movzx eax, al
  mov [rbp - 80], eax
  mov rax, [rbp - 80]
  test rax, rax
  jne test_conditions_small_case
  jmp test_conditions_check_ten
test_conditions_small_case:
  mov rax, [rbp + 16]
  imul rax, 2
  mov [rbp - 88], rax
  mov rax, [rbp - 88]
  jmp test_conditions_epilogue
test_conditions_check_ten:
  mov rax, [rbp + 16]
  cmp rax, [rbp - 72]
  sete al
  movzx eax, al
  mov [rbp - 96], eax
  mov rax, [rbp - 96]
  test rax, rax
  jne test_conditions_ten_case
  jmp test_conditions_large_case
test_conditions_ten_case:
  mov rax, [rbp - 72]
  jmp test_conditions_epilogue
test_conditions_large_case:
  mov rax, [rbp + 16]
  cqo
  idiv 2
  mov [rbp - 104], rax
  mov rax, [rbp - 104]
  jmp test_conditions_epilogue
test_conditions_epilogue:
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
  sub rsp, 112
main_start:
  mov rcx, 10
  call fibonacci_with_jumps
  mov [rbp - 64], rax
  mov rcx, [rbp - 64]
  call test_conditions
  mov [rbp - 72], rax
  mov rax, [rbp - 72]
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
