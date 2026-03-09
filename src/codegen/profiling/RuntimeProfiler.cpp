#include "codegen/profiling/RuntimeProfiler.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace codegen {
namespace profiling {

RuntimeProfiler::RuntimeProfiler(CodeGen& codegen) : cg(codegen), profilingEnabled(false), sampleCounter(0) {
    initializeCounters();
}

RuntimeProfiler::~RuntimeProfiler() {
    // Cleanup, if necessary
}

void RuntimeProfiler::enableProfiling(bool enable) {
    profilingEnabled = enable;
}


void RuntimeProfiler::beginFunctionProfiling(const std::string& functionName) {
    if (!profilingEnabled) return;
    
    auto it = functionProfiles.find(functionName);
    if (it == functionProfiles.end()) {
        it = functionProfiles.emplace(functionName, functionName).first;
    }
    
    it->second.callCount++;
    it->second.startTime = std::chrono::high_resolution_clock::now();
}

void RuntimeProfiler::endFunctionProfiling(const std::string& functionName) {
    if (!profilingEnabled) return;
    
    auto it = functionProfiles.find(functionName);
    if (it != functionProfiles.end()) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            endTime - it->second.startTime);
        it->second.executionTime += duration.count();
        it->second.totalInstructions += currentFunctionInstructionCount;
    }
    
    currentFunctionInstructionCount = 0;
}

void RuntimeProfiler::profileInstruction(const ir::Instruction& instr) {
    if (!profilingEnabled) return;
    
    instructionCount++;
    currentFunctionInstructionCount++;
    
    // Update opcode frequency
    opcodeFrequency[instr.getOpcode()]++;
    
    // Update type usage
    if (instr.getType()) {
        typeUsage[instr.getType()->getTypeID()]++;
    }
    
    // Profile specific instruction types
    switch (instr.getOpcode()) {
        case ir::Instruction::Add:
        case ir::Instruction::FAdd:
        case ir::Instruction::Sub:
        case ir::Instruction::FSub:
        case ir::Instruction::Mul:
        case ir::Instruction::FMul:
        case ir::Instruction::Div:
        case ir::Instruction::FDiv:
        case ir::Instruction::Rem:
        case ir::Instruction::FRem:
            arithmeticInstructionCount++;
            break;
        case ir::Instruction::Load:
        case ir::Instruction::Store:
            memoryInstructionCount++;
            break;
        case ir::Instruction::Call:
            callInstructionCount++;
            break;
        case ir::Instruction::Jmp:
        case ir::Instruction::Jnz:
        case ir::Instruction::Jz:
            branchInstructionCount++;
            break;
        default:
            otherInstructionCount++;
            break;
    }
}

void RuntimeProfiler::addPerformanceCounter(const std::string& name, uint64_t value) {
    if (!profilingEnabled) return;
    
    auto it = std::find_if(counters.begin(), counters.end(),
        [&name](const PerformanceCounter& counter) {
            return counter.name == name;
        });
    
    if (it != counters.end()) {
        it->value = value;
    } else {
        counters.emplace_back(CounterType::InstructionCount, name);
        counters.back().value = value;
    }
}

void RuntimeProfiler::incrementCounter(const std::string& name, uint64_t increment) {
    if (!profilingEnabled) return;
    
    auto it = std::find_if(counters.begin(), counters.end(),
        [&name](const PerformanceCounter& counter) {
            return counter.name == name;
        });
    
    if (it != counters.end()) {
        it->value += increment;
    } else {
        counters.emplace_back(CounterType::InstructionCount, name);
        counters.back().value = increment;
    }
}

