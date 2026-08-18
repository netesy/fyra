#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <vector>
#include <iostream>

int main() {
    auto target_ptr = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Windows}); auto& target = *target_ptr;
    struct Case { const char* name; int argc; bool expect; };
    auto all_specs = target::CapabilityRegistry::getAll();
    auto* i64 = ir::IntegerType::get(64);
    auto* arg = ir::ConstantInt::get(i64, 1);
    int pass = 0;
    for (const auto& spec : all_specs) {
        std::vector<ir::Value*> args(spec.minArgs, arg);
        ir::Type* retTy = spec.returnsValue ? static_cast<ir::Type*>(i64) : static_cast<ir::Type*>(ir::VoidType::get());
        ir::ExternCallInstruction instr(retTy, args, spec.name);
        if (target.validateCapability(instr, spec)) pass++;
        else std::cout << "Failed capability: " << spec.name << "\n";
    }

    std::cout << "Windows capability compliance: " << pass << "/" << all_specs.size() << "\n";
    return pass == static_cast<int>(all_specs.size()) ? 0 : 1;
}
