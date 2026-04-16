#!/usr/bin/env bash
set -euo pipefail

win_file="src/codegen/target/Windows_x64.cpp"
linux_file="src/codegen/target/SystemV_x64.cpp"

# Windows extern capability mapping should use Win32 APIs
rg -q 'call WriteFile' "$win_file"
rg -q 'call ReadFile' "$win_file"
rg -q 'call CreateFileA' "$win_file"
rg -q 'call CloseHandle' "$win_file"
rg -q 'call ExitProcess' "$win_file"
rg -q 'module.resolve' "$win_file"
rg -q 'call GetProcAddress' "$win_file"

# Linux extern capability mapping should use Linux syscalls for core I/O/process
rg -q 'movq \$1, %rax' "$linux_file"   # write
rg -q 'movq \$0, %rax' "$linux_file"   # read
rg -q 'movq \$2, %rax' "$linux_file"   # open
rg -q 'movq \$3, %rax' "$linux_file"   # close
rg -q 'movq \$60, %rax' "$linux_file"  # exit
rg -q 'movq \$41, %rax' "$linux_file"  # socket
rg -q 'movq \$42, %rax' "$linux_file"  # connect

echo "target api/syscall audit passed"