RuntimeProfiler::PerformanceReport RuntimeProfiler::generateReport() const {
    PerformanceReport report;
    
    if (!profilingEnabled) {
        return report;
    }
    
    report.totalInstructions = instructionCount;
    report.totalFunctions = functionProfiles.size();
    report.arithmeticInstructions = arithmeticInstructionCount;
    report.memoryInstructions = memoryInstructionCount;
    report.callInstructions = callInstructionCount;
    report.branchInstructions = branchInstructionCount;
    report.otherInstructions = otherInstructionCount;
    
    // Calculate function statistics
    uint64_t totalExecutionTime = 0;
    uint64_t totalFunctionCalls = 0;
    
    for (const auto& pair : functionProfiles) {
        totalExecutionTime += pair.second.executionTime;
        totalFunctionCalls += pair.second.callCount;
    }
    
    report.averageFunctionExecutionTime = totalFunctionCalls > 0 ? 
        totalExecutionTime / totalFunctionCalls : 0;
    
    // Find hottest functions
    std::vector<std::pair<std::string, uint64_t>> functionTimes;
    for (const auto& pair : functionProfiles) {
        functionTimes.push_back({pair.first, pair.second.executionTime});
    }
    
    std::sort(functionTimes.begin(), functionTimes.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
    
    // Get top 10 hottest functions
    size_t count = std::min(size_t(10), functionTimes.size());
    for (size_t i = 0; i < count; ++i) {
        report.hottestFunctions.push_back(functionTimes[i]);
    }
    
    // Opcode frequency analysis
    std::vector<std::pair<ir::Instruction::Opcode, uint64_t>> opcodeFreq(opcodeFrequency.begin(), 
                                                                         opcodeFrequency.end());
    std::sort(opcodeFreq.begin(), opcodeFreq.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
    
    // Get top 10 most frequent opcodes
    count = std::min(size_t(10), opcodeFreq.size());
    for (size_t i = 0; i < count; ++i) {
        report.mostFrequentOpcodes.push_back(opcodeFreq[i]);
    }
    
    // Performance counters
    report.counters = counters;
    
    return report;
}

void RuntimeProfiler::printReport(std::ostream& os) const {
    if (!profilingEnabled) {
        os << "Profiling is not enabled.\n";
        return;
    }
    
    auto report = generateReport();
    
    os << "\n=== Runtime Performance Report ===\n";
    os << "Total Instructions: " << report.totalInstructions << "\n";
    os << "Total Functions: " << report.totalFunctions << "\n";
    os << "Average Function Execution Time: " << report.averageFunctionExecutionTime << " ns\n";
    
    os << "\nInstruction Breakdown:\n";
    os << "  Arithmetic: " << report.arithmeticInstructions << " (" 
       << std::fixed << std::setprecision(2) 
       << (report.totalInstructions > 0 ? (double(report.arithmeticInstructions) / report.totalInstructions * 100) : 0)
       << "%)\n";
    os << "  Memory: " << report.memoryInstructions << " (" 
       << std::fixed << std::setprecision(2)
       << (report.totalInstructions > 0 ? (double(report.memoryInstructions) / report.totalInstructions * 100) : 0)
       << "%)\n";
    os << "  Calls: " << report.callInstructions << " (" 
       << std::fixed << std::setprecision(2)
       << (report.totalInstructions > 0 ? (double(report.callInstructions) / report.totalInstructions * 100) : 0)
       << "%)\n";
    os << "  Branches: " << report.branchInstructions << " (" 
       << std::fixed << std::setprecision(2)
       << (report.totalInstructions > 0 ? (double(report.branchInstructions) / report.totalInstructions * 100) : 0)
       << "%)\n";
    os << "  Other: " << report.otherInstructions << " (" 
       << std::fixed << std::setprecision(2)
       << (report.totalInstructions > 0 ? (double(report.otherInstructions) / report.totalInstructions * 100) : 0)
       << "%)\n";
    
    if (!report.hottestFunctions.empty()) {
        os << "\nHottest Functions (Top 10):\n";
        for (const auto& func : report.hottestFunctions) {
            os << "  " << func.first << ": " << func.second << " ns\n";
        }
    }
    
    if (!report.mostFrequentOpcodes.empty()) {
        os << "\nMost Frequent Opcodes (Top 10):\n";
        for (const auto& opcode : report.mostFrequentOpcodes) {
            os << "  Opcode " << static_cast<int>(opcode.first) << ": " << opcode.second << " times\n";
        }
    }
    
    if (!report.counters.empty()) {
        os << "\nPerformance Counters:\n";
        for (const auto& counter : report.counters) {
            os << "  " << counter.name << ": " << counter.value << "\n";
        }
    }
    
    os << "==================================\n\n";
}

void RuntimeProfiler::reset() {
    instructionCount = 0;
    functionCount = 0;
    arithmeticInstructionCount = 0;
    memoryInstructionCount = 0;
    callInstructionCount = 0;
    branchInstructionCount = 0;
    otherInstructionCount = 0;
    
    opcodeFrequency.clear();
    typeUsage.clear();
    functionProfiles.clear();
    counters.clear();
    
    currentFunctionInstructionCount = 0;
}

// RegisterPressureAnalyzer implementation
RegisterPressureAnalyzer::RegisterPressureAnalyzer(const target::TargetInfo* targetInfo)
    : targetInfo(targetInfo) {
    // Initialize register counts from target info
    if (targetInfo) {
        integerRegisters = targetInfo->getRegisters(target::RegisterClass::Integer).size();
        floatRegisters = targetInfo->getRegisters(target::RegisterClass::Float).size();
        vectorRegisters = targetInfo->getRegisters(target::RegisterClass::Vector).size();
    }
}

void RegisterPressureAnalyzer::analyzeFunction(const ir::Function& func) {
    currentPressure = 0;
    maxPressure = 0;
    liveIntervals.clear();
    
    // Simple analysis: count instructions that typically require registers
    for (const auto& block : func.getBasicBlocks()) {
        for (const auto& instr : block->getInstructions()) {
            switch (instr->getOpcode()) {
                case ir::Instruction::Add:
                case ir::Instruction::FAdd:
                case ir::Instruction::Sub:
                case ir::Instruction::FSub:
                case ir::Instruction::Mul:
                case ir::Instruction::FMul:
                case ir::Instruction::Div:
                case ir::Instruction::FDiv:
                case ir::Instruction::Load:
                case ir::Instruction::Store:
                case ir::Instruction::Call:
                    currentPressure++;
                    maxPressure = std::max(maxPressure, currentPressure);
                    break;
                case ir::Instruction::Ret:
                    currentPressure = 0; // Reset at return
                    break;
                default:
                    break;
            }
        }
    }
}

RegisterPressureAnalyzer::PressureReport RegisterPressureAnalyzer::generateReport() const {
    PressureReport report;
    report.maxPressure = maxPressure;
    report.averagePressure = maxPressure > 0 ? maxPressure / 2.0 : 0; // Simplified average
    report.integerRegisterCount = integerRegisters;
    report.floatRegisterCount = floatRegisters;
    report.vectorRegisterCount = vectorRegisters;
    report.pressureLevel = getPressureLevel();
    return report;
}

std::string RegisterPressureAnalyzer::getPressureLevel() const {
    if (maxPressure == 0) return "None";
    
    // Calculate pressure relative to available registers
    size_t totalRegisters = integerRegisters + floatRegisters + vectorRegisters;
    double pressureRatio = totalRegisters > 0 ? 
        static_cast<double>(maxPressure) / totalRegisters : 0;
    
    if (pressureRatio > 0.8) return "High";
    if (pressureRatio > 0.5) return "Medium";
    if (pressureRatio > 0.2) return "Low";
    return "Very Low";
}

void RegisterPressureAnalyzer::printReport(std::ostream& os) const {
    auto report = generateReport();
    
    os << "\n=== Register Pressure Analysis ===\n";
    os << "Maximum Register Pressure: " << report.maxPressure << "\n";
    os << "Average Register Pressure: " << std::fixed << std::setprecision(2) 
       << report.averagePressure << "\n";
    os << "Pressure Level: " << report.pressureLevel << "\n";
    os << "Available Registers:\n";
    os << "  Integer: " << report.integerRegisterCount << "\n";
    os << "  Float: " << report.floatRegisterCount << "\n";
    os << "  Vector: " << report.vectorRegisterCount << "\n";
    os << "==================================\n\n";
}

// CachePerformanceAnalyzer implementation
CachePerformanceAnalyzer::CachePerformanceAnalyzer()
    : l1CacheSize(32768), l2CacheSize(262144), l3CacheSize(8388608) { // Default sizes
}

void CachePerformanceAnalyzer::analyzeMemoryAccesses(const ir::Function& func) {
    memoryAccesses.clear();
    cacheMisses.clear();
    
    // Collect memory access patterns
    for (const auto& block : func.getBasicBlocks()) {
        for (const auto& instr : block->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Load ||
                instr->getOpcode() == ir::Instruction::Store) {
                MemoryAccess access;
                access.instruction = instr.get();
                access.address = 0; // Would be determined by actual analysis
                access.size = instr->getType() ? instr->getType()->getSize() : 0;
                memoryAccesses.push_back(access);
            }
        }
    }
    
    // Simple cache simulation
    simulateCacheBehavior();
}

void CachePerformanceAnalyzer::simulateCacheBehavior() {
    // Very simplified cache simulation
    size_t l1Misses = 0;
    size_t l2Misses = 0;
    size_t l3Misses = 0;
    
    // Assume some percentage of accesses miss each cache level
    size_t totalAccesses = memoryAccesses.size();
    l1Misses = totalAccesses * 0.05; // 5% L1 miss rate
    l2Misses = l1Misses * 0.20;      // 20% of L1 misses go to L2
    l3Misses = l2Misses * 0.10;      // 10% of L2 misses go to L3
    
    cacheMisses["L1"] = l1Misses;
    cacheMisses["L2"] = l2Misses;
    cacheMisses["L3"] = l3Misses;
}

CachePerformanceAnalyzer::CacheReport CachePerformanceAnalyzer::generateReport() const {
    CacheReport report;
    report.totalMemoryAccesses = memoryAccesses.size();
    report.cacheMisses = cacheMisses;
    
    // Calculate miss rates
    if (report.totalMemoryAccesses > 0) {
        report.l1MissRate = static_cast<double>(cacheMisses.at("L1")) / report.totalMemoryAccesses;
        report.l2MissRate = static_cast<double>(cacheMisses.at("L2")) / report.totalMemoryAccesses;
        report.l3MissRate = static_cast<double>(cacheMisses.at("L3")) / report.totalMemoryAccesses;
    }
    
    return report;
}

void CachePerformanceAnalyzer::printReport(std::ostream& os) const {
    auto report = generateReport();
    
    os << "\n=== Cache Performance Analysis ===\n";
    os << "Total Memory Accesses: " << report.totalMemoryAccesses << "\n";
    os << "L1 Cache Misses: " << report.cacheMisses.at("L1") 
       << " (Rate: " << std::fixed << std::setprecision(4) << report.l1MissRate << ")\n";
    os << "L2 Cache Misses: " << report.cacheMisses.at("L2") 
       << " (Rate: " << std::fixed << std::setprecision(4) << report.l2MissRate << ")\n";
    os << "L3 Cache Misses: " << report.cacheMisses.at("L3") 
       << " (Rate: " << std::fixed << std::setprecision(4) << report.l3MissRate << ")\n";
    os << "==================================\n\n";
}

// Integration with CodeGen
void integrateWithCodeGen(CodeGen& cg) {
    // This would be called to integrate profiling with the code generation process
    // In a real implementation, this would add instrumentation code to the generated assembly
}

void RuntimeProfiler::initializeCounters() {
    //
}

} // namespace profiling
} // namespace codegen