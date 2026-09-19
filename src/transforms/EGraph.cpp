#include "transforms/EGraph.h"
#include "ir/Use.h"
#include "ir/SIMDInstruction.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace transforms {

EClassId EGraph::addNode(const ENode& node) {
    ENode canonicalNode = node;
    for (size_t i = 0; i < canonicalNode.children.size(); ++i) {
        canonicalNode.children[i] = uf.find(canonicalNode.children[i]);
    }

    auto it = memo.find(canonicalNode);
    if (it != memo.end()) {
        return uf.find(it->second);
    }

    EClassId id = uf.makeClass();
    memo[canonicalNode] = id;
    classes[id].push_back(canonicalNode);
    return id;
}

EClassId EGraph::addValue(ir::Value* val) {
    if (!val) return uf.makeClass();

    if (auto* c = dynamic_cast<ir::Constant*>(val)) {
        ENode n;
        n.constantVal = c;
        return addNode(n);
    }

    if (auto* inst = dynamic_cast<ir::Instruction*>(val)) {
        ENode n;
        n.op = inst->getOpcode();
        for (const auto& opUse : inst->getOperands()) {
            if (opUse && opUse->get()) {
                n.children.push_back(addValue(opUse->get()));
            }
        }
        return addNode(n);
    }

    ENode n;
    n.leafVal = val;
    return addNode(n);
}

EClassId EGraph::merge(EClassId id1, EClassId id2) {
    EClassId r1 = uf.find(id1);
    EClassId r2 = uf.find(id2);
    if (r1 == r2) return r1;

    EClassId newId = uf.merge(r1, r2);
    EClassId otherId = (newId == r1) ? r2 : r1;

    auto& nodes1 = classes[newId];
    auto& nodes2 = classes[otherId];
    nodes1.insert(nodes1.end(), nodes2.begin(), nodes2.end());
    classes.erase(otherId);

    return newId;
}

void EGraph::rebuild() {
    std::map<ENode, EClassId> newMemo;
    for (auto& [eclass, nodes] : classes) {
        EClassId canonicalClass = uf.find(eclass);
        std::vector<ENode> canonicalNodes;
        for (auto node : nodes) {
            for (size_t i = 0; i < node.children.size(); ++i) {
                node.children[i] = uf.find(node.children[i]);
            }
            if (newMemo.count(node)) {
                uf.merge(canonicalClass, newMemo[node]);
            } else {
                newMemo[node] = canonicalClass;
                canonicalNodes.push_back(node);
            }
        }
        nodes = canonicalNodes;
    }
}

static bool isPowerOfTwo(uint64_t v, int& shift) {
    if (v == 0 || (v & (v - 1)) != 0) return false;
    shift = 0;
    while ((v >> shift) > 1) shift++;
    return true;
}

