#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <stdexcept>

namespace target {
    class TargetInfo; // Forward declaration
}

namespace ir {

class IRContext; // Forward declaration

class Type {
public:
    enum TypeID {
        // Primitive types
        VoidTyID,
        LabelTyID,
        IntegerTyID,
        FloatTyID,
        DoubleTyID,
        // Vector types
        VectorTyID,
        // Aggregate types
        StructTyID,
        UnionTyID,
        ArrayTyID,
        PointerTyID,
        FunctionTyID,
    };

    virtual ~Type() = default;
    TypeID getTypeID() const { return id; }

    bool isFloatTy() const { return getTypeID() == FloatTyID; }
    bool isDoubleTy() const { return getTypeID() == DoubleTyID; }
    bool isIntegerTy() const { return getTypeID() == IntegerTyID; }
    bool isVoidTy() const { return getTypeID() == VoidTyID; }
    bool isPointerTy() const { return getTypeID() == PointerTyID; }
    bool isStructTy() const { return getTypeID() == StructTyID; }
    bool isArrayTy() const { return getTypeID() == ArrayTyID; }
    bool isVectorTy() const { return getTypeID() == VectorTyID; }
    
    // Target-aware methods
    virtual size_t getSize() const = 0; // Size in bytes
    virtual size_t getAlignment() const = 0; // Alignment in bytes
    virtual size_t getTargetSize(const target::TargetInfo* target) const;
    virtual size_t getTargetAlignment(const target::TargetInfo* target) const;
    
    // Type classification for ABI
    virtual bool isFloatingPoint() const { return isFloatTy() || isDoubleTy(); }
    virtual bool isInteger() const { return isIntegerTy(); }
    virtual bool isAggregate() const { return isStructTy() || isArrayTy(); }
    virtual bool isVector() const { return isVectorTy(); }
    virtual bool isSIMDType() const { return isVectorTy(); }
    
    // Utility methods
    virtual std::string toString() const = 0;
    static Type* getUnionTy(const std::vector<Type*>& members);

protected:
    Type(TypeID id) : id(id) {}

private:
    TypeID id;
};

class IntegerType : public Type {
public:
    IntegerType(unsigned bitwidth) : Type(Type::IntegerTyID), bitwidth(bitwidth) {}

    static IntegerType* get(unsigned bitwidth);
    static IntegerType* get(IRContext& ctx, unsigned bitwidth);

    unsigned getBitwidth() const { return bitwidth; }
    bool isSigned() const { return true; } // Default to signed
    
    // Override Type methods
    size_t getSize() const override { return (bitwidth + 7) / 8; }
    size_t getAlignment() const override {
        if (bitwidth <= 8) return 1;
        if (bitwidth <= 16) return 2;
        if (bitwidth <= 32) return 4;
        return 8;
    }
    
    std::string toString() const override {
        return "i" + std::to_string(bitwidth);
    }

private:
    unsigned bitwidth;
};

class FloatType : public Type {
public:
    FloatType() : Type(Type::FloatTyID) {}
    static FloatType* get();
    static FloatType* get(IRContext& ctx);
    
    // Override Type methods
    size_t getSize() const override { return 4; }
    size_t getAlignment() const override { return 4; }
    std::string toString() const override { return "float"; }
};

class DoubleType : public Type {
public:
    DoubleType() : Type(Type::DoubleTyID) {}
    static DoubleType* get();
    static DoubleType* get(IRContext& ctx);
    
    // Override Type methods
    size_t getSize() const override { return 8; }
    size_t getAlignment() const override { return 8; }
    std::string toString() const override { return "double"; }
};

class VoidType : public Type {
public:
    VoidType() : Type(Type::VoidTyID) {}
    static VoidType* get();
    static VoidType* get(IRContext& ctx);
    
    // Override Type methods
    size_t getSize() const override { return 0; }
    size_t getAlignment() const override { return 1; }
    std::string toString() const override { return "void"; }
};

class LabelType : public Type {
public:
    LabelType() : Type(Type::LabelTyID) {}
    static LabelType* get();
    static LabelType* get(IRContext& ctx);
    
    // Override Type methods
    size_t getSize() const override { return 0; }
    size_t getAlignment() const override { return 1; }
    std::string toString() const override { return "label"; }
};

class PointerType : public Type {
public:
    PointerType(Type* elementType, unsigned addressSpace = 0)
        : Type(Type::PointerTyID), elementType(elementType), addressSpace(addressSpace) {}
    
    static PointerType* get(Type* elementType, unsigned addressSpace = 0);
    static PointerType* get(IRContext& ctx, Type* elementType, unsigned addressSpace = 0);
    
    Type* getElementType() const { return elementType; }
    unsigned getAddressSpace() const { return addressSpace; }
    
