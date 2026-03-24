#pragma once

#include "User.h"
#include "Syscall.h"
#include <vector>

namespace ir {

class BasicBlock; // Forward declaration

class Instruction : public User {
public:
    enum Opcode {
        // Terminator Instructions
        Ret,
        Jmp,
        Jz,
        Jnz,
        Br,
        Hlt,

        // Binary Operators
        Add,
        Sub,
        Mul,
        Div,
        Udiv,
        Rem,
        Urem,
        And,
        Or,
        Xor,
        Shl,
        Shr,
        Sar,
        
        // Floating-point Binary Operators
        FAdd,
        FSub,
        FMul,
        FDiv,
        FRem,
        
        // Vector/SIMD Instructions
        VAdd,          // Vector addition
        VSub,          // Vector subtraction
        VMul,          // Vector multiplication
        VDiv,          // Vector division
        VFAdd,         // Vector floating-point addition
        VFSub,         // Vector floating-point subtraction
        VFMul,         // Vector floating-point multiplication
        VFDiv,         // Vector floating-point division
        VAnd,          // Vector bitwise AND
        VOr,           // Vector bitwise OR
        VXor,          // Vector bitwise XOR
        VShl,          // Vector bitwise shift left
        VShr,          // Vector bitwise shift right
        VNot,          // Vector bitwise NOT
        VLoad,         // Vector load
        VStore,        // Vector store
        VBroadcast,    // Broadcast scalar to vector
        VExtract,      // Extract element from vector
        VInsert,       // Insert element into vector
        VShuffle,      // Vector shuffle
        VCmp,          // Vector comparison
        VSelect,       // Vector select based on mask
        VGather,       // Vector gather from memory
        VScatter,      // Vector scatter to memory
        VHAdd,         // Horizontal vector addition
        VHSub,         // Horizontal vector subtraction
        VHMul,         // Horizontal vector multiplication
        VHAnd,         // Horizontal vector bitwise AND
        VHOr,          // Horizontal vector bitwise OR
        VHXor,         // Horizontal vector bitwise XOR
        VMin,          // Vector minimum
        VMax,          // Vector maximum
        VFMin,         // Vector floating-point minimum
        VFMax,         // Vector floating-point maximum
        
        // Fused Instructions
        FMA,           // Fused multiply-add (a * b + c)
        FMS,           // Fused multiply-subtract (a * b - c)
        FNMA,          // Fused negative multiply-add (-(a * b) + c)
        FNMS,          // Fused negative multiply-subtract (-(a * b) - c)

        // Unary Operators
        Neg,
        Not,

        // Memory operators
        Alloc,
        Alloc4,
        Alloc16,
        Load,
        Loadd,
        Loads,
        Loadl,
        Loaduw,
        Loadsh,
        Loaduh,
        Loadsb,
        Loadub,
        Store,
        Stored,
        Stores,
        Storel,
        Storeh,
        Storeb,

        // Other
        Call,
        Phi,
        Copy,

        // Comparisons
        Ceq, Cne, Csle, Cslt, Csge, Csgt, Cule, Cult, Cuge, Cugt,
        Ceqf, Cnef, Cle, Clt, Cge, Cgt, Co, Cuo,

        Blit,

        // Conversions
        ExtUB,
        ExtUH,
        ExtUW,
        ExtSB,
        ExtSH,
        ExtSW,
        ExtS,
        TruncD,
        SWtoF,
        UWtoF,
        DToSI,
        DToUI,
        SToSI,
        SToUI,
        Sltof,
        Ultof,
        Cast,

        VAStart,
        VAArg,
        Syscall
    };

    Instruction(Type* ty, Opcode op, const std::vector<Value*>& operands, BasicBlock* parent = nullptr);
    virtual ~Instruction() = default;

    Opcode getOpcode() const { return opcode; }
    BasicBlock* getParent() const { return parent; }
    void setParent(BasicBlock* p) { parent = p; }

    void print(std::ostream& os) const override;

    // Register allocation support
    int getPhysicalRegister() const { return physicalReg; }
    void setPhysicalRegister(int reg) { physicalReg = reg; }
    bool hasPhysicalRegister() const { return physicalReg != -1; }

private:
    Opcode opcode;
    BasicBlock* parent;
    int physicalReg = -1;
};

class SyscallInstruction : public Instruction {
public:
    SyscallInstruction(Type* ty, const std::vector<Value*>& operands, SyscallId id, BasicBlock* parent = nullptr);

    SyscallId getSyscallId() const { return id; }

    void print(std::ostream& os) const override;

private:
    SyscallId id;
};

} // namespace ir
