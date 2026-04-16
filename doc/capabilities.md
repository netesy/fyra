# Fyra Capability System (Production-Ready)

Fyra IR defines **intent**, not implementation. The IR only exposes stable capabilities and never platform details.

## Core Philosophy

Fyra IR does **not** expose:
- syscalls
- libc
- OS APIs
- runtime behavior

Fyra IR **does** expose:
- capability contracts

## External Call Model

### Syntax (non-restrictive)

```fyra
extern <capability>(<args>) : <optional_return>
```

Example:

```fyra
%fd = extern fs.open("file.txt", 0, 0)
%n  = extern io.write(%fd, %buf, 10)
```

### IR Representation

```text
Call {
    capability: "io.write",
    args: [...]
}
```

### Dispatch

```text
dispatchExtern(capability, args)
```

The backend resolves the capability to the platform implementation.

## Hard Rules (Enforced)

1. No syscall exposure in IR.
2. No OS-specific naming.
3. No libc assumptions.
4. No capability versioning.
5. No exceptions.
6. Capabilities are stable contracts.
7. All backends must implement all capabilities.
8. IR must remain fully portable.

## Core Resource Model (Critical)

| Responsibility | Domain |
| --- | --- |
| Resource creation | `fs` / `net` / `ipc` |
| Data movement | `io` |
| Resource destruction | `io.close` |

Invariants:
- All reads/writes go through `io.*`.
- All resources end with `io.close`.

## Capability Domains

### 1) IO — Unified Data Plane
- `io.read(res, buf, len) : l`
- `io.write(res, buf, len) : l`
- `io.flush(res)`
- `io.seek(res, offset, whence) : l`
- `io.close(res)`
- `io.dup(res) : l`
- `io.dup2(src, dst) : l`
- `io.set_nonblock(res, enabled)`

Applies to files, sockets, pipes, and devices.

Constraints:
- No creation
- No paths

### 2) Filesystem — Structure Only
- `fs.open(path, flags, mode) : l`
- `fs.create(path, mode)`
- `fs.unlink(path)`
- `fs.mkdir(path, mode)`
- `fs.rmdir(path)`
- `fs.rename(old, new)`
- `fs.stat(path, stat_ptr)`
- `fs.fstat(fd, stat_ptr)`
- `fs.listdir(path) : l`
- `fs.truncate(path, size)`
- `fs.sync(fd)`
- `fs.chmod(path, mode)`
- `fs.chown(path, uid, gid)`
- `fs.utime(path, access, modify)`
- `fs.cwd() : l`
- `fs.chdir(path)`
- `fs.path_join(a, b) : l`
- `fs.path_normalize(path) : l`
- `fs.lock(fd)`
- `fs.unlock(fd)`
- `fs.watch(path) : l`
- `fs.unwatch(handle)`
- `fs.atomic_write(path, buf, len)`

Constraints:
- No read/write
- No close

### 3) Memory
- `memory.alloc(size) : l`
- `memory.free(addr, size)`
- `memory.resize(addr, old, new) : l`
- `memory.map(addr, len, prot, flags, fd, off) : l`
- `memory.unmap(addr, len)`
- `memory.protect(addr, len, prot)`
- `memory.advise(addr, len, advice)`
- `memory.lock(addr, len)`
- `memory.unlock(addr, len)`

### 4) Process
- `process.exit(code)`
- `process.abort()`
- `process.spawn(exe, args) : l`
- `process.sleep(ms)`
- `process.info(ptr)`
- `process.args(out)`

### 5) Threading & Synchronization
- `thread.spawn(fn, arg) : l`
- `thread.join(tid)`
- `thread.exit(code)`
- `sync.mutex.lock(m)`
- `sync.mutex.unlock(m)`
- `sync.condvar.wait(cv, m)`
- `sync.condvar.signal(cv)`
- `sync.atomic.load(ptr) : l`
- `sync.atomic.store(ptr, val)`
- `sync.atomic.add(ptr, val) : l`
- `sync.fence()`

### 6) Time
- `time.now() : l`
- `time.monotonic() : l`
- `time.sleep(ns)`
- `time.utc_now(ptr)`
- `time.local_now(ptr)`
- `time.timer_create(out)`
- `time.timer_set(timer, ns)`
- `time.timer_cancel(timer)`

### 7) Event System (Async Foundation)
- `event.register(res, mask)`
- `event.unregister(res)`
- `event.poll(events, count, timeout) : w`
- `event.wait(handle, timeout) : w`

Typical backend mappings include epoll, kqueue, IOCP, and WASM event loops.

