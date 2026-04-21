#include "codegen/target/MacOS_AArch64.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <vector>
#include <iostream>

int main() {
    codegen::target::MacOS_AArch64 target;
    struct Case { const char* name; int argc; bool expect; };
    const std::vector<Case> cases = {
        {"io.read", 3, true}, {"io.write", 3, true}, {"io.open", 3, true}, {"io.close", 1, true},
        {"io.seek", 3, true}, {"io.stat", 2, true}, {"io.flush", 1, true},
        {"fs.open", 3, true}, {"fs.create", 3, true}, {"fs.stat", 2, true}, {"fs.remove", 1, true},
        {"memory.alloc", 1, true}, {"memory.free", 2, true}, {"memory.map", 6, true}, {"memory.protect", 3, true},
        {"process.exit", 1, true}, {"process.abort", 0, true}, {"process.sleep", 1, true}, {"process.spawn", 2, true}, {"process.args", 1, true},
        {"thread.spawn", 2, true}, {"thread.join", 1, true},
        {"sync.mutex.lock", 1, true}, {"sync.mutex.unlock", 1, true},
        {"time.now", 0, true}, {"time.monotonic", 0, true}, {"event.poll", 1, true},
        {"net.socket", 3, true}, {"net.connect", 3, true}, {"net.listen", 2, true}, {"net.accept", 3, true}, {"net.send", 4, true}, {"net.recv", 4, true},
        {"ipc.send", 3, true}, {"ipc.recv", 3, true},
        {"env.get", 2, true}, {"env.list", 0, true}, {"system.info", 1, true},
        {"signal.send", 2, true}, {"signal.register", 2, true}, {"random.u64", 0, true},
        {"error.get", 0, true}, {"debug.log", 1, true}, {"module.load", 1, true},
        {"tty.isatty", 1, true}, {"security.chmod", 2, true}, {"gpu.compute", 2, false}
    };

    auto* i64 = ir::IntegerType::get(64);
    auto* arg = ir::ConstantInt::get(i64, 1);
    int pass = 0;
    for (const auto& c : cases) {
        const auto* spec = target.findCapability(c.name);
        if (!spec) continue;
        std::vector<ir::Value*> args(c.argc, arg);
        ir::Type* retTy = spec->returnsValue ? static_cast<ir::Type*>(i64) : static_cast<ir::Type*>(ir::VoidType::get());
        ir::ExternCallInstruction instr(retTy, args, c.name);
        if (target.validateCapability(instr, *spec) == c.expect) pass++;
    }
    std::cout << "MacOS AArch64 capability compliance: " << pass << "/" << cases.size() << "\n";
    return pass == static_cast<int>(cases.size()) ? 0 : 1;
}
