#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/IdempotentLoopCollapse.h"
#include <cassert>
#include <memory>
#include <iostream>

namespace {
struct Fixture {
    std::shared_ptr<ir::IRContext> context = std::make_shared<ir::IRContext>();
    ir::Module module{"idempotent-loop", context};
    ir::IRBuilder builder{context};
    Fixture() { builder.setModule(&module); }
};

enum class BodyKind { InvariantStore, InductionStore, ReadModifyWrite, PointerStore };

std::pair<ir::Function*, ir::Instruction*> buildLoop(Fixture& fixture, BodyKind kind) {
    auto* i32 = fixture.context->getIntegerType(32);
    auto* i64 = fixture.context->getIntegerType(64);
    std::vector<ir::Type*> parameters = kind == BodyKind::PointerStore
        ? std::vector<ir::Type*>{i64} : std::vector<ir::Type*>{};
    auto* function = fixture.builder.createFunction("candidate", i32, parameters);
    auto* entry = fixture.builder.createBasicBlock("entry", function);
    auto* header = fixture.builder.createBasicBlock("loop", function);
    auto* body = fixture.builder.createBasicBlock("body", function);
    auto* exit = fixture.builder.createBasicBlock("exit", function);

    fixture.builder.setInsertPoint(entry);
    ir::Value* pointer = nullptr;
    if (kind == BodyKind::PointerStore)
        pointer = function->getParameters().front().get();
    else
        pointer = fixture.builder.createAlloc(fixture.context->getConstantInt(i64, 8), i64);
    fixture.builder.createJmp(header);

    fixture.builder.setInsertPoint(header);
    auto phi = std::make_unique<ir::PhiNode>(i32, 0, nullptr, header);
    auto* induction = phi.get();
    header->getInstructions().push_back(std::move(phi));
    induction->addIncoming(fixture.context->getConstantInt(i32, 0), entry);
    auto* condition = fixture.builder.createCslt(
        induction, fixture.context->getConstantInt(i32, 10));
    fixture.builder.createBr(condition, body, exit);

    fixture.builder.setInsertPoint(body);
    ir::Value* stored = fixture.context->getConstantInt(i32, 42);
    if (kind == BodyKind::InductionStore) stored = induction;
    if (kind == BodyKind::ReadModifyWrite)
        stored = fixture.builder.createAdd(fixture.builder.createLoaduw(pointer),
                                           fixture.context->getConstantInt(i32, 1));
    fixture.builder.createStore(stored, pointer);
    auto* next = fixture.builder.createAdd(induction,
                                            fixture.context->getConstantInt(i32, 1));
    induction->addIncoming(next, body);
    fixture.builder.createJmp(header);

    fixture.builder.setInsertPoint(exit);
    fixture.builder.createRet(fixture.context->getConstantInt(i32, 0));
    transforms::CFGBuilder::run(*function);
    return {function, condition};
}

void positiveInvariantOverwriteCollapses() {
    Fixture fixture;
    auto [function, condition] = buildLoop(fixture, BodyKind::InvariantStore);
    transforms::IdempotentLoopCollapse pass;
    assert(pass.run(*function));
    auto* bound = dynamic_cast<ir::ConstantInt*>(condition->getOperands()[1]->get());
    assert(bound && bound->getValue() == 1);
}

void negativeCasesRemainRepeated() {
    for (BodyKind kind : {BodyKind::InductionStore, BodyKind::ReadModifyWrite,
                          BodyKind::PointerStore}) {
        Fixture fixture;
        auto [function, condition] = buildLoop(fixture, kind);
        transforms::IdempotentLoopCollapse pass;
        assert(!pass.run(*function));
        auto* bound = dynamic_cast<ir::ConstantInt*>(condition->getOperands()[1]->get());
        assert(bound && bound->getValue() == 10);
    }
}
} // namespace

int main() {
    positiveInvariantOverwriteCollapses();
    negativeCasesRemainRepeated();
    std::cout << "Idempotent loop collapse legality tests passed\n";
}