bool EGraph::applyRules() {
    bool changed = false;
    std::vector<std::pair<EClassId, ENode>> newNodes;
    std::vector<std::pair<EClassId, EClassId>> merges;

    for (auto& [eclass, nodes] : classes) {
        EClassId cid = uf.find(eclass);
        for (const auto& node : nodes) {
            if (node.isConstant() || node.isLeaf()) continue;

            // Peepholes: Add(x, 0) -> x, Sub(x, 0) -> x, Mul(x, 1) -> x
            if (node.children.size() >= 2) {
                EClassId child0 = uf.find(node.children[0]);
                EClassId child1 = uf.find(node.children[1]);

                // Check for constant child
                ir::ConstantInt* c0 = nullptr;
                ir::ConstantInt* c1 = nullptr;
                for (const auto& n : classes[child0]) if (n.constantVal) c0 = dynamic_cast<ir::ConstantInt*>(n.constantVal);
                for (const auto& n : classes[child1]) if (n.constantVal) c1 = dynamic_cast<ir::ConstantInt*>(n.constantVal);

                if (node.op == ir::Instruction::Add && c1 && c1->getValue() == 0) {
                    merges.push_back({cid, child0});
                    changed = true;
                }
                if (node.op == ir::Instruction::Sub && c1 && c1->getValue() == 0) {
                    merges.push_back({cid, child0});
                    changed = true;
                }
                if (node.op == ir::Instruction::Mul && c1 && c1->getValue() == 1) {
                    merges.push_back({cid, child0});
                    changed = true;
                }

                // Sub(x, x) -> 0, Xor(x, x) -> 0
                if ((node.op == ir::Instruction::Sub || node.op == ir::Instruction::Xor) && child0 == child1) {
                    ENode zeroNode;
                    zeroNode.constantVal = ir::ConstantInt::get(ir::IntegerType::get(32), 0);
                    EClassId zeroC = addNode(zeroNode);
                    merges.push_back({cid, zeroC});
                    changed = true;
                }

                // Division Strength Reduction: Udiv(x, 2^k) -> Shr(x, k), Mul(x, 2^k) -> Shl(x, k)
                int shift = 0;
                if (c1 && isPowerOfTwo(c1->getValue(), shift)) {
                    if (node.op == ir::Instruction::Udiv || node.op == ir::Instruction::Shr) {
                        ENode shiftNode;
                        shiftNode.op = ir::Instruction::Shr;
                        shiftNode.children = {child0, addNode(ENode{ir::Instruction::Copy, {}, ir::ConstantInt::get(ir::IntegerType::get(32), shift), nullptr})};
                        EClassId shiftC = addNode(shiftNode);
                        merges.push_back({cid, shiftC});
                        changed = true;
                    } else if (node.op == ir::Instruction::Div || node.op == ir::Instruction::Sar) {
                        ENode shiftNode;
                        shiftNode.op = ir::Instruction::Sar;
                        shiftNode.children = {child0, addNode(ENode{ir::Instruction::Copy, {}, ir::ConstantInt::get(ir::IntegerType::get(32), shift), nullptr})};
                        EClassId shiftC = addNode(shiftNode);
                        merges.push_back({cid, shiftC});
                        changed = true;
                    } else if (node.op == ir::Instruction::Mul) {
                        ENode shlNode;
                        shlNode.op = ir::Instruction::Shl;
                        shlNode.children = {child0, addNode(ENode{ir::Instruction::Copy, {}, ir::ConstantInt::get(ir::IntegerType::get(32), shift), nullptr})};
                        EClassId shlC = addNode(shlNode);
                        merges.push_back({cid, shlC});
                        changed = true;
                    } else if (node.op == ir::Instruction::Urem) {
                        uint64_t maskVal = (1ULL << shift) - 1ULL;
                        ENode andNode;
                        andNode.op = ir::Instruction::And;
                        andNode.children = {child0, addNode(ENode{ir::Instruction::Copy, {}, ir::ConstantInt::get(ir::IntegerType::get(32), maskVal), nullptr})};
                        EClassId andC = addNode(andNode);
                        merges.push_back({cid, andC});
                        changed = true;
                    }
                }

                // SIMD Lane Packing: VAdd(VBroadcast(a), VBroadcast(b)) -> VBroadcast(Add(a, b))
                if (node.op == ir::Instruction::VAdd || node.op == ir::Instruction::VSub || node.op == ir::Instruction::VMul) {
                    bool bcast0 = false, bcast1 = false;
                    EClassId scalar0 = 0, scalar1 = 0;
                    for (const auto& n : classes[child0]) {
                        if (n.op == ir::Instruction::VBroadcast && !n.children.empty()) {
                            bcast0 = true; scalar0 = n.children[0]; break;
                        }
                    }
                    for (const auto& n : classes[child1]) {
                        if (n.op == ir::Instruction::VBroadcast && !n.children.empty()) {
                            bcast1 = true; scalar1 = n.children[0]; break;
                        }
                    }

                    if (bcast0 && bcast1) {
                        ir::Instruction::Opcode scalarOp = ir::Instruction::Add;
                        if (node.op == ir::Instruction::VSub) scalarOp = ir::Instruction::Sub;
                        if (node.op == ir::Instruction::VMul) scalarOp = ir::Instruction::Mul;

                        ENode scalarAdd;
                        scalarAdd.op = scalarOp;
                        scalarAdd.children = {scalar0, scalar1};
                        EClassId scalarAddC = addNode(scalarAdd);

                        ENode packedBcast;
                        packedBcast.op = ir::Instruction::VBroadcast;
                        packedBcast.children = {scalarAddC};
                        EClassId packedC = addNode(packedBcast);

                        merges.push_back({cid, packedC});
                        changed = true;
                    }
                }
            }
        }
    }

    for (const auto& [m1, m2] : merges) {
        merge(m1, m2);
    }

    return changed;
}

