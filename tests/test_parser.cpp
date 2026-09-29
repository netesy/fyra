#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "ir/FunctionType.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <memory>
#include <iostream>

int main() {
    std::string test_file = "tests/simple.fyra";
    std::ifstream input(test_file);
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    ir::Function* func = module->getFunction("main");
    assert(func != nullptr);

    assert(func->getBasicBlocks().size() == 1);
    ir::BasicBlock* bb = func->getBasicBlocks().front().get();
    assert(bb != nullptr);

    // There should be one instruction: ret i32 42
    assert(bb->getInstructions().size() == 1);
    ir::Instruction* instr = bb->getInstructions().front().get();
    assert(instr->getOpcode() == ir::Instruction::Ret);
    assert(instr->getOperands().size() == 1);
    ir::Value* retVal = instr->getOperands()[0]->get();
    ir::ConstantInt* constInt = dynamic_cast<ir::ConstantInt*>(retVal);
    assert(constInt != nullptr);
    assert(constInt->getValue() == 42);

    std::cout << "Parser test passed!" << std::endl;

    // Unit tests for canonical type parsing, printing, invalid syntax rejection, and round-trips
    {
        std::string src =
            "export function $type_test("
            "i8 %a, i16 %b, i32 %c, i64 %d, "
            "u8 %e, u16 %f, u32 %g, u64 %h, "
            "f32 %i, f64 %j, bool %k, "
            "v16i8 %v1, v8i16 %v2, v4i32 %v3, v2i64 %v4, "
            "v16u8 %v5, v8u16 %v6, v4u32 %v7, v2u64 %v8, "
            "v4f32 %v9, v2f64 %v10) : void {\n"
            "    ret\n"
            "}\n";
        std::stringstream ss(src);
        parser::Parser p(ss, parser::FileFormat::FYRA);
        auto m = p.parseModule();
        assert(m != nullptr);
        auto* fn = m->getFunction("type_test");
        assert(fn != nullptr);

        // Verify type printing and round-trips
        auto& params = fn->getParameters();
        for (auto& param : params) {
            std::string typeStr = param->getType()->toString();
            std::string paramTestSrc = "export function $pt(%x : " + typeStr + ") : " + typeStr + " { ret %x }\n";
            std::stringstream pss(paramTestSrc);
            parser::Parser p2(pss, parser::FileFormat::FYRA);
            auto m2 = p2.parseModule();
            assert(m2 != nullptr);
            auto* fn2 = m2->getFunction("pt");
            assert(fn2 != nullptr);
            auto* ft2 = dynamic_cast<ir::FunctionType*>(fn2->getType());
            assert(ft2 != nullptr);
            assert(ft2->getReturnType()->toString() == typeStr);
        }

        // Test invalid types rejection
        std::vector<std::string> invalidTypes = {
            "i0", "i7", "i128", "f16", "v0i32", "v4", "v4foo", "w", "l", "s", "d", "v4s", "v4w"
        };
        for (const auto& inv : invalidTypes) {
            std::string invSrc = "export function $bad(" + inv + " %x) : void { ret }\n";
            std::stringstream pss(invSrc);
            parser::Parser pBad(pss, parser::FileFormat::FYRA);
            bool caught = false;
            try {
                pBad.parseModule();
            } catch (const std::exception& e) {
                caught = true;
            }
            assert(caught && "Parser should reject invalid/legacy type");
        }
    }

    std::cout << "Canonical type parsing and round-trip unit tests passed!" << std::endl;

    return 0;
}
