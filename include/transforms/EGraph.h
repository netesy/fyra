#pragma once

#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/IRBuilder.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <memory>
#include <string>
#include <optional>

namespace transforms {

using EClassId = uint32_t;

struct ENode {
    ir::Instruction::Opcode op = ir::Instruction::Copy;
    std::vector<EClassId> children;
    ir::Constant* constantVal = nullptr;
    ir::Value* leafVal = nullptr;

    bool isConstant() const { return constantVal != nullptr; }
    bool isLeaf() const { return leafVal != nullptr && constantVal == nullptr; }

    bool operator==(const ENode& o) const {
        return op == o.op && children == o.children && constantVal == o.constantVal && leafVal == o.leafVal;
    }
    bool operator<(const ENode& o) const {
        if (op != o.op) return op < o.op;
        if (constantVal != o.constantVal) return constantVal < o.constantVal;
        if (leafVal != o.leafVal) return leafVal < o.leafVal;
        return children < o.children;
    }
};

class UnionFind {
public:
    EClassId makeClass() {
        EClassId id = static_cast<EClassId>(parent.size());
        parent.push_back(id);
        return id;
    }

    EClassId find(EClassId id) {
        if (id >= parent.size()) return id;
        if (parent[id] == id) return id;
        return parent[id] = find(parent[id]);
    }

    EClassId merge(EClassId id1, EClassId id2) {
        EClassId r1 = find(id1);
        EClassId r2 = find(id2);
        if (r1 != r2) {
            parent[r2] = r1;
        }
        return r1;
    }

private:
    std::vector<EClassId> parent;
};

class EGraph {
public:
    EGraph() = default;

    EClassId addNode(const ENode& node);
    EClassId addValue(ir::Value* val);
    EClassId merge(EClassId id1, EClassId id2);
    void rebuild();
    bool saturate(size_t maxIterations = 10);

    EClassId find(EClassId id) { return uf.find(id); }

    // Extraction
    ir::Value* extractBest(EClassId root, ir::IRBuilder& builder, ir::BasicBlock* bb, std::map<EClassId, ir::Value*>& extractedMap);

private:
    UnionFind uf;
    std::map<ENode, EClassId> memo;
    std::map<EClassId, std::vector<ENode>> classes;

    int computeNodeCost(const ENode& node) const;
    bool applyRules();
};

} // namespace transforms