bool EGraph::saturate(size_t maxIterations) {
    bool changed = false;
    for (size_t iter = 0; iter < maxIterations; ++iter) {
        if (!applyRules()) break;
        rebuild();
        changed = true;
    }
    return changed;
}

int EGraph::computeNodeCost(const ENode& node) const {
    if (node.isConstant() || node.isLeaf()) return 0;
    int cost = 1;
    switch (node.op) {
        case ir::Instruction::Shr: case ir::Instruction::Sar: case ir::Instruction::Shl:
        case ir::Instruction::And: case ir::Instruction::Or: case ir::Instruction::Xor:
            cost = 1; break;
        case ir::Instruction::Add: case ir::Instruction::Sub:
            cost = 1; break;
        case ir::Instruction::Mul: case ir::Instruction::VMul:
            cost = 3; break;
        case ir::Instruction::Div: case ir::Instruction::Udiv:
            cost = 20; break;
        default:
            cost = 2; break;
    }
    return cost;
}

ir::Value* EGraph::extractBest(EClassId root, ir::IRBuilder& builder, ir::BasicBlock* bb, std::map<EClassId, ir::Value*>& extractedMap) {
    EClassId cid = uf.find(root);
    if (extractedMap.count(cid)) return extractedMap[cid];

    const auto& nodes = classes[cid];
    if (nodes.empty()) return nullptr;

    const ENode* bestNode = &nodes.front();
    int minCost = 999999;

    for (const auto& n : nodes) {
        if (n.isConstant() || n.isLeaf()) {
            bestNode = &n;
            minCost = 0;
            break;
        }
        int c = computeNodeCost(n);
        if (c < minCost) {
            minCost = c;
            bestNode = &n;
        }
    }

    if (bestNode->isConstant()) {
        extractedMap[cid] = bestNode->constantVal;
        return bestNode->constantVal;
    }

    if (bestNode->isLeaf()) {
        extractedMap[cid] = bestNode->leafVal;
        return bestNode->leafVal;
    }

    std::vector<ir::Value*> childVals;
    for (EClassId ch : bestNode->children) {
        ir::Value* v = extractBest(ch, builder, bb, extractedMap);
        if (!v) return nullptr;
        childVals.push_back(v);
    }

    builder.setInsertPoint(bb);
    ir::Value* result = nullptr;
    if (bestNode->op == ir::Instruction::Shr && childVals.size() >= 2) {
        result = builder.createShr(childVals[0], childVals[1]);
    } else if (bestNode->op == ir::Instruction::Sar && childVals.size() >= 2) {
        result = builder.createSar(childVals[0], childVals[1]);
    } else if (bestNode->op == ir::Instruction::Shl && childVals.size() >= 2) {
        result = builder.createShl(childVals[0], childVals[1]);
    } else if (bestNode->op == ir::Instruction::And && childVals.size() >= 2) {
        result = builder.createAnd(childVals[0], childVals[1]);
    } else if (bestNode->op == ir::Instruction::VBroadcast && childVals.size() >= 1) {
        ir::VectorType* vt = builder.getContext()->getVectorType(builder.getContext()->getIntegerType(32), 4);
        result = builder.createVBroadcast(vt, childVals[0]);
    } else if (bestNode->op == ir::Instruction::Add && childVals.size() >= 2) {
        result = builder.createAdd(childVals[0], childVals[1]);
    } else if (bestNode->op == ir::Instruction::Sub && childVals.size() >= 2) {
        result = builder.createSub(childVals[0], childVals[1]);
    } else if (bestNode->op == ir::Instruction::Mul && childVals.size() >= 2) {
        result = builder.createMul(childVals[0], childVals[1]);
    }

    if (result) extractedMap[cid] = result;
    return result;
}

} // namespace transforms