### 8) Networking (Resource Only)
- `net.socket(domain, type, protocol) : l`
- `net.bind(sock, addr, len)`
- `net.listen(sock, backlog)`
- `net.accept(sock, addr, len_ptr) : l`
- `net.connect(sock, addr, len)`
- `net.shutdown(sock, how)`
- `net.set_option(sock, opt, val)`
- `net.set_nonblock(sock, enabled)`
- `net.addr_ipv4(ip, port, out)`
- `net.addr_ipv6(ip, port, out)`

Constraints:
- No send/recv
- No close

### 9) IPC & Shared Memory
- `ipc.pipe(read_ptr, write_ptr)`
- `ipc.send(channel, buf, len) : l`
- `ipc.recv(channel, buf, len) : l`
- `memory.shared_create(name, size) : l`
- `memory.shared_attach(name) : l`

### 10) Environment & System
- `env.get(key) : l`
- `env.set(key, value)`
- `env.list(out)`
- `system.info(out)`
- `system.cpu_count() : w`
- `system.page_size() : w`
- `system.limit(resource, value)`

### 11) Signals
- `signal.register(sig, handler)`
- `signal.send(pid, sig)`

### 12) Random
- `random.bytes(buf, len)`
- `random.u64() : l`
- `random.seed(val)`

### 13) Error Model (Fixed)

Principles:
- No exceptions
- No hidden state
- Errors are explicit and queryable

Functions:
- `error.get() : w`
- `error.clear()`
- `error.str(code, buf) : l`
- `error.domain(code) : w`
- `error.category(code) : w`

Encoding (32-bit):

```text
[ DOMAIN (8) | CATEGORY (8) | CODE (16) ]
```

Domains:
1 IO
2 FS
3 NET
4 MEMORY
5 PROCESS
6 THREAD
7 EVENT
8 GPU
9 SYSTEM

Categories:
- `0 OK`
- `1 INVALID`
- `2 NOT_FOUND`
- `3 PERMISSION`
- `4 TIMEOUT`
- `5 WOULD_BLOCK`
- `6 INTERRUPTED`
- `7 UNSUPPORTED`
- `8 RESOURCE_EXHAUSTED`
- `9 INTERNAL`

Critical rule:

```fyra
result = extern io.read(...)
if result < 0:
    err = extern error.get()
```

Do not rely on implicit error state alone.

Async integration example:

```fyra
if error == WOULD_BLOCK:
    extern event.register(res, READ)
```

### 14) Debug
- `debug.log(msg)`
- `debug.trace()`
- `debug.assert(cond, msg)`
- `debug.dump(addr, len) : l`

### 15) Modules
- `module.load(path) : l`
- `module.unload(handle)`
- `module.resolve(handle, name) : l`

### 16) TTY
- `tty.isatty(res) : w`
- `tty.get_size(out)`
- `tty.set_mode(res, mode)`

### 17) Security
- `security.check(res, op) : w`
- `security.sandbox(mode)`

### 18) GPU / Accelerator
- `gpu.device_count() : w`
- `gpu.device_info(index, out)`
- `gpu.context_create(device) : l`
- `gpu.context_destroy(ctx)`
- `gpu.queue_create(ctx) : l`
- `gpu.queue_submit(queue, cmd_buf)`
- `gpu.mem_alloc(ctx, size) : l`
- `gpu.mem_free(ctx, ptr)`
- `gpu.mem_copy_to(ctx, dst, src, size)`
- `gpu.mem_copy_from(ctx, dst, src, size)`
- `gpu.module_load(ctx, code) : l`
- `gpu.kernel_launch(ctx, kernel, grid, block, args)`
- `gpu.event_create(ctx) : l`
- `gpu.event_record(evt, queue)`
- `gpu.event_wait(evt)`
- `gpu.sync(ctx)`

## Backend Mapping Model

```text
Call { capability: "io.write", args: [...] }
→ dispatchExtern()
→ backend adapter
```

| Target | Implementation |
| --- | --- |
| Linux | syscalls |
| Windows | WinAPI |
| macOS | POSIX / Mach |
| WASM | WASI / host imports |
| Bare metal | runtime / hardware |

## Runtime Position

If Fyra replaces a backend like QBE:
- You do not need a traditional runtime.
- You do need a thin platform layer (capability shim).

That shim provides:
- WASM import glue
- thread bootstrap
- optional memory bootstrap
- process entry normalization

## Production-Readiness Blockers

1. ABI standardization (calling conventions, registers, struct layout, stack alignment).
2. Capability compliance tests (per-capability and cross-platform failure/stress validation).
3. Deterministic error behavior across all backends.
4. WASM contract definition (imports, memory layout, async/event semantics).
5. Threading model clarification (especially WASM guarantees for `thread.spawn`).
6. GPU portability ABI standardization (kernel ABI, argument layout, memory model).
7. Security/sandbox model definition for WASM, embedded, and multi-tenant environments.

## Final Verdict

Fyra’s capability model is a portable systems IR that cleanly separates semantics from execution.
