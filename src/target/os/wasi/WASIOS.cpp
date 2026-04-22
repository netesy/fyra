#include "target/os/wasi/WASIOS.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include <ostream>

namespace codegen {
namespace target {

void WASIOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::IO_WRITE) {
             *os << "  call $fd_write\n";
        } else if (spec.id == CapabilityId::IO_READ) {
             *os << "  call $fd_read\n";
        }
    }
}

void WASIOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::PROCESS_EXIT) {
             *os << "  call $proc_exit\n";
        }
    }
}

void WASIOS::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "(module\n";
        *os << "  (import \"wasi_unstable\" \"fd_write\" (func $fd_write (param i32 i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"fd_read\" (func $fd_read (param i32 i32 i32 i32) (result i32)))\n";
        *os << "  (import \"wasi_unstable\" \"proc_exit\" (func $proc_exit (param i32)))\n";
        *os << "  (memory 1)\n";
        *os << "  (global $__heap_ptr (mut i32) (i32.const 1024))\n";
    }
}

}
}
