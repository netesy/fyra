# Fyra Syscall Layer

Fyra provide a target-agnostic syscall layer that allows you to write portable low-level code. Instead of using raw syscall numbers which vary by platform, you can use named syscalls that are automatically mapped to the correct numbers and calling conventions for your target.

## Supported Syscalls

The following named syscalls are currently supported:

| Name | Description |
| :--- | :--- |
| `sys_exit` | Terminate the process |
| `sys_execve` | Execute a program |
| `sys_fork` | Create a child process |
| `sys_clone` | Create a child process (extended) |
| `sys_wait4` | Wait for process state change |
| `sys_kill` | Send a signal to a process |
| `sys_read` | Read from a file descriptor |
| `sys_write` | Write to a file descriptor |
| `sys_openat` | Open a file relative to a directory |
| `sys_close` | Close a file descriptor |
| `sys_lseek` | Reposition read/write file offset |
| `sys_mmap` | Map files or devices into memory |
| `sys_munmap` | Unmap memory |
| `sys_mprotect` | Set memory protection |
| `sys_brk` | Change data segment size |
| `sys_mkdirat` | Create a directory relative to another |
| `sys_unlinkat` | Remove a directory entry |
| `sys_renameat` | Rename a directory entry |
| `sys_getdents64`| Get directory entries |
| `sys_clock_gettime`| Get current clock time |
| `sys_nanosleep` | High-resolution sleep |
| `sys_rt_sigaction` | Examine and change a signal action |
| `sys_rt_sigprocmask`| Examine and change signal mask |
| `sys_rt_sigreturn` | Return from signal handler |
| `sys_getrandom` | Obtain a series of random bytes |
| `sys_uname` | Get name and information about kernel |
| `sys_getpid` | Get process identification |
| `sys_gettid` | Get thread identification |

## Usage in Fyra IR

To use a named syscall in Fyra IR, use the `syscall` instruction with the syscall name as the first argument within the parentheses:

```fyra
# Example: sys_write(fd=1, buf=$msg, count=13)
%res = syscall(sys_write, l 1, l $msg, l 13) : l

# Example: sys_exit(status=0)
syscall(sys_exit, l 0)
```

The compiler will automatically:
1. Lookup the correct syscall number for the selected target.
2. Place the syscall number in the appropriate register (e.g., `rax` on Linux x64, `x8` on Linux AArch64).
3. Place the arguments in the correct registers according to the target's syscall ABI.

## How to Extend

To add a new syscall to the target-agnostic layer, follow these steps:

### 1. Update `include/ir/Syscall.h`

Add the new syscall name to the `SyscallId` enum and update the `syscallIdToString` and `stringToSyscallId` helper functions.

```cpp
enum class SyscallId {
    // ...
    GetRandom,
    NewSyscall  // Add here
};
```

### 2. Update the Lexer

Add the new syscall name as a keyword in `src/parser/Lexer.cpp`:

```cpp
static const std::map<std::string, TokenType> keywords = {
    // ...
    {"sys_getrandom", TokenType::Keyword},
    {"sys_new_syscall", TokenType::Keyword}, // Add here
};
```

### 3. Implement Target Mappings

Update the `getSyscallNumber` implementation for each target to return the appropriate number for the new syscall.

For Linux x64 (`src/codegen/target/SystemV_x64.cpp`):
```cpp
uint64_t SystemV_x64::getSyscallNumber(ir::SyscallId id) const {
    switch (id) {
        // ...
        case ir::SyscallId::NewSyscall: return 999;
    }
}
```

Repeat this for other targets (AArch64, RISC-V, macOS, Windows).

### 4. Implementation Details

If the new syscall requires special handling (e.g., more arguments than the current `emitSyscall` handles), update the target-specific `emitSyscall` methods.
