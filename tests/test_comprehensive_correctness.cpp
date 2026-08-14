#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Validator.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "transforms/DominanceFrontier.h"
#include "transforms/PhiInsertion.h"
#include "transforms/SSARenamer.h"
#include "transforms/Mem2Reg.h"
#include "transforms/CopyElimination.h"
#include "transforms/GVN.h"
#include "transforms/SCCP.h"
#include "transforms/DeadInstructionElimination.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/ControlFlowSimplification.h"
#include "codegen/regalloc/RegAllocRewriter.h"
#include "codegen/abi/ABIAnalysis.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

int main() {
    std::string test_file = "tests/comprehensive_correctness.fyra";
    std::ifstream input(test_file);
    if (!input.good()) {
        std::cerr << "Could not open test file: " << test_file << std::endl;
        return 1;
    }

    parser::Parser parser(input);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    // Validate IR Correctness
    std::vector<std::string> irErrors;
    if (!ir::Validator::validateModule(*module, irErrors)) {
        std::cerr << "IR Validation Failed! Detected " << irErrors.size() << " errors:\n";
        for (const auto& err : irErrors) {
            std::cerr << "  - " << err << "\n";
        }
        return 1;
    }
    std::cout << "IR validation passed successfully!" << std::endl;

    // Run Optimization and RegAlloc
    auto error_reporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);
    for (auto& func : module->getFunctions()) {
        transforms::CFGBuilder::run(*func);
        transforms::DominatorTree domTree; domTree.run(*func);
        transforms::DominanceFrontier domFrontier; domFrontier.run(*func, domTree);
        transforms::PhiInsertion phiInserter; phiInserter.run(*func, domFrontier);
        transforms::SSARenamer ssaRenamer; ssaRenamer.run(*func, domTree);
        transforms::Mem2Reg mem2reg; mem2reg.run(*func);

        transforms::SCCP sccp(error_reporter); sccp.run(*func);
        transforms::CopyElimination copy_elim; copy_elim.run(*func);
        transforms::GVN gvn; gvn.run(*func);
        transforms::ControlFlowSimplification cfg_simpl; cfg_simpl.run(*func);
        transforms::DeadInstructionElimination dce(error_reporter); dce.run(*func);

        transforms::RegAllocRewriter rewriter;
        rewriter.run(*func);
    }

    // Generate x64 Linux Assembly
    auto targetInfo = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "--- Generated Correctness Assembly ---\n" << generated_asm << "\n";

    // Verification check for unsigned comparisons
    assert(generated_asm.find("setb") != std::string::npos);
    assert(generated_asm.find("setbe") != std::string::npos);
    assert(generated_asm.find("seta") != std::string::npos);
    assert(generated_asm.find("setae") != std::string::npos);

    // Verification check for casts/sign extension
    assert(generated_asm.find("extsb") != std::string::npos || generated_asm.find("movsbq") != std::string::npos);
    assert(generated_asm.find("extub") != std::string::npos || generated_asm.find("movzbq") != std::string::npos);

    std::cout << "All comprehensive correctness tests passed successfully!" << std::endl;
    return 0;
}
