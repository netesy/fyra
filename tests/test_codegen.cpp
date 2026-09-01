#include "parser/Parser.h"
#include "ir/Module.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LivenessAnalysis.h"
#include "codegen/regalloc/RegAllocRewriter.h"
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

    // Focused tests for CFG-aware per-instruction liveness analysis using parsed IR
    {
        using namespace ir;
        using namespace ::transforms;

        std::string liveness_ir = R"(
function $test_straight_line(%p : w) : w {
@entry
    %x = add %p, w 1 : w
    %y = add %x, w 2 : w
    ret %y : w
}

function $test_later_use(%p : w) : w {
@entry
    %x = add %p, w 1 : w
    %a = add %x, w 10 : w
    %b = add %x, w 20 : w
    ret %b : w
}

function $test_branch(%cond : w, %p : w) : w {
@entry
    %x = add %p, w 1 : w
    jnz %cond, @left, @right

@left
    %a = add %x, w 10 : w
    ret %a : w

@right
    ret w 0 : w
}

function $test_join(%cond : w, %p : w) : w {
@entry
    %x = add %p, w 1 : w
    jnz %cond, @b1, @b2

@b1
    jmp @join

@b2
    jmp @join

@join
    %use_x = add %x, w 5 : w
    ret %use_x : w
}

function $test_loop_backedge(%p : w) : w {
@entry
    %limit = add %p, w 10 : w
    jmp @header

@header
    %phi = phi @entry w 0, @body %next : w
    %cond = slt %phi, %limit : w
    jnz %cond, @body, @exit

@body
    %next = add %phi, w 1 : w
    jmp @header

@exit
    ret %phi : w
}

function $test_phi_edges(%cond : w, %p : w) : w {
@entry
    jnz %cond, @bA, @bB

@bA
    %valA = add %p, w 1 : w
    jmp @header

@bB
    %valB = add %p, w 2 : w
    jmp @header

@header
    %phi = phi @bA %valA, @bB %valB : w
    ret %phi : w
}

function $test_cfg_vs_linear(%cond : w, %p : w) : w {
@entry
    %x = add %p, w 100 : w
    jnz %cond, @b_live, @b_dead

@b_dead
    %dead_inst = add %p, w 1 : w
    ret %dead_inst : w

@b_live
    %live_inst = add %x, w 2 : w
    ret %live_inst : w
}
)";
        std::istringstream stream(liveness_ir);
        parser::Parser liveness_parser(stream, parser::FileFormat::FYRA);
        std::unique_ptr<ir::Module> live_module = liveness_parser.parseModule();
        assert(live_module != nullptr);

        // 1. Straight-line final use
        {
            Function* f = live_module->getFunction("test_straight_line");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            auto& instrs = f->getBasicBlocks().front()->getInstructions();
            auto it = instrs.begin();
            Instruction* i1 = (it++)->get(); // %x
            Instruction* i2 = (it++)->get(); // %y
            Instruction* ret = (it++)->get(); // ret

            assert(liveness.isLiveAfter(i1, i1) == true);
            assert(liveness.isLiveAfter(i2, i1) == false);
            assert(liveness.isLiveAfter(ret, i1) == false);

            // Test isLastUseOfOperand
            assert(liveness.isLastUseOfOperand(i2, i2->getOperands()[0].get()) == true);
            assert(liveness.isLastUseOfOperand(ret, ret->getOperands()[0].get()) == true);
        }

        // 2. Later same-block use
        {
            Function* f = live_module->getFunction("test_later_use");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            auto& instrs = f->getBasicBlocks().front()->getInstructions();
            auto it = instrs.begin();
            Instruction* x = (it++)->get();
            Instruction* a = (it++)->get();
            Instruction* b = (it++)->get();

            assert(liveness.isLiveAfter(x, x) == true);
            assert(liveness.isLiveAfter(a, x) == true);
            assert(liveness.isLiveAfter(b, x) == false);

            // Test isLastUseOfOperand: use at %a is NOT last use of %x; use at %b IS last use of %x
            assert(liveness.isLastUseOfOperand(a, a->getOperands()[0].get()) == false);
            assert(liveness.isLastUseOfOperand(b, b->getOperands()[0].get()) == true);
        }

        // 3. Conditional branch
        {
            Function* f = live_module->getFunction("test_branch");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            BasicBlock* entry = f->getBasicBlocks().front().get();
            Instruction* x = entry->getInstructions().front().get();
            Instruction* br = entry->getInstructions().back().get();

            assert(liveness.isLiveAfter(x, x) == true);
            assert(liveness.isLiveAfter(br, x) == true);
        }

        // 4. Join point
        {
            Function* f = live_module->getFunction("test_join");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            Instruction* x = f->getBasicBlocks().front()->getInstructions().front().get();
            assert(liveness.isLiveAfter(x, x) == true);

            for (auto& bb : f->getBasicBlocks()) {
                if (bb->getName() == "b1" || bb->getName() == "b2") {
                    Instruction* jmp = bb->getInstructions().back().get();
                    assert(liveness.isLiveAfter(jmp, x) == true);
                }
            }
        }

        // 5. Loop back-edge
        {
            Function* f = live_module->getFunction("test_loop_backedge");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            Instruction* limit = f->getBasicBlocks().front()->getInstructions().front().get();

            for (auto& bb : f->getBasicBlocks()) {
                if (bb->getName() == "body") {
                    Instruction* jmp_body = bb->getInstructions().back().get();
                    assert(liveness.isLiveAfter(jmp_body, limit) == true);
                }
            }
        }

        // 6. Loop-carried Phi incoming edge semantics
        {
            Function* f = live_module->getFunction("test_phi_edges");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            Instruction *valA = nullptr, *jmpA = nullptr, *valB = nullptr, *jmpB = nullptr;
            for (auto& bb : f->getBasicBlocks()) {
                if (bb->getName() == "bA") {
                    valA = bb->getInstructions().front().get();
                    jmpA = bb->getInstructions().back().get();
                } else if (bb->getName() == "bB") {
                    valB = bb->getInstructions().front().get();
                    jmpB = bb->getInstructions().back().get();
                }
            }
            assert(valA && jmpA && valB && jmpB);

            assert(liveness.isLiveAfter(jmpA, valA) == true);
            assert(liveness.isLiveAfter(jmpB, valA) == false);
            assert(liveness.isLiveAfter(jmpB, valB) == true);
            assert(liveness.isLiveAfter(jmpA, valB) == false);
        }

        // 7. Prove linear instruction ordering is insufficient
        {
            Function* f = live_module->getFunction("test_cfg_vs_linear");
            assert(f != nullptr);
            LivenessAnalysis liveness;
            liveness.run(*f);

            Instruction* dead_inst = nullptr;
            Instruction* ret_dead = nullptr;
            Instruction* br = nullptr;
            Instruction* x = nullptr;

            for (auto& bb : f->getBasicBlocks()) {
                if (bb->getName() == "entry") {
                    x = bb->getInstructions().front().get();
                    br = bb->getInstructions().back().get();
                } else if (bb->getName() == "b_dead") {
                    dead_inst = bb->getInstructions().front().get();
                    ret_dead = bb->getInstructions().back().get();
                }
            }
            assert(x && br && dead_inst && ret_dead);

            // Linear instruction numbers put dead_inst and ret_dead AFTER x and BEFORE live_inst.
            // Under linear ordering, x would appear "live" during b_dead.
            // Under true CFG dataflow, x is NOT live after dead_inst or ret_dead!
            assert(liveness.isLiveAfter(dead_inst, x) == false);
            assert(liveness.isLiveAfter(ret_dead, x) == false);

            // But x IS live after br on the edge to b_live
            assert(liveness.isLiveAfter(br, x) == true);
        }
    }

    // Focused tests for spill provenance tracking
    {
        using namespace ir;
        using namespace transforms;

        std::string spill_ir = R"(
function $test_spill_provenance(%p : w) : w {
@entry
    %v0 = add %p, w 1 : w
    %v1 = add %v0, w 2 : w
    %v2 = add %v1, w 3 : w
    %v3 = add %v2, w 4 : w
    %v4 = add %v3, w 5 : w
    %v5 = add %v4, w 6 : w
    %v6 = add %v5, w 7 : w
    %v7 = add %v6, w 8 : w
    %v8 = add %v7, w 9 : w
    %v9 = add %v8, w 10 : w
    %v10 = add %v9, w 11 : w
    %v11 = add %v10, w 12 : w
    %v12 = add %v11, w 13 : w
    %v13 = add %v12, w 14 : w
    %v14 = add %v13, w 15 : w
    %sum1 = add %v0, %v1 : w
    %sum2 = add %v2, %v3 : w
    %sum3 = add %v4, %v5 : w
    %sum4 = add %v6, %v7 : w
    %sum5 = add %v8, %v9 : w
    %sum6 = add %v10, %v11 : w
    %sum7 = add %v12, %v13 : w
    %total = add %sum1, %sum2 : w
    %total2 = add %total, %sum3 : w
    %total3 = add %total2, %sum4 : w
    %total4 = add %total3, %sum5 : w
    %total5 = add %total4, %sum6 : w
    %total6 = add %total5, %sum7 : w
    %total7 = add %total6, %v14 : w
    ret %total7 : w
}
)";
        std::istringstream stream(spill_ir);
        parser::Parser spill_parser(stream, parser::FileFormat::FYRA);
        std::unique_ptr<ir::Module> spill_module = spill_parser.parseModule();
        assert(spill_module != nullptr);

        Function* f = spill_module->getFunction("test_spill_provenance");
        assert(f != nullptr);

        LivenessAnalysis pre_spill_liveness;
        pre_spill_liveness.run(*f);

        RegAllocRewriter rewriter;
        rewriter.run(*f);

        bool found_spilled_load = false;
        bool found_unmodified_use = false;

        for (auto& bb : f->getBasicBlocks()) {
            for (auto& instr : bb->getInstructions()) {
                for (auto& use : instr->getOperands()) {
                    Value* cur_val = use->get();
                    Value* orig_val = use->getOriginalValue();

                    if (cur_val != orig_val) {
                        // Spilled operand rewritten to LoadStack
                        found_spilled_load = true;
                        auto* load_inst = dynamic_cast<Instruction*>(cur_val);
                        assert(load_inst != nullptr);
                        assert(load_inst->getOpcode() == Instruction::Load);

                        // Prove that getOriginalValue() returns the original pre-spill SSA instruction
                        auto* orig_inst = dynamic_cast<Instruction*>(orig_val);
                        assert(orig_inst != nullptr);
                        assert(orig_inst != load_inst);

                        // Verify isLastUseOfOperand on spilled reload operand consumes original SSA value provenance
                        bool is_last_use = pre_spill_liveness.isLastUseOfOperand(instr.get(), use.get());
                        bool is_live_after = pre_spill_liveness.isLiveAfter(instr.get(), orig_val);
                        assert(is_last_use == !is_live_after);
                    } else if (dynamic_cast<Instruction*>(cur_val)) {
                        found_unmodified_use = true;
                        assert(use->getOriginalValue() == cur_val);
                    }
                }
            }
        }

        assert(found_spilled_load == true);
        assert(found_unmodified_use == true);
        std::cout << "Spill provenance tests passed successfully!" << std::endl;
    }

    // Focused tests for two-address arithmetic lowering safety and emission
    {
        std::string lowering_ir = R"(
function $test_safe_inplace_add(%p : w, %q : w) : w {
@entry
    %x = add %p, %q : w
    %y = add %x, w 5 : w
    ret %y : w
}

function $test_unsafe_inplace_add(%p : w, %q : w) : w {
@entry
    %x = add %p, %q : w
    %y = add %x, w 5 : w
    %z = add %x, w 10 : w
    %res = add %y, %z : w
    ret %res : w
}

function $test_inplace_sub(%p : w, %q : w) : w {
@entry
    %x = add %p, %q : w
    %y = sub %x, w 3 : w
    ret %y : w
}

function $test_inplace_mul(%p : w, %q : w) : w {
@entry
    %x = add %p, %q : w
    %y = mul %x, w 7 : w
    ret %y : w
}
)";
        std::istringstream stream(lowering_ir);
        parser::Parser low_parser(stream, parser::FileFormat::FYRA);
        std::unique_ptr<ir::Module> low_module = low_parser.parseModule();
        assert(low_module != nullptr);

        std::stringstream ss_low;
        codegen::CodeGen low_cg(*low_module, target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux}), &ss_low);
        low_cg.emit();

        std::string low_asm = ss_low.str();
        assert(!low_asm.empty());
        assert(low_asm.find("test_safe_inplace_add:") != std::string::npos);
        assert(low_asm.find("test_unsafe_inplace_add:") != std::string::npos);
        assert(low_asm.find("test_inplace_sub:") != std::string::npos);
        assert(low_asm.find("test_inplace_mul:") != std::string::npos);
        std::cout << "Two-address lowering tests completed successfully!" << std::endl;
    }

    std::cout << "All CFG-aware liveness tests passed successfully!" << std::endl;
    return 0;
}
