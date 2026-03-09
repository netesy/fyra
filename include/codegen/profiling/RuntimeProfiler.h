#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include "codegen/CodeGen.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>

namespace codegen {
namespace target {
class TargetInfo;
}
namespace profiling {

// Performance counter types
enum class CounterType {
    InstructionCount,
    CycleCount,
    CacheMiss,
    BranchMiss,
    FunctionCalls,
    MemoryAccess,
    VectorInstructions,
    FusedInstructions
};

// Performance counter data
struct PerformanceCounter {
    CounterType type;
    std::string name;
    uint64_t value;
    double rate; // Per second or per instruction
    
    PerformanceCounter(CounterType t, const std::string& n) 
        : type(t), name(n), value(0), rate(0.0) {}
};

// Function profiling data
struct FunctionProfile {
    std::string name;
    uint64_t callCount;
    uint64_t totalTime; // In nanoseconds
    uint64_t avgTime;   // In nanoseconds
    std::map<CounterType, uint64_t> counters;
    std::vector<std::string> hotPaths;
    uint64_t totalInstructions;
    std::chrono::high_resolution_clock::time_point startTime;
    uint64_t executionTime;
    
    FunctionProfile(const std::string& n) 
        : name(n), callCount(0), totalTime(0), avgTime(0) {}
};

// Runtime profiler
class RuntimeProfiler {
public:
    struct ProfilingOptions {
        bool instructionCounting;
        bool cycleCounting;
        bool cacheProfiling;
        bool branchProfiling;
        bool memoryProfiling;
        bool vectorProfiling;
        bool fusedInstructionProfiling;
        unsigned samplingRate; // 1 = every instruction, 100 = every 100th instruction
        
        ProfilingOptions() 
            : instructionCounting(true), cycleCounting(true), cacheProfiling(false),
              branchProfiling(true), memoryProfiling(false), vectorProfiling(true),
              fusedInstructionProfiling(true), samplingRate(1) {}
    };

    struct PerformanceReport {
        uint64_t totalInstructions;
        uint64_t totalFunctions;
        uint64_t totalCycles;
        double ipc;
        std::map<ir::Instruction::Opcode, uint64_t> opcodeFrequency;
        std::map<ir::Type::TypeID, uint64_t> typeUsage;
        uint64_t arithmeticInstructions;
        uint64_t memoryInstructions;
        uint64_t callInstructions;
        uint64_t branchInstructions;
        uint64_t otherInstructions;
        uint64_t averageFunctionExecutionTime;
        std::vector<std::pair<std::string, uint64_t>> hottestFunctions;
        std::vector<std::pair<ir::Instruction::Opcode, uint64_t>> mostFrequentOpcodes;
        std::vector<PerformanceCounter> counters;
    };

private:
    CodeGen& cg;
    ProfilingOptions options;
    std::map<std::string, FunctionProfile> functionProfiles;
    std::vector<PerformanceCounter> counters;
    bool profilingEnabled;
    uint64_t sampleCounter;
    uint64_t instructionCount;
    uint64_t functionCount;
    uint64_t currentFunctionInstructionCount;
    std::map<ir::Instruction::Opcode, uint64_t> opcodeFrequency;
    std::map<ir::Type::TypeID, uint64_t> typeUsage;
    uint64_t arithmeticInstructionCount;
    uint64_t memoryInstructionCount;
    uint64_t callInstructionCount;
    uint64_t branchInstructionCount;
    uint64_t otherInstructionCount;

    
public:
    RuntimeProfiler(CodeGen& codegen);
    ~RuntimeProfiler();
    
    // Configuration
    void enableProfiling(bool enable = true);
    bool isProfilingEnabled() const { return profilingEnabled; }
    void setProfilingOptions(const ProfilingOptions& opts) { options = opts; }
    const ProfilingOptions& getProfilingOptions() const { return options; }
    
    // Counter management
    void incrementCounter(const std::string& name, uint64_t amount = 1);
    void updateCounterRate(CounterType type, double elapsedTime);
    PerformanceCounter* getCounter(CounterType type);
    
