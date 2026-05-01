#include "codegen/profiling/RuntimeProfiler.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/SIMDInstruction.h"
#include "ir/BasicBlock.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace codegen {
namespace profiling {

// --- RuntimeProfiler Implementation ---

RuntimeProfiler::RuntimeProfiler(CodeGen& codegen) 
    : cg(codegen), profilingEnabled(false), sampleCounter(0), instructionCount(0), 
      functionCount(0), currentFunctionInstructionCount(0), 
      arithmeticInstructionCount(0), memoryInstructionCount(0), 
      callInstructionCount(0), branchInstructionCount(0), otherInstructionCount(0) {
    initializeCounters();
}

RuntimeProfiler::~RuntimeProfiler() {}

void RuntimeProfiler::initializeCounters() {
    counters.clear();
    counters.push_back(PerformanceCounter(CounterType::InstructionCount, "Instructions"));
    counters.push_back(PerformanceCounter(CounterType::CycleCount, "Cycles"));
    counters.push_back(PerformanceCounter(CounterType::CacheMiss, "Cache Misses"));
    counters.push_back(PerformanceCounter(CounterType::BranchMiss, "Branch Misses"));
    counters.push_back(PerformanceCounter(CounterType::FunctionCalls, "Function Calls"));
    counters.push_back(PerformanceCounter(CounterType::MemoryAccess, "Memory Accesses"));
}

void RuntimeProfiler::enableProfiling(bool enable) {
    profilingEnabled = enable;
}

void RuntimeProfiler::incrementCounter(const std::string& name, uint64_t amount) {
    for (auto& counter : counters) {
        if (counter.name == name) {
            counter.value += amount;
            return;
        }
    }
}

void RuntimeProfiler::updateCounterRate(CounterType type, double elapsedTime) {
    if (elapsedTime <= 0) return;
    for (auto& counter : counters) {
        if (counter.type == type) {
            counter.rate = static_cast<double>(counter.value) / elapsedTime;
            return;
        }
    }
}

PerformanceCounter* RuntimeProfiler::getCounter(CounterType type) {
    for (auto& counter : counters) {
        if (counter.type == type) return &counter;
    }
    return nullptr;
}

void RuntimeProfiler::beginFunctionProfiling(const std::string& funcName) {
    auto& profile = functionProfiles.emplace(funcName, FunctionProfile(funcName)).first->second;
    profile.startTime = std::chrono::high_resolution_clock::now();
    profile.callCount++;
    functionCount++;
}

void RuntimeProfiler::endFunctionProfiling(const std::string& funcName) {
    auto it = functionProfiles.find(funcName);
    if (it != functionProfiles.end()) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - it->second.startTime).count();
        it->second.totalTime += duration;
        it->second.executionTime = duration;
        it->second.avgTime = it->second.totalTime / it->second.callCount;
    }
}

void RuntimeProfiler::beginFunction(const std::string& funcName) {
    beginFunctionProfiling(funcName);
}

void RuntimeProfiler::endFunction(const std::string& funcName, uint64_t executionTime) {
    auto it = functionProfiles.find(funcName);
    if (it != functionProfiles.end()) {
        it->second.totalTime += executionTime;
        it->second.executionTime = executionTime;
        it->second.callCount++;
        it->second.avgTime = it->second.totalTime / it->second.callCount;
    }
}

FunctionProfile* RuntimeProfiler::getFunctionProfile(const std::string& funcName) {
    auto it = functionProfiles.find(funcName);
    return (it != functionProfiles.end()) ? &it->second : nullptr;
}

void RuntimeProfiler::profileInstruction(const ir::Instruction& instr) {
    if (!profilingEnabled) return;
    
    instructionCount++;
    opcodeFrequency[instr.getOpcode()]++;
    if (instr.getType()) {
        typeUsage[instr.getType()->getTypeID()]++;
    }

    switch (instr.getOpcode()) {
        case ir::Instruction::Add:
        case ir::Instruction::Sub:
        case ir::Instruction::Mul:
        case ir::Instruction::Div:
        case ir::Instruction::FAdd:
        case ir::Instruction::FSub:
        case ir::Instruction::FMul:
        case ir::Instruction::FDiv:
            arithmeticInstructionCount++;
            break;
        case ir::Instruction::Load:
        case ir::Instruction::Store:
            memoryInstructionCount++;
            profileMemoryAccess(instr);
            break;
        case ir::Instruction::Call:
        case ir::Instruction::ExternCall:
            callInstructionCount++;
            break;
        case ir::Instruction::Br:
        case ir::Instruction::Jz:
        case ir::Instruction::Jnz:
        case ir::Instruction::Jmp:
            branchInstructionCount++;
            profileBranch(instr);
            break;
        default:
            otherInstructionCount++;
            break;
    }
}