    // Override Type methods
    size_t getSize() const override { return 8; } // Assume 64-bit pointers
    size_t getAlignment() const override { return 8; }
    std::string toString() const override {
        return elementType->toString() + "*";
    }

private:
    Type* elementType;
    unsigned addressSpace;
};

class StructType : public Type {
public:
    // For creating a new struct type
    StructType(const std::string& name, std::vector<Type*> elements, bool isOpaque = false)
        : Type(Type::StructTyID), name(name), elements(elements), opaque(isOpaque) {}

    // For forward declaration
    static StructType* create(const std::string& name);

    void setBody(std::vector<Type*> elements, bool isOpaque = false);

    const std::string& getName() const { return name; }
    const std::vector<Type*>& getElements() const { return elements; }
    bool isOpaque() const { return opaque; }
    
    // Override Type methods
    size_t getSize() const override {
        size_t totalSize = 0;
        for (Type* element : elements) {
            // Add padding for alignment
            size_t align = element->getAlignment();
            totalSize = (totalSize + align - 1) & ~(align - 1);
            totalSize += element->getSize();
        }
        return totalSize;
    }
    
    size_t getAlignment() const override {
        size_t maxAlign = 1;
        for (Type* element : elements) {
            maxAlign = std::max(maxAlign, element->getAlignment());
        }
        return maxAlign;
    }
    
    std::string toString() const override {
        return "struct " + name;
    }

private:
    std::string name;
    std::vector<Type*> elements;
    bool opaque;
};

class UnionType : public Type {
public:
    UnionType(const std::string& name, std::vector<Type*> elements)
        : Type(Type::UnionTyID), name(name), elements(elements) {}

    static UnionType* create(const std::string& name, std::vector<Type*> elements);

    const std::string& getName() const { return name; }
    const std::vector<Type*>& getElements() const { return elements; }
    
    // Override Type methods
    size_t getSize() const override {
        size_t maxSize = 0;
        for (Type* element : elements) {
            maxSize = std::max(maxSize, element->getSize());
        }
        return maxSize;
    }
    
    size_t getAlignment() const override {
        size_t maxAlign = 1;
        for (Type* element : elements) {
            maxAlign = std::max(maxAlign, element->getAlignment());
        }
        return maxAlign;
    }
    
    std::string toString() const override {
        return "union " + name;
    }

private:
    std::string name;
    std::vector<Type*> elements;
};

class ArrayType : public Type {
public:
    ArrayType(Type* elementType, uint64_t numElements)
        : Type(Type::ArrayTyID), elementType(elementType), numElements(numElements) {}

    static ArrayType* get(Type* elementType, uint64_t numElements);

    Type* getElementType() const { return elementType; }
    uint64_t getNumElements() const { return numElements; }
    
    // Override Type methods
    size_t getSize() const override {
        return elementType->getSize() * numElements;
    }
    
    size_t getAlignment() const override {
        return elementType->getAlignment();
    }
    
    std::string toString() const override {
        return "[" + std::to_string(numElements) + " x " + elementType->toString() + "]";
    }

private:
    Type* elementType;
    uint64_t numElements;
};

class VectorType : public Type {
public:
    VectorType(Type* elementType, unsigned numElements)
        : Type(Type::VectorTyID), elementType(elementType), numElements(numElements) {
        // Validate element type for vectors
        if (!elementType->isIntegerTy() && !elementType->isFloatTy() && !elementType->isDoubleTy()) {
            throw std::invalid_argument("Vector elements must be integer or floating-point types");
        }
    }

    static VectorType* get(Type* elementType, unsigned numElements);

    Type* getElementType() const { return elementType; }
    unsigned getNumElements() const { return numElements; }
    
    // Vector-specific methods
    unsigned getBitWidth() const {
        return elementType->getSize() * 8 * numElements;
    }
    
    bool isIntegerVector() const {
        return elementType->isIntegerTy();
    }
    
    bool isFloatingPointVector() const {
        return elementType->isFloatTy() || elementType->isDoubleTy();
    }
    
    unsigned getElementBitWidth() const {
        return elementType->getSize() * 8;
    }
    
    // Check if this vector type is compatible with target SIMD capabilities
    bool isCompatibleWithSIMD(unsigned targetVectorWidth) const {
        return getBitWidth() <= targetVectorWidth;
    }
    
    // Override Type methods
    size_t getSize() const override {
        return elementType->getSize() * numElements;
    }
    
    size_t getAlignment() const override {
        // Vector alignment is typically the size of the vector, up to a maximum
        size_t vectorSize = getSize();
        if (vectorSize <= 16) return vectorSize;
        return 16; // Max alignment for most architectures
    }
    
    // Override classification methods
    bool isFloatingPoint() const override {
        return elementType->isFloatingPoint();
    }
    
    bool isInteger() const override {
        return elementType->isInteger();
    }
    
    bool isSIMDType() const override {
        return true;
    }
    
    std::string toString() const override {
        return "<" + std::to_string(numElements) + " x " + elementType->toString() + ">";
    }

private:
    Type* elementType;
    unsigned numElements;
};

} // namespace ir