    // Function profiling
    void beginFunctionProfiling(const std::string& funcName);
    void endFunctionProfiling(const std::string& funcName);
    void beginFunction(const std::string& funcName);
    void endFunction(const std::string& funcName, uint64_t executionTime);
    FunctionProfile* getFunctionProfile(const std::string& funcName);
    
    // Instruction profiling
    void profileInstruction(const ir::Instruction& instr);
    void profileVectorInstruction(const ir::VectorInstruction& instr);
    void profileFusedInstruction(const ir::FusedInstruction& instr);
    void profileMemoryAccess(const ir::Instruction& instr);
    void profileBranch(const ir::Instruction& instr);
    
    // Sampling
    bool shouldSample();
    void sampleInstruction(const ir::Instruction& instr);
    
    // Report generation
    PerformanceReport generateReport() const;
    void printReport(std::ostream& os) const;
    void generateCSVReport(const std::string& filename);
    void generateJSONReport(const std::string& filename);
    void reset();
    
    // Integration with CodeGen
    void emitProfilingHooks(CodeGen& cg, std::ostream& os, const ir::Function& func);
    void emitInstructionProfiling(CodeGen& cg, std::ostream& os, const ir::Instruction& instr);
    void emitFunctionEntryHook(CodeGen& cg, std::ostream& os, const std::string& funcName);
    void emitFunctionExitHook(CodeGen& cg, std::ostream& os, const std::string& funcName);
    
    // Performance analysis
    std::vector<FunctionProfile> getHotFunctions(unsigned count = 10);
    std::vector<PerformanceCounter> getTopCounters(unsigned count = 5);
    double getOverallPerformanceScore();
    
private:
    void initializeCounters();
    std::string getCounterName(CounterType type);
    void updateFunctionCounters(FunctionProfile& profile, const ir::Instruction& instr);
    void addPerformanceCounter(const std::string& name, uint64_t value);
};

// Performance monitoring interface
class PerformanceMonitor {
private:
    std::unique_ptr<RuntimeProfiler> profiler;
    std::chrono::high_resolution_clock::time_point startTime;
    bool monitoringEnabled;
    
public:
    PerformanceMonitor();
    
    // Monitoring control
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoringEnabled; }
    
    // Integration points
    void beforeFunctionExecution(const std::string& funcName);
    void afterFunctionExecution(const std::string& funcName);
    void beforeInstructionExecution(const ir::Instruction& instr);
    void afterInstructionExecution(const ir::Instruction& instr);
    
    // Performance data access
    RuntimeProfiler* getProfiler() { return profiler.get(); }
    const RuntimeProfiler* getProfiler() const { return profiler.get(); }
    
    // Report generation
    void generatePerformanceReport(std::ostream& os);
    void savePerformanceData(const std::string& filename);
};

// Instrumentation hooks generator
class InstrumentationGenerator {
public:
    // Profiling hook types
    enum class HookType {
        FunctionEntry,
        FunctionExit,
        InstructionExecute,
        LoopIteration,
        MemoryAccess,
        BranchTaken
    };
    
    struct InstrumentationHook {
        HookType type;
        std::string functionName;
        std::string label;
        uint64_t address;
        std::map<std::string, std::string> parameters;
    };
    
    // Hook generation
    void generateFunctionHooks(CodeGen& cg, std::ostream& os, const ir::Function& func);
    void generateInstructionHooks(CodeGen& cg, std::ostream& os, const ir::Instruction& instr);
    void generateLoopHooks(CodeGen& cg, std::ostream& os, const ir::BasicBlock& loopHeader);
    
    // Hook emission
    void emitHook(CodeGen& cg, std::ostream& os, const InstrumentationHook& hook);
    void emitTimerStart(CodeGen& cg, std::ostream& os, const std::string& timerName);
    void emitTimerStop(CodeGen& cg, std::ostream& os, const std::string& timerName);
    
