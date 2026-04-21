#include "codegen/target/SystemV_x64.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <iostream>
#include <map>
#include <vector>

int main() {
    codegen::target::SystemV_x64 target;
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
        {"process.spawn", CapabilityDomain::PROCESS, 2, true},
        {"process.args", CapabilityDomain::PROCESS, 1, true},
        {"process.exit", CapabilityDomain::PROCESS, 1, true},
        {"memory.alloc", CapabilityDomain::MEMORY, 1, true},
        {"memory.free", CapabilityDomain::MEMORY, 2, true},
        {"thread.spawn", CapabilityDomain::THREAD, 2, true},
        {"thread.join", CapabilityDomain::THREAD, 1, true},
        {"sync.mutex.lock", CapabilityDomain::SYNC, 1, true},
        {"time.now", CapabilityDomain::TIME, 0, true},
        {"random.u64", CapabilityDomain::RANDOM, 0, true},
        {"net.socket", CapabilityDomain::NET, 3, true},
        {"net.connect", CapabilityDomain::NET, 3, true},
        {"net.listen", CapabilityDomain::NET, 2, true},
        {"event.poll", CapabilityDomain::EVENT, 1, true},
        {"ipc.send", CapabilityDomain::IPC, 3, true},
        {"ipc.recv", CapabilityDomain::IPC, 3, true},
        {"env.get", CapabilityDomain::ENV, 1, true},
        {"env.list", CapabilityDomain::ENV, 1, true},
        {"system.info", CapabilityDomain::SYSTEM, 1, true},
        {"signal.send", CapabilityDomain::SIGNAL, 2, true},
        {"signal.register", CapabilityDomain::SIGNAL, 2, true},
        {"module.load", CapabilityDomain::MODULE, 1, true},
        {"tty.isatty", CapabilityDomain::TTY, 1, true},
        {"security.chmod", CapabilityDomain::SECURITY, 2, true},
        {"gpu.compute", CapabilityDomain::GPU, 2, false},
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
