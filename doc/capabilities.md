# Fyra Capability System (Expanded)

Fyra uses a portable capability-based external call system instead of OS-specific syscalls. This makes the IR platform-agnostic and allows the backend to map capabilities to the appropriate platform APIs.

## Extern Syntax

```fyra
extern <capability_name>(<args>) : <return_type>
```

Example:
```fyra
%res = extern io.write(1, %buf, 10) : i32
```

## Capability Categories

### 1. IO Capability (Stream + Resource Abstraction)
- `io.write(res: i64, buf: i64, len: i64) : i64`
- `io.read(res: i64, buf: i64, len: i64) : i64`
- `io.flush(res: i64)`
- `io.open(path: i64, flags: i32, mode: i32) : i64`
- `io.close(res: i64)`
- `io.seek(res: i64, offset: i64, whence: i32) : i64`
- `io.stat(res: i64, stat_ptr: i64) : i32`

### 2. Memory Capability (Heap + Virtual Memory Abstraction)
- `memory.alloc(size: i64) : i64`
- `memory.free(addr: i64, size: i64)`
- `memory.resize(addr: i64, old_size: i64, new_size: i64) : i64`
- `memory.map(addr: i64, len: i64, prot: i32, flags: i32, fd: i64, off: i64) : i64`
- `memory.unmap(addr: i64, len: i64)`
- `memory.protect(addr: i64, len: i64, prot: i32) : i32`

### 3. Process Capability (Execution Lifecycle Abstraction)
- `process.exit(code: i32)`
- `process.abort()`
- `process.spawn(exe: i64, args: i64) : i64`
- `process.sleep(ms: i64)`
- `process.info(info_ptr: i64) : i32`

### 4. Threading & Concurrency Capability
- `thread.spawn(func_ptr: i64, arg: i64) : i64`
- `thread.join(tid: i64) : i32`
- `thread.exit(code: i32)`
- `sync.mutex.lock(mutex_ptr: i64)`
- `sync.mutex.unlock(mutex_ptr: i64)`
- `sync.atomic.load(ptr: i64) : i64`
- `sync.atomic.store(ptr: i64, val: i64)`
- `sync.atomic.add(ptr: i64, val: i64) : i64`
- `sync.fence()`
- `sync.condvar.wait(cv_ptr: i64, mutex_ptr: i64)`
- `sync.condvar.signal(cv_ptr: i64)`

### 5. Time Capability
- `time.now() : i64` (Standard system time)
- `time.monotonic() : i64` (High-resolution monotonic time)
- `time.sleep(ns: i64)`
- `time.utc_now(ts_ptr: i64)`
- `time.local_now(ts_ptr: i64)`

### 6. Randomness Capability
- `random.bytes(buf: i64, len: i64)`
- `random.u64() : i64`
- `random.seed(val: i64)`

### 7. Error Capability (Unified Failure Model)
- `error.get() : i32` (Get last capability error code)
- `error.clear()` (Clear last error)
- `error.raise(msg: i64)` (Raise a portable trap/exception)

### 8. Networking Capability (Stream-based Network Abstraction)
- `net.connect(addr: i64, port: i32) : i64`
- `net.send(res: i64, buf: i64, len: i64) : i64`
- `net.recv(res: i64, buf: i64, len: i64) : i64`

### 9. IPC & Shared Memory
- `ipc.send(channel: i64, buf: i64, len: i64) : i64`
- `ipc.recv(channel: i64, buf: i64, len: i64) : i64`
- `memory.shared_create(name: i64, size: i64) : i64`
- `memory.shared_attach(name: i64) : i64`

### 10. Diagnostics & Debugging (Runtime Observability)
- `debug.log(msg: i64)`
- `debug.trace()`
- `debug.assert(cond: i32, msg: i64)`
- `debug.dump(addr: i64, len: i64)`

### 11. Module & Dynamic Linking (Controlled Dynamic Runtime Extension)
- `module.load(path: i64) : i64`
- `module.unload(handle: i64)`
- `module.resolve(handle: i64, name: i64) : i64`
