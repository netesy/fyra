#include "transforms/InstructionSafety.h"
#include "ir/Instruction.h"
#include "ir/Type.h"

namespace transforms {

bool isSafeToSpeculativelyExecute(const ir::Instruction& instruction) {
    using O = ir::Instruction::Opcode;
    const auto integerResult = [&] {
        return instruction.getType() && instruction.getType()->isIntegerTy();
    };

    switch (instruction.getOpcode()) {
        // Fyra defines these fixed-width operations modulo 2^N, so overflow is
        // non-trapping. Comparisons and bitwise operations are also total.
        case O::Add: case O::Sub: case O::Mul: case O::Neg:
        case O::And: case O::Or: case O::Xor: case O::Not:
        case O::Ceq: case O::Cne: case O::Csle: case O::Cslt:
        case O::Csge: case O::Csgt: case O::Cule: case O::Cult:
        case O::Cuge: case O::Cugt:
            return integerResult();
        // Copy only names an already-computed value and cannot introduce work.
        case O::Copy:
            return true;
        default:
            return false;
    }
}

} // namespace transforms
