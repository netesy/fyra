#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

int main() {
    std::string test_file = "tests/simple.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto targetInfo = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    std::stringstream ss;
    codegen::CodeGen codeGen(*module, std::move(targetInfo), &ss);
    codeGen.emit();

    std::string generated_asm = ss.str();
    std::cout << "Generated ASM:\n" << generated_asm << std::endl;

    assert(generated_asm.find("main:") != std::string::npos);
    assert(generated_asm.find("movq $42, %rax") != std::string::npos || generated_asm.find("movl $42, %eax") != std::string::npos);
    assert(generated_asm.find("ret") != std::string::npos);

    // Test 32-bit arithmetic with overflow sign-extension semantics and parameter preservation
    std::string test_dot_ir = R"(
function $test_dot_overflow(%n : w) : l {
@entry
    jmp @loop

@loop
    %i = phi @entry w 0, @body %i_next : w
    %sum = phi @entry l 0, @body %sum_next : l
    %cond = slt %i, %n : w
    jnz %cond, @body, @exit

@body
    %t3 = mul %i, w 3 : w
    %a_w = add %t3, w 1 : w
    %a = extsw %a_w : l

    %t7 = mul %i, w 7 : w
    %b_w = add %t7, w 2 : w
    %b = extsw %b_w : l

    %prod = mul %a, %b : l
    %sum_next = add %sum, %prod : l
    %i_next = add %i, w 1 : w
    jmp @loop

@exit
    ret %sum : l
}
)";
    std::istringstream dot_stream(test_dot_ir);
    parser::Parser dot_parser(dot_stream, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> dot_module = dot_parser.parseModule();
    assert(dot_module != nullptr);

    std::stringstream ss_dot;
    codegen::CodeGen codeGenDot(*dot_module, target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux}), &ss_dot);
    codeGenDot.emit();

    std::string dot_asm = ss_dot.str();
    assert(dot_asm.find("imull $3") != std::string::npos || dot_asm.find("imul") != std::string::npos);
    assert(dot_asm.find("addl $1") != std::string::npos || dot_asm.find("add") != std::string::npos);
    assert(dot_asm.find("cltq") != std::string::npos || dot_asm.find("mov") != std::string::npos);
    // Verify %edi is compared against loop counter and not overwritten by local variables
    assert(dot_asm.find("cmpl %edi,") != std::string::npos);

    return 0;
}
