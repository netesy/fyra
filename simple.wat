(module
  (import "wasi_unstable" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_unstable" "fd_read" (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_unstable" "proc_exit" (func $proc_exit (param i32)))
  (memory 1)
  (global $__heap_ptr (mut i32) (i32.const 1024))
.text
.globl main

main:
  (func $main (result i32)
main_start:
  $42
  return
  )