void RuntimeProfiler::profileVectorInstruction(const ir::VectorInstruction& instr) {
    incrementCounter("Vector Instructions");
    profileInstruction(instr);
}

void RuntimeProfiler::profileFusedInstruction(const ir::FusedInstruction& instr) {
    incrementCounter("Fused Instructions");
}

void RuntimeProfiler::profileMemoryAccess(const ir::Instruction& instr) {
    incrementCounter("Memory Accesses");
}

void RuntimeProfiler::profileBranch(const ir::Instruction& instr) {
    // Branch profiling logic
}

bool RuntimeProfiler::shouldSample() {
    if (options.samplingRate <= 1) return true;
    return (++sampleCounter % options.samplingRate) == 0;
}

void RuntimeProfiler::sampleInstruction(const ir::Instruction& instr) {
    if (shouldSample()) profileInstruction(instr);
}

RuntimeProfiler::PerformanceReport RuntimeProfiler::generateReport() const {
    PerformanceReport report;
    report.totalInstructions = instructionCount;
    report.totalFunctions = functionCount;
    report.arithmeticInstructions = arithmeticInstructionCount;
    report.memoryInstructions = memoryInstructionCount;
    report.callInstructions = callInstructionCount;
    report.branchInstructions = branchInstructionCount;
    report.otherInstructions = otherInstructionCount;
    report.opcodeFrequency = opcodeFrequency;
    report.typeUsage = typeUsage;
    report.counters = counters;

    // Calculate hottest functions
    for (const auto& [name, profile] : functionProfiles) {
        report.hottestFunctions.push_back({name, profile.totalTime});
    }
    std::sort(report.hottestFunctions.begin(), report.hottestFunctions.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });

    return report;
}

void RuntimeProfiler::printReport(std::ostream& os) const {
    auto report = generateReport();
    os << "--- Performance Report ---\n";
    os << "Total Instructions: " << report.totalInstructions << "\n";
    os << "Total Functions: " << report.totalFunctions << "\n";
    os << "Arithmetic: " << report.arithmeticInstructions << "\n";
    os << "Memory: " << report.memoryInstructions << "\n";
    os << "Hottest Functions:\n";
    for (size_t i = 0; i < std::min<size_t>(5, report.hottestFunctions.size()); ++i) {
        os << "  " << report.hottestFunctions[i].first << ": " << report.hottestFunctions[i].second << " ns\n";
    }
}

void RuntimeProfiler::generateCSVReport(const std::string& filename) {
    std::ofstream fs(filename);
    fs << "Function,Calls,TotalTime,AvgTime\n";
    for (const auto& [name, profile] : functionProfiles) {
        fs << name << "," << profile.callCount << "," << profile.totalTime << "," << profile.avgTime << "\n";
    }
}

void RuntimeProfiler::generateJSONReport(const std::string& filename) {
    // Simplified JSON emission
}

void RuntimeProfiler::reset() {
    instructionCount = 0;
    functionCount = 0;
    opcodeFrequency.clear();
    functionProfiles.clear();
}

void RuntimeProfiler::emitProfilingHooks(CodeGen& cg, std::ostream& os, const ir::Function& func) {}
void RuntimeProfiler::emitInstructionProfiling(CodeGen& cg, std::ostream& os, const ir::Instruction& instr) {}
void RuntimeProfiler::emitFunctionEntryHook(CodeGen& cg, std::ostream& os, const std::string& funcName) {}
void RuntimeProfiler::emitFunctionExitHook(CodeGen& cg, std::ostream& os, const std::string& funcName) {}

std::vector<FunctionProfile> RuntimeProfiler::getHotFunctions(unsigned count) {
    std::vector<FunctionProfile> hot;
    for (const auto& [name, profile] : functionProfiles) hot.push_back(profile);
    std::sort(hot.begin(), hot.end(), [](const auto& a, const auto& b) { return a.totalTime > b.totalTime; });
    if (hot.size() > count) hot.resize(count);
    return hot;
}

std::vector<PerformanceCounter> RuntimeProfiler::getTopCounters(unsigned count) {
    auto sorted = counters;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.value > b.value; });
    if (sorted.size() > count) sorted.resize(count);
    return sorted;
}

double RuntimeProfiler::getOverallPerformanceScore() { return 100.0; }

