#pragma once

namespace ir { class Instruction; }

namespace transforms {

// Returns true only when evaluating the instruction on a path where it did not
// originally execute cannot trap, access memory, perform I/O, mutate state, or
// otherwise change observable program behaviour. Unknown opcodes are unsafe.
bool isSafeToSpeculativelyExecute(const ir::Instruction& instruction);

} // namespace transforms
