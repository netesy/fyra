#ifndef FYRA_WASM32_H
#define FYRA_WASM32_H
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/wasm32/Wasm32Architecture.h"
#include "target/os/wasi/WASIOS.h"

namespace codegen {
namespace target {
class Wasm32 : public CompositeTargetInfo {
public:
    Wasm32() : CompositeTargetInfo(
        std::make_unique<Wasm32Architecture>(),
        std::make_unique<WASIOS>()
    ) {}

    void emitTypeSection(CodeGen& cg) { static_cast<Wasm32Architecture*>(architecture.get())->emitTypeSection(cg); }
    void emitFunctionSection(CodeGen& cg) { static_cast<Wasm32Architecture*>(architecture.get())->emitFunctionSection(cg); }
    void emitExportSection(CodeGen& cg) { static_cast<Wasm32Architecture*>(architecture.get())->emitExportSection(cg); }
    void emitCodeSection(CodeGen& cg) { static_cast<Wasm32Architecture*>(architecture.get())->emitCodeSection(cg); }
};
}
}
#endif