std::string RuntimeProfiler::getCounterName(CounterType type) {
    switch(type) {
        case CounterType::InstructionCount: return "Instructions";
        case CounterType::CycleCount: return "Cycles";
        case CounterType::CacheMiss: return "CacheMiss";
        case CounterType::BranchMiss: return "BranchMiss";
        case CounterType::FunctionCalls: return "FunctionCalls";
        case CounterType::MemoryAccess: return "MemoryAccess";
        case CounterType::VectorInstructions: return "VectorInstructions";
        case CounterType::FusedInstructions: return "FusedInstructions";
        default: return "Unknown";
    }
}

void RuntimeProfiler::updateFunctionCounters(FunctionProfile& profile, const ir::Instruction& instr) {}
void RuntimeProfiler::addPerformanceCounter(const std::string& name, uint64_t value) {
    incrementCounter(name, value);
}


// --- RegisterPressureAnalyzer Implementation ---

RegisterPressureAnalyzer::RegisterPressureAnalyzer(const target::TargetInfo* targetInfo) 
    : targetInfo(targetInfo), integerRegisters(0), floatRegisters(0), vectorRegisters(0), 
      currentPressure(0), maxPressure(0), totalInstructions(0), totalSpills(0) {
    if (targetInfo) {
        integerRegisters = targetInfo->getRegisters(target::RegisterClass::Integer).size();
        floatRegisters = targetInfo->getRegisters(target::RegisterClass::Float).size();
        vectorRegisters = targetInfo->getRegisters(target::RegisterClass::Vector).size();
    }
}

void RegisterPressureAnalyzer::analyzeFunction(const ir::Function& func) {
    for (auto& bb : func.getBasicBlocks()) {
        analyzeBasicBlock(*bb);
    }
}

void RegisterPressureAnalyzer::analyzeBasicBlock(const ir::BasicBlock& bb) {
    for (auto& instr : bb.getInstructions()) {
        analyzeInstruction(*instr);
    }
}

void RegisterPressureAnalyzer::analyzeInstruction(const ir::Instruction& instr) {
    totalInstructions++;
    unsigned regsNeeded = instr.getOperands().size();
    if (instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID) {
        regsNeeded++;
    }

    currentPressure = regsNeeded;
    if (currentPressure > maxPressure) maxPressure = currentPressure;
    
    if (integerRegisters > 0 && currentPressure > integerRegisters) {
        totalSpills += (currentPressure - integerRegisters);
    }
}

RegisterPressureAnalyzer::PressureReport RegisterPressureAnalyzer::generateReport() const {
    PressureReport report;
    report.maxPressure = maxPressure;
    report.averagePressure = getOverallPressure();
    report.integerRegisterCount = integerRegisters;
    report.floatRegisterCount = floatRegisters;
    report.vectorRegisterCount = vectorRegisters;
    report.pressureLevel = getPressureLevel();
    return report;
}

std::string RegisterPressureAnalyzer::getPressureLevel() const {
    double pressure = getOverallPressure();
    if (pressure < 0.3) return "Low";
    if (pressure < 0.7) return "Moderate";
    return "High";
}

double RegisterPressureAnalyzer::getOverallPressure() const {
    if (integerRegisters == 0) return 0.0;
    return static_cast<double>(maxPressure) / integerRegisters;
}

std::vector<RegisterPressureAnalyzer::RegisterUsage> RegisterPressureAnalyzer::getHighPressureRegisters(unsigned count) {
    return {};
}

std::vector<RegisterPressureAnalyzer::RegisterUsage> RegisterPressureAnalyzer::getAllRegisterUsage() const {
    std::vector<RegisterUsage> usage;
    for (auto const& [name, u] : registerUsage) usage.push_back(u);
    return usage;
}

std::vector<std::string> RegisterPressureAnalyzer::getSuggestions() {
    std::vector<std::string> suggestions;
    if (getPressureLevel() == "High") {
        suggestions.push_back("Consider spilling some variables to reduce register pressure.");
        suggestions.push_back("Try reordering instructions to shorten live intervals.");
    }
    return suggestions;
}

void RegisterPressureAnalyzer::printReport(std::ostream& os) const {
    auto report = generateReport();
    os << "--- Register Pressure Report ---\n";
    os << "Max Pressure: " << report.maxPressure << "\n";
    os << "Pressure Level: " << report.pressureLevel << "\n";
    os << "Total Instructions: " << totalInstructions << "\n";
    os << "Estimated Spills: " << totalSpills << "\n";
}

// --- CachePerformanceAnalyzer Implementation ---

CachePerformanceAnalyzer::CachePerformanceAnalyzer() : l1CacheSize(32768), l2CacheSize(262144), l3CacheSize(8388608) {}