    // Runtime library integration
    void emitRuntimeInitialization(CodeGen& cg, std::ostream& os);
    void emitRuntimeCleanup(CodeGen& cg, std::ostream& os);
    
private:
    std::string getHookLabel(const InstrumentationHook& hook);
    std::string generateHookCode(const InstrumentationHook& hook);
};

// Register pressure analyzer
class RegisterPressureAnalyzer {
public:
    struct PressureReport {
        uint64_t maxPressure;
        double averagePressure;
        uint64_t integerRegisterCount;
        uint64_t floatRegisterCount;
        uint64_t vectorRegisterCount;
        std::string pressureLevel;
    };
    struct RegisterUsage {
        std::string registerName;
        unsigned usageCount;
        unsigned spillCount;
        double pressure; // 0.0 to 1.0
        
        RegisterUsage(const std::string& name) 
            : registerName(name), usageCount(0), spillCount(0), pressure(0.0) {}
    };
    
    // Analysis methods
    RegisterPressureAnalyzer(const target::TargetInfo* targetInfo);
    void analyzeFunction(const ir::Function& func);
    void analyzeBasicBlock(const ir::BasicBlock& bb);
    void analyzeInstruction(const ir::Instruction& instr);
    
    // Register pressure metrics
    PressureReport generateReport() const;
    std::string getPressureLevel() const;
    double getOverallPressure() const;
    std::vector<RegisterUsage> getHighPressureRegisters(unsigned count = 5);
    std::vector<RegisterUsage> getAllRegisterUsage() const;
    
    // Optimization suggestions
    std::vector<std::string> getSuggestions();
    void printReport(std::ostream& os) const;
    
private:
    const target::TargetInfo* targetInfo;
    unsigned integerRegisters;
    unsigned floatRegisters;
    unsigned vectorRegisters;
    unsigned currentPressure;
    unsigned maxPressure;
    std::map<std::string, RegisterUsage> registerUsage;
    std::vector<std::pair<unsigned, unsigned>> liveIntervals;
    unsigned totalInstructions;
    unsigned totalSpills;
};

// Cache performance analyzer
class CachePerformanceAnalyzer {
public:
    struct MemoryAccess {
        const ir::Instruction* instruction;
        uint64_t address;
        uint64_t size;
    };
    struct CacheReport {
        uint64_t totalMemoryAccesses;
        std::map<std::string, uint64_t> cacheMisses;
        double l1MissRate;
        double l2MissRate;
        double l3MissRate;
    };
    struct CacheMetrics {
        uint64_t hits;
        uint64_t misses;
        double hitRate;
        double missRate;
        uint64_t evictions;
        
        CacheMetrics() : hits(0), misses(0), hitRate(0.0), missRate(0.0), evictions(0) {}
    };
    
    // Cache levels
    enum class CacheLevel {
        L1,
        L2,
        L3,
        TLB
    };
    
    // Memory access pattern analysis
    CachePerformanceAnalyzer();
    void analyzeMemoryAccesses(const ir::Function& func);
    void analyzeMemoryAccess(const ir::Instruction& instr);
    void analyzeLoopMemoryPattern(const std::vector<ir::Instruction*>& loopInstructions);
    void analyzeDataLocality(const ir::Function& func);
    
    // Cache metrics
    CacheReport generateReport() const;
    void printReport(std::ostream& os) const;
    CacheMetrics getCacheMetrics(CacheLevel level) const;
    double getMemoryBandwidthUsage() const;
    std::vector<std::string> getCacheOptimizationSuggestions();
    
private:
    void simulateCacheBehavior();
    size_t l1CacheSize;
    size_t l2CacheSize;
    size_t l3CacheSize;
    std::vector<MemoryAccess> memoryAccesses;
    std::map<std::string, uint64_t> cacheMisses;
    std::map<CacheLevel, CacheMetrics> cacheMetrics;
    std::map<std::string, uint64_t> memoryAccessPatterns;
};

} // namespace profiling
} // namespace codegen