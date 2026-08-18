#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <iostream>
#include <map>
#include <vector>

int main() {
    auto target_ptr = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux}); auto& target = *target_ptr;
    using target::CapabilityDomain;

    std::map<CapabilityDomain, int> totals;
    std::map<CapabilityDomain, int> passed;
    int valid = 0;
    int total = 0;

    auto all_specs = target::CapabilityRegistry::getAll();
    auto* i64 = ir::IntegerType::get(64);
    auto* arg = ir::ConstantInt::get(i64, 1);
    for (const auto& spec : all_specs) {
        std::vector<ir::Value*> args(spec.minArgs, arg);
        ir::Type* retTy = spec.returnsValue ? static_cast<ir::Type*>(i64) : static_cast<ir::Type*>(ir::VoidType::get());
        ir::ExternCallInstruction instr(retTy, args, spec.name);
        const bool isSupported = target.validateCapability(instr, spec);
        totals[spec.domain]++;
        total++;
        if (isSupported) {
            passed[spec.domain]++;
            valid++;
        } else {
            std::cout << "Unsupported capability in Linux: " << spec.name << "\n";
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
