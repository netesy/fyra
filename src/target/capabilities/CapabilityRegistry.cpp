#include "target/capabilities/Capabilities.h"
#include <map>

namespace target {

static const std::vector<CapabilitySpec> specs = {
    {CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_OPEN, "io.open", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_CLOSE, "io.close", CapabilityDomain::IO, 1, 1, true, true},
    {CapabilityId::IO_SEEK, "io.seek", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_STAT, "io.stat", CapabilityDomain::IO, 2, 2, true, true},
    {CapabilityId::IO_FLUSH, "io.flush", CapabilityDomain::IO, 1, 1, true, true},
    {CapabilityId::FS_OPEN, "fs.open", CapabilityDomain::FS, 3, 3, true, true},
    {CapabilityId::FS_CREATE, "fs.create", CapabilityDomain::FS, 3, 3, true, true},
    {CapabilityId::FS_STAT, "fs.stat", CapabilityDomain::FS, 2, 2, true, true},
    {CapabilityId::FS_REMOVE, "fs.remove", CapabilityDomain::FS, 1, 1, true, true},
    {CapabilityId::FS_RENAME, "fs.rename", CapabilityDomain::FS, 2, 2, true, true},
    {CapabilityId::FS_MKDIR, "fs.mkdir", CapabilityDomain::FS, 2, 2, true, true},
    {CapabilityId::FS_RMDIR, "fs.rmdir", CapabilityDomain::FS, 1, 1, true, true},
    {CapabilityId::MEMORY_ALLOC, "memory.alloc", CapabilityDomain::MEMORY, 1, 1, true, true},
    {CapabilityId::MEMORY_FREE, "memory.free", CapabilityDomain::MEMORY, 2, 2, false, false},
    {CapabilityId::MEMORY_MAP, "memory.map", CapabilityDomain::MEMORY, 6, 6, true, true},
    {CapabilityId::MEMORY_PROTECT, "memory.protect", CapabilityDomain::MEMORY, 3, 3, true, true},
    {CapabilityId::MEMORY_USAGE, "memory.usage", CapabilityDomain::MEMORY, 0, 0, true, false},
    {CapabilityId::PROCESS_EXIT, "process.exit", CapabilityDomain::PROCESS, 1, 1, false, false},
    {CapabilityId::PROCESS_ABORT, "process.abort", CapabilityDomain::PROCESS, 0, 0, false, false},
    {CapabilityId::PROCESS_SLEEP, "process.sleep", CapabilityDomain::PROCESS, 1, 1, false, false},
    {CapabilityId::PROCESS_SPAWN, "process.spawn", CapabilityDomain::PROCESS, 2, 2, true, true},
    {CapabilityId::PROCESS_ARGS, "process.args", CapabilityDomain::PROCESS, 1, 1, true, true},
    {CapabilityId::PROCESS_GETPID, "process.getpid", CapabilityDomain::PROCESS, 0, 0, true, false},
    {CapabilityId::THREAD_SPAWN, "thread.spawn", CapabilityDomain::THREAD, 2, 2, true, true},
    {CapabilityId::THREAD_JOIN, "thread.join", CapabilityDomain::THREAD, 1, 1, true, true},
    {CapabilityId::THREAD_DETACH, "thread.detach", CapabilityDomain::THREAD, 1, 1, true, true},
    {CapabilityId::THREAD_YIELD, "thread.yield", CapabilityDomain::THREAD, 0, 0, false, false},
    {CapabilityId::THREAD_GETID, "thread.getid", CapabilityDomain::THREAD, 0, 0, true, false},
    {CapabilityId::SYNC_MUTEX_LOCK, "sync.mutex.lock", CapabilityDomain::SYNC, 1, 1, false, false},
    {CapabilityId::SYNC_MUTEX_UNLOCK, "sync.mutex.unlock", CapabilityDomain::SYNC, 1, 1, false, false},
    {CapabilityId::SYNC_ATOMIC_ADD, "sync.atomic.add", CapabilityDomain::SYNC, 2, 2, true, false},
    {CapabilityId::SYNC_ATOMIC_SUB, "sync.atomic.sub", CapabilityDomain::SYNC, 2, 2, true, false},
    {CapabilityId::SYNC_ATOMIC_CAS, "sync.atomic.cas", CapabilityDomain::SYNC, 3, 3, true, false},
    {CapabilityId::TIME_NOW, "time.now", CapabilityDomain::TIME, 0, 0, true, false},
    {CapabilityId::TIME_MONOTONIC, "time.monotonic", CapabilityDomain::TIME, 0, 0, true, false},
    {CapabilityId::TIME_SLEEP, "time.sleep", CapabilityDomain::TIME, 1, 1, false, false},
    {CapabilityId::RANDOM_U64, "random.u64", CapabilityDomain::RANDOM, 0, 0, true, false},
    {CapabilityId::RANDOM_BYTES, "random.bytes", CapabilityDomain::RANDOM, 2, 2, true, true},
    {CapabilityId::ERROR_GET, "error.get", CapabilityDomain::ERROR, 0, 0, true, false},
    {CapabilityId::ERROR_STR, "error.str", CapabilityDomain::ERROR, 1, 1, true, false},
    {CapabilityId::DEBUG_LOG, "debug.log", CapabilityDomain::DEBUG, 1, 1, false, false},
    {CapabilityId::DEBUG_BREAK, "debug.break", CapabilityDomain::DEBUG, 0, 0, false, false},
    {CapabilityId::DEBUG_TRACE, "debug.trace", CapabilityDomain::DEBUG, 0, 0, false, false},
    {CapabilityId::NET_SOCKET, "net.socket", CapabilityDomain::NET, 3, 3, true, true},
    {CapabilityId::NET_CONNECT, "net.connect", CapabilityDomain::NET, 3, 3, true, true},
    {CapabilityId::NET_LISTEN, "net.listen", CapabilityDomain::NET, 2, 2, true, true},
    {CapabilityId::NET_ACCEPT, "net.accept", CapabilityDomain::NET, 3, 3, true, true},
    {CapabilityId::NET_SEND, "net.send", CapabilityDomain::NET, 4, 4, true, true},
    {CapabilityId::NET_RECV, "net.recv", CapabilityDomain::NET, 4, 4, true, true},
    {CapabilityId::NET_CLOSE, "net.close", CapabilityDomain::NET, 1, 1, true, true},
    {CapabilityId::NET_BIND, "net.bind", CapabilityDomain::NET, 2, 2, true, true},
    {CapabilityId::EVENT_POLL, "event.poll", CapabilityDomain::EVENT, 1, 1, true, true},
    {CapabilityId::EVENT_CREATE, "event.create", CapabilityDomain::EVENT, 0, 0, true, true},
    {CapabilityId::EVENT_MODIFY, "event.modify", CapabilityDomain::EVENT, 3, 3, true, true},
    {CapabilityId::EVENT_CLOSE, "event.close", CapabilityDomain::EVENT, 1, 1, true, true},
    {CapabilityId::IPC_SEND, "ipc.send", CapabilityDomain::IPC, 3, 3, true, true},
    {CapabilityId::IPC_RECV, "ipc.recv", CapabilityDomain::IPC, 3, 3, true, true},
    {CapabilityId::IPC_CONNECT, "ipc.connect", CapabilityDomain::IPC, 1, 1, true, true},
    {CapabilityId::IPC_LISTEN, "ipc.listen", CapabilityDomain::IPC, 1, 1, true, true},
    {CapabilityId::ENV_GET, "env.get", CapabilityDomain::ENV, 1, 2, true, true},
    {CapabilityId::ENV_SET, "env.set", CapabilityDomain::ENV, 2, 2, true, true},
    {CapabilityId::ENV_LIST, "env.list", CapabilityDomain::ENV, 0, 1, true, true},
    {CapabilityId::SYSTEM_INFO, "system.info", CapabilityDomain::SYSTEM, 1, 1, true, true},
    {CapabilityId::SYSTEM_REBOOT, "system.reboot", CapabilityDomain::SYSTEM, 0, 0, true, true},
    {CapabilityId::SYSTEM_SHUTDOWN, "system.shutdown", CapabilityDomain::SYSTEM, 0, 0, true, true},
    {CapabilityId::SIGNAL_SEND, "signal.send", CapabilityDomain::SIGNAL, 2, 2, true, true},
    {CapabilityId::SIGNAL_REGISTER, "signal.register", CapabilityDomain::SIGNAL, 2, 2, true, true},
    {CapabilityId::SIGNAL_WAIT, "signal.wait", CapabilityDomain::SIGNAL, 1, 1, true, true},
    {CapabilityId::MODULE_LOAD, "module.load", CapabilityDomain::MODULE, 1, 1, true, true},
    {CapabilityId::MODULE_UNLOAD, "module.unload", CapabilityDomain::MODULE, 1, 1, true, true},
    {CapabilityId::MODULE_GETSYM, "module.getsym", CapabilityDomain::MODULE, 2, 2, true, true},
    {CapabilityId::TTY_ISATTY, "tty.isatty", CapabilityDomain::TTY, 1, 1, true, false},
    {CapabilityId::TTY_GETSIZE, "tty.getsize", CapabilityDomain::TTY, 1, 1, true, false},
    {CapabilityId::TTY_SETMODE, "tty.setmode", CapabilityDomain::TTY, 2, 2, true, false},
    {CapabilityId::SECURITY_CHMOD, "security.chmod", CapabilityDomain::SECURITY, 2, 2, true, true},
    {CapabilityId::SECURITY_CHOWN, "security.chown", CapabilityDomain::SECURITY, 3, 3, true, true},
    {CapabilityId::SECURITY_GETUID, "security.getuid", CapabilityDomain::SECURITY, 0, 0, true, false},
    {CapabilityId::GPU_COMPUTE, "gpu.compute", CapabilityDomain::GPU, 2, 2, false, true},
    {CapabilityId::GPU_MALLOC, "gpu.malloc", CapabilityDomain::GPU, 1, 1, false, true},
    {CapabilityId::GPU_MEMCPY, "gpu.memcpy", CapabilityDomain::GPU, 3, 3, false, true}
};

const CapabilitySpec* CapabilityRegistry::find(std::string_view name) {
    for (const auto& spec : specs) {
        if (spec.name == name) return &spec;
    }
    return nullptr;
}

const std::vector<CapabilitySpec>& CapabilityRegistry::getAll() {
    return specs;
}

}
}
