#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <iostream>
#include <map>
#include <vector>

int main() {
    auto target_ptr = codegen::target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux}); auto& target = *target_ptr;
    using codegen::target::CapabilityDomain;

    struct Case { const char* name; CapabilityDomain domain; int argc; bool expectSupported; };
    const std::vector<Case> cases = {
        {"io.read", CapabilityDomain::IO, 3, true},
        {"io.write", CapabilityDomain::IO, 3, true},
        {"io.seek", CapabilityDomain::IO, 3, true},
        {"io.close", CapabilityDomain::IO, 1, true},
        {"io.flush", CapabilityDomain::IO, 1, true},
        {"fs.open", CapabilityDomain::FS, 3, true},
        {"fs.create", CapabilityDomain::FS, 3, true},
        {"fs.stat", CapabilityDomain::FS, 2, true},
        {"fs.remove", CapabilityDomain::FS, 1, true},
        {"fs.rename", CapabilityDomain::FS, 2, true},
        {"fs.mkdir", CapabilityDomain::FS, 2, true},
        {"fs.rmdir", CapabilityDomain::FS, 1, true},
        {"process.spawn", CapabilityDomain::PROCESS, 2, true},
        {"process.args", CapabilityDomain::PROCESS, 1, true},
        {"process.exit", CapabilityDomain::PROCESS, 1, true},
        {"process.getpid", CapabilityDomain::PROCESS, 0, true},
        {"memory.alloc", CapabilityDomain::MEMORY, 1, true},
        {"memory.free", CapabilityDomain::MEMORY, 2, true},
        {"memory.usage", CapabilityDomain::MEMORY, 0, true},
        {"thread.spawn", CapabilityDomain::THREAD, 2, true},
        {"thread.join", CapabilityDomain::THREAD, 1, true},
        {"thread.detach", CapabilityDomain::THREAD, 1, true},
        {"thread.yield", CapabilityDomain::THREAD, 0, true},
        {"thread.getid", CapabilityDomain::THREAD, 0, true},
        {"sync.mutex.lock", CapabilityDomain::SYNC, 1, true},
        {"sync.atomic.add", CapabilityDomain::SYNC, 2, true},
        {"sync.atomic.sub", CapabilityDomain::SYNC, 2, true},
        {"sync.atomic.cas", CapabilityDomain::SYNC, 3, true},
        {"time.now", CapabilityDomain::TIME, 0, true},
        {"time.monotonic", CapabilityDomain::TIME, 0, true},
        {"time.sleep", CapabilityDomain::TIME, 1, true},
        {"random.u64", CapabilityDomain::RANDOM, 0, true},
        {"random.bytes", CapabilityDomain::RANDOM, 2, true},
        {"error.get", CapabilityDomain::ERROR, 0, true},
        {"error.str", CapabilityDomain::ERROR, 1, true},
        {"debug.log", CapabilityDomain::DEBUG, 1, true},
        {"debug.break", CapabilityDomain::DEBUG, 0, true},
        {"debug.trace", CapabilityDomain::DEBUG, 0, true},
        {"net.socket", CapabilityDomain::NET, 3, true},
        {"net.connect", CapabilityDomain::NET, 3, true},
        {"net.listen", CapabilityDomain::NET, 2, true},
        {"net.accept", CapabilityDomain::NET, 3, true},
        {"net.send", CapabilityDomain::NET, 4, true},
        {"net.recv", CapabilityDomain::NET, 4, true},
        {"net.close", CapabilityDomain::NET, 1, true},
        {"net.bind", CapabilityDomain::NET, 2, true},
        {"event.poll", CapabilityDomain::EVENT, 1, true},
        {"event.create", CapabilityDomain::EVENT, 0, true},
        {"event.modify", CapabilityDomain::EVENT, 3, true},
        {"event.close", CapabilityDomain::EVENT, 1, true},
        {"ipc.send", CapabilityDomain::IPC, 3, true},
        {"ipc.recv", CapabilityDomain::IPC, 3, true},
        {"ipc.connect", CapabilityDomain::IPC, 1, true},
        {"ipc.listen", CapabilityDomain::IPC, 1, true},
        {"env.get", CapabilityDomain::ENV, 1, true},
        {"env.set", CapabilityDomain::ENV, 2, true},
        {"env.list", CapabilityDomain::ENV, 1, true},
        {"system.info", CapabilityDomain::SYSTEM, 1, true},
        {"system.reboot", CapabilityDomain::SYSTEM, 0, true},
        {"system.shutdown", CapabilityDomain::SYSTEM, 0, true},
        {"signal.send", CapabilityDomain::SIGNAL, 2, true},
        {"signal.register", CapabilityDomain::SIGNAL, 2, true},
        {"signal.wait", CapabilityDomain::SIGNAL, 1, true},
        {"system.info", CapabilityDomain::SYSTEM, 1, true},
        {"system.reboot", CapabilityDomain::SYSTEM, 0, true},
        {"system.shutdown", CapabilityDomain::SYSTEM, 0, true},
        {"module.load", CapabilityDomain::MODULE, 1, true},
        {"module.unload", CapabilityDomain::MODULE, 1, true},
        {"module.getsym", CapabilityDomain::MODULE, 2, true},
        {"tty.isatty", CapabilityDomain::TTY, 1, true},
        {"tty.getsize", CapabilityDomain::TTY, 1, true},
        {"tty.setmode", CapabilityDomain::TTY, 2, true},
        {"security.chmod", CapabilityDomain::SECURITY, 2, true},
        {"security.chown", CapabilityDomain::SECURITY, 3, true},
        {"security.getuid", CapabilityDomain::SECURITY, 0, true},
        {"gpu.compute", CapabilityDomain::GPU, 2, true},
        {"gpu.malloc", CapabilityDomain::GPU, 1, true},
        {"gpu.memcpy", CapabilityDomain::GPU, 3, true},
    };

    std::map<CapabilityDomain, int> totals;
    std::map<CapabilityDomain, int> passed;
    int valid = 0;
    int total = 0;

    auto* i64 = ir::IntegerType::get(64);
    auto* arg = ir::ConstantInt::get(i64, 1);
    for (const auto& t : cases) {
        const auto* spec = target.findCapability(t.name);
        if (!spec) continue;

        std::vector<ir::Value*> args(t.argc, arg);
        ir::Type* retTy = spec->returnsValue ? static_cast<ir::Type*>(i64) : static_cast<ir::Type*>(ir::VoidType::get());
        ir::ExternCallInstruction instr(retTy, args, t.name);
        const bool isSupported = target.validateCapability(instr, *spec);
        totals[t.domain]++;
        total++;
        if (isSupported == t.expectSupported) {
            passed[t.domain]++;
            valid++;
        }
    }

    std::cout << "Capability Compliance Report (SystemV_x64)\n";
    for (const auto& [domain, count] : totals) {
        const int ok = passed[domain];
        const bool domainPass = ok == count;
        std::cout << "Domain " << static_cast<int>(domain) << ": " << (domainPass ? "PASS" : "FAIL")
                  << " (" << ok << "/" << count << ")\n";
    }
    const double pct = total ? (100.0 * static_cast<double>(valid) / static_cast<double>(total)) : 0.0;
    std::cout << "Conformance: " << pct << "%\n";
    return valid == total ? 0 : 1;
}
