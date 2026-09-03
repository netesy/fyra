#pragma once

#include "transforms/TransformPass.h"
#include "transforms/ErrorReporter.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include <memory>

namespace transforms {

class DivisionStrengthReduction : public TransformPass {
public:
    explicit DivisionStrengthReduction(std::shared_ptr<ErrorReporter> errorReporter = nullptr)
        : TransformPass("DivisionStrengthReduction", errorReporter) {}

protected:
    bool performTransformation(ir::Function& func) override;

private:
    bool processInstruction(ir::Instruction* instr, ir::IRBuilder& builder, ir::Function& func);

    // Magic multiplier calculation helpers
    struct UnsignedMagic {
        uint64_t magic;
        bool add_indicator;
        uint32_t shift;
    };

    struct SignedMagic {
        int64_t magic;
        uint32_t shift;
    };

    UnsignedMagic computeUnsignedMagic32(uint32_t d);
    SignedMagic computeSignedMagic32(int32_t d);
};

} // namespace transforms
