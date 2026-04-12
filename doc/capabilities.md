# Fyra Capability System (Expanded)

Fyra uses a portable capability-based external call system instead of OS-specific syscalls. This makes the IR platform-agnostic and allows the backend to map capabilities to the appropriate platform APIs.

## Extern Syntax

```fyra
extern <capability_name>(<args>) : <return_type>
```

Example:
```fyra
%res = extern io.write(1, %buf, 10) : w
```

## Capability Categories

### 1. IO Capability (Stream + Resource Abstraction)
- `io.write(res: l, buf: l, len: l) : l`
- `io.read(res: l, buf: l, len: l) : l`
- `io.flush(res: l)`
- `io.open(path: l, flags: w, mode: w) : l`
- `io.close(res: l)`
- `io.seek(res: l, offset: l, whence: w) : l`
- `io.stat(res: l, stat_ptr: l) : w`

### 2. Memory Capability (Heap + Virtual Memory Abstraction)
- `memory.alloc(size: l) : l`
- `memory.free(addr: l, size: l)`
- `memory.resize(addr: l, old_size: l, new_size: l) : l`
- `memory.map(addr: l, len: l, prot: w, flags: w, fd: l, off: l) : l`
- `memory.unmap(addr: l, len: l)`
- `memory.protect(addr: l, len: l, prot: w) : w`

### 3. Process Capability (Execution Lifecycle Abstraction)
- `process.exit(code: w)`
- `process.abort()`
- `process.spawn(exe: l, args: l) : l`
- `process.sleep(ms: l)`
- `process.info(info_ptr: l) : w`

### 4. Threading & Concurrency Capability
- `thread.spawn(func_ptr: l, arg: l) : l`
- `thread.join(tid: l) : w`
- `thread.exit(code: w)`
- `sync.mutex.lock(mutex_ptr: l)`
- `sync.mutex.unlock(mutex_ptr: l)`
- `sync.atomic.load(ptr: l) : l`
- `sync.atomic.store(ptr: l, val: l)`
- `sync.atomic.add(ptr: l, val: l) : l`
- `sync.fence()`
- `sync.condvar.wait(cv_ptr: l, mutex_ptr: l)`
- `sync.condvar.signal(cv_ptr: l)`

### 5. Time Capability
- `time.now() : l` (Standard system time)
- `time.monotonic() : l` (High-resolution monotonic time)
- `time.sleep(ns: l)`
- `time.utc_now(ts_ptr: l)`
- `time.local_now(ts_ptr: l)`

### 6. Randomness Capability
- `random.bytes(buf: l, len: l)`
- `random.u64() : l`
- `random.seed(val: l)`

### 7. Error Capability (Unified Failure Model)
- `error.get() : w` (Get last capability error code)
- `error.clear()` (Clear last error)
- `error.raise(msg: l)` (Raise a portable trap/exception)

### 8. Networking Capability (Stream-based Network Abstraction)
- `net.connect(addr: l, port: w) : l`
- `net.send(res: l, buf: l, len: l) : l`
- `net.recv(res: l, buf: l, len: l) : l`

### 9. IPC & Shared Memory
- `ipc.send(channel: l, buf: l, len: l) : l`
- `ipc.recv(channel: l, buf: l, len: l) : l`
- `memory.shared_create(name: l, size: l) : l`
- `memory.shared_attach(name: l) : l`

### 10. Diagnostics & Debugging (Runtime Observability)
- `debug.log(msg: l)`
- `debug.trace()`
- `debug.assert(cond: w, msg: l)`
- `debug.dump(addr: l, len: l)`

### 11. Module & Dynamic Linking (Controlled Dynamic Runtime Extension)
- `module.load(path: l) : l`
- `module.unload(handle: l)`
- `module.resolve(handle: l, name: l) : l`
