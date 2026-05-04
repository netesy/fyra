#include "target/os/wasi/WASIOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace target {

void WASIOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    if (spec.id == CapabilityId::IO_WRITE) func = "fd_write";
    else if (spec.id == CapabilityId::IO_READ) func = "fd_read";

    if (!func.empty()) {
        std::vector<ir::Value*> args;
        for (auto& op : i.getOperands()) args.push_back(op->get());
        arch.emitNativeLibraryCall(cg, func, args);
    }
}

void WASIOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::PROCESS_EXIT) {
        std::vector<ir::Value*> args;
        for (auto& op : i.getOperands()) args.push_back(op->get());
        arch.emitNativeLibraryCall(cg, "proc_exit", args);
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