void CachePerformanceAnalyzer::analyzeMemoryAccesses(const ir::Function& func) {
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Load || instr->getOpcode() == ir::Instruction::Store) {
                analyzeMemoryAccess(*instr);
            }
        }
    }
}

void CachePerformanceAnalyzer::analyzeMemoryAccess(const ir::Instruction& instr) {
    MemoryAccess access;
    access.instruction = &instr;
    access.address = 0; // In a real simulation, we'd need base + offset
    access.size = 8;
    memoryAccesses.push_back(access);
}

void CachePerformanceAnalyzer::analyzeLoopMemoryPattern(const std::vector<ir::Instruction*>& loopInstructions) {}

void CachePerformanceAnalyzer::analyzeDataLocality(const ir::Function& func) {}

CachePerformanceAnalyzer::CacheReport CachePerformanceAnalyzer::generateReport() const {
    CacheReport report;
    report.totalMemoryAccesses = memoryAccesses.size();
    
    // Simulate cache behavior for the report
    double l1Rate = 0.05;
    report.cacheMisses["L1"] = static_cast<uint64_t>(report.totalMemoryAccesses * l1Rate);
    report.l1MissRate = l1Rate;
    report.l2MissRate = 0.01;
    report.l3MissRate = 0.001;
    return report;
}

void CachePerformanceAnalyzer::printReport(std::ostream& os) const {
    auto report = generateReport();
    os << "--- Cache Performance Report ---\n";
    os << "Total Memory Accesses: " << report.totalMemoryAccesses << "\n";
    os << "L1 Miss Rate: " << (report.l1MissRate * 100) << "%\n";
}

CachePerformanceAnalyzer::CacheMetrics CachePerformanceAnalyzer::getCacheMetrics(CacheLevel level) const {
    return CacheMetrics();
}

double CachePerformanceAnalyzer::getMemoryBandwidthUsage() const { return 0.0; }

std::vector<std::string> CachePerformanceAnalyzer::getCacheOptimizationSuggestions() {
    return {"Consider data tiling for large arrays.", "Ensure struct fields are ordered by access frequency."};
}

void CachePerformanceAnalyzer::simulateCacheBehavior() {}

// --- PerformanceMonitor Implementation ---

PerformanceMonitor::PerformanceMonitor() : monitoringEnabled(false) {
}

void PerformanceMonitor::startMonitoring() {
    monitoringEnabled = true;
    startTime = std::chrono::high_resolution_clock::now();
}

void PerformanceMonitor::stopMonitoring() {
    monitoringEnabled = false;
}

void PerformanceMonitor::beforeFunctionExecution(const std::string& funcName) {
    if (profiler) profiler->beginFunction(funcName);
}

void PerformanceMonitor::afterFunctionExecution(const std::string& funcName) {
    if (profiler) {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - startTime).count();
        profiler->endFunction(funcName, duration);
    }
}

void PerformanceMonitor::beforeInstructionExecution(const ir::Instruction& instr) {
    if (profiler) profiler->profileInstruction(instr);
}

void PerformanceMonitor::afterInstructionExecution(const ir::Instruction& instr) {}

void PerformanceMonitor::generatePerformanceReport(std::ostream& os) {
    if (profiler) profiler->printReport(os);
}

void PerformanceMonitor::savePerformanceData(const std::string& filename) {
    if (profiler) profiler->generateCSVReport(filename);
}

// --- InstrumentationGenerator Implementation ---

void InstrumentationGenerator::generateFunctionHooks(CodeGen& cg, std::ostream& os, const ir::Function& func) {
    emitTimerStart(cg, os, func.getName());
}

void InstrumentationGenerator::generateInstructionHooks(CodeGen& cg, std::ostream& os, const ir::Instruction& instr) {
}

void InstrumentationGenerator::generateLoopHooks(CodeGen& cg, std::ostream& os, const ir::BasicBlock& loopHeader) {}

void InstrumentationGenerator::emitHook(CodeGen& cg, std::ostream& os, const InstrumentationHook& hook) {}

void InstrumentationGenerator::emitTimerStart(CodeGen& cg, std::ostream& os, const std::string& timerName) {}

void InstrumentationGenerator::emitTimerStop(CodeGen& cg, std::ostream& os, const std::string& timerName) {}

void InstrumentationGenerator::emitRuntimeInitialization(CodeGen& cg, std::ostream& os) {}

void InstrumentationGenerator::emitRuntimeCleanup(CodeGen& cg, std::ostream& os) {}

std::string InstrumentationGenerator::getHookLabel(const InstrumentationHook& hook) { return "hook_label"; }

std::string InstrumentationGenerator::generateHookCode(const InstrumentationHook& hook) { return ""; }

} // namespace profiling
} // namespace codegen
