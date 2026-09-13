#include "parser/Parser.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/architecture/wasm32/WasmModule.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <cstdlib>
#include <stdexcept>

static std::string toHex(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    for (uint8_t b : data) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}

static void runNodeVerification(const std::vector<uint8_t>& code, const std::string& checkJs) {
    std::ofstream out("test_temp.wasm", std::ios::binary);
    out.write(reinterpret_cast<const char*>(code.data()), code.size());
    out.close();

    std::string cmd = "node -e 'const fs=require(\"fs\"); const bytes=fs.readFileSync(\"test_temp.wasm\"); if (!WebAssembly.validate(bytes)) { console.error(\"INVALID WASM\"); process.exit(1); } const m=new WebAssembly.Module(bytes); const i=new WebAssembly.Instance(m); " + checkJs + "' > /tmp/node.log 2>&1";
    int res = std::system(cmd.c_str());
    if (res != 0) {
        std::cerr << "Node verification failed. Output log:" << std::endl;
        std::ifstream logFile("/tmp/node.log");
        std::cerr << logFile.rdbuf() << std::endl;
    } else {
        std::remove("test_temp.wasm");
    }
    assert(res == 0);
}

int main() {
    std::cout << "=== WASM Target Execution & Verification Test Suite ===" << std::endl;

    // Test 1: Minimal fixture (ret 42)
    {
        std::string src = "export function $main() : i32 {\n@start\n    ret 42 : i32\n}";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        std::string hex = toHex(code);
        std::cout << "Fixture WASM hex (" << code.size() << " bytes): " << hex << std::endl;

        assert(code.size() == 38 || code.size() == 37 || code.size() == 39);
        assert(code[0] == 0x00 && code[1] == 0x61 && code[2] == 0x73 && code[3] == 0x6d);

        runNodeVerification(code, "if (i.exports.main() !== 42) process.exit(1);");

        // Test determinism
        auto targetInfo2 = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen2(*module, std::move(targetInfo2));
        codeGen2.emit();
        assert(codeGen.getAssembler().getCode() == codeGen2.getAssembler().getCode());
        std::cout << "Determinism check passed for minimal fixture." << std::endl;
    }

    // Test 2: Multi-function parameterized call ($add(20, 22) -> 42)
    {
        std::string src = R"(
function $add(%a : i32, %b : i32) : i32 {
@start
    %r = add %a, %b : i32
    ret %r : i32
}

export function $main() : i32 {
@start
    %r = call $add(i32 20, i32 22) : i32
    ret %r : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        // WAT text generation test
        std::stringstream watStream;
        auto targetInfoWat = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenWat(*module, std::move(targetInfoWat), &watStream);
        codeGenWat.emit();
        std::string wat = watStream.str();
        assert(wat.find("i32.add") != std::string::npos);
        assert(wat.find("call $add") != std::string::npos);

        // Binary generation test
        auto targetInfoBin = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenBin(*module, std::move(targetInfoBin));
        codeGenBin.emit();

        const auto& code = codeGenBin.getAssembler().getCode();
        std::cout << "Multi-function WASM size: " << code.size() << " bytes" << std::endl;
        std::cout << "Multi-function WASM hex: " << toHex(code) << std::endl;

        runNodeVerification(code, "if (i.exports.main() !== 42) process.exit(1);");
        std::cout << "Node.js WebAssembly.instantiate verification passed: main() == 42" << std::endl;

        // Determinism check
        std::stringstream watStream2;
        auto targetInfoWat2 = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenWat2(*module, std::move(targetInfoWat2), &watStream2);
        codeGenWat2.emit();
        assert(watStream.str() == watStream2.str());

        auto targetInfoBin2 = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenBin2(*module, std::move(targetInfoBin2));
        codeGenBin2.emit();
        assert(codeGenBin.getAssembler().getCode() == codeGenBin2.getAssembler().getCode());
        std::cout << "Determinism check passed for multi-function module." << std::endl;
    }

    // Test 3: Opcode Execution Suite (Arithmetic, Bitwise, Shifts, Comparisons, LEB128 boundaries)
    {
        std::string src = R"(
export function $test_ops() : i32 {
@start
    %a = add 10, 5 : i32
    %s = sub %a, 3 : i32
    %m = mul %s, 4 : i32
    %d = div %m, 6 : i32
    %u = udiv %d, 2 : i32
    %r1 = rem %u, 3 : i32
    ret %r1 : i32
}

export function $test_signed_div() : i32 {
@start
    %d = div -100, 5 : i32
    ret %d : i32
}

export function $test_unsigned_div() : i32 {
@start
    %d = udiv -1, 2 : i32
    ret %d : i32
}

export function $test_bitwise() : i32 {
@start
    %a = and 15, 7 : i32
    %b = or %a, 16 : i32
    %c = xor %b, 3 : i32
    %d = shl %c, 2 : i32
    ret %d : i32
}

export function $test_shr_u() : i32 {
@start
    %a = shr -16, 2 : i32
    ret %a : i32
}

export function $test_sar_s() : i32 {
@start
    %a = sar -16, 2 : i32
    ret %a : i32
}

export function $test_leb128() : i32 {
@start
    %v1 = add -1, -64 : i32
    %v2 = add %v1, -65 : i32
    %v3 = add %v2, 127 : i32
    %v4 = add %v3, 128 : i32
    ret %v4 : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        runNodeVerification(code, R"(if (i.exports.test_ops() !== 1) process.exit(1); if (i.exports.test_signed_div() !== -20) process.exit(2); if (i.exports.test_unsigned_div() !== 2147483647) process.exit(3); if (i.exports.test_bitwise() !== 80) process.exit(4); if (i.exports.test_shr_u() !== 1073741820) process.exit(5); if (i.exports.test_sar_s() !== -4) process.exit(6); if (i.exports.test_leb128() !== 125) process.exit(8);)");
        std::cout << "Opcode & Signedness & LEB128 execution tests passed successfully!" << std::endl;
    }

    // Test 4: Real Conditional Diamond CFG Execution Test (with merge block & phi node)
    {
        std::string src = R"(
export function $diamond(%x : i32) : i32 {
@start
    %cond = sgt %x, 0 : i32
    jnz %cond, @pos, @neg : i32

@pos
    %r1 = add 10, 1 : i32
    jmp @merge : i32

@neg
    %r2 = add 20, 1 : i32
    jmp @merge : i32

@merge
    %res = phi @pos %r1, @neg %r2 : i32
    %res2 = add %res, 1 : i32
    ret %res2 : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        runNodeVerification(code, R"(if (i.exports.diamond(5) !== 12) process.exit(1); if (i.exports.diamond(-5) !== 22) process.exit(2);)");
        std::cout << "Real Conditional Diamond CFG with Merge Block & PHI Node execution test passed successfully!" << std::endl;
    }

    // Test 5: Fibonacci Control Flow Execution Test (fib(0), fib(1), fib(2), fib(5), fib(10))
    {
        std::string src = R"(
export function $fibonacci(%n : i32) : i32 {
@start
    %t0 = sle %n, 1 : i32
    jnz %t0, @base_case, @recursive_case : i32

@base_case
    ret %n : i32

@recursive_case
    %t1 = sub %n, 1 : i32
    %t2 = call $fibonacci(%t1) : i32
    %t3 = sub %n, 2 : i32
    %t4 = call $fibonacci(%t3) : i32
    %t5 = add %t2, %t4 : i32
    ret %t5 : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        runNodeVerification(code, R"(if (i.exports.fibonacci(0) !== 0) process.exit(1); if (i.exports.fibonacci(1) !== 1) process.exit(2); if (i.exports.fibonacci(2) !== 1) process.exit(3); if (i.exports.fibonacci(5) !== 5) process.exit(4); if (i.exports.fibonacci(10) !== 55) process.exit(5);)");
        std::cout << "Fibonacci base-case and recursive execution test (0, 1, 2, 5, 10 -> 0, 1, 1, 5, 55) passed successfully!" << std::endl;
    }

    // Test 6: Gate C Reducible Natural Loop Execution Suite (Sum 0..9 == 45, Zero-Iter, One-Iter, Loop-Carried Accumulator)
    {
        std::string src = R"(
export function $sum_loop() : i32 {
@entry
    %i_init = copy 0 : i32
    %sum_init = copy 0 : i32
    jmp @header : i32

@header
    %i = phi @entry %i_init, @body %i_next : i32
    %sum = phi @entry %sum_init, @body %sum_next : i32
    %cond = sge %i, 10 : i32
    jnz %cond, @exit, @body : i32

@body
    %sum_next = add %sum, %i : i32
    %i_next = add %i, 1 : i32
    jmp @header : i32

@exit
    ret %sum : i32
}

export function $zero_iter_loop() : i32 {
@entry
    %i_init = copy 10 : i32
    %sum_init = copy 0 : i32
    jmp @header : i32

@header
    %i = phi @entry %i_init, @body %i_next : i32
    %sum = phi @entry %sum_init, @body %sum_next : i32
    %cond = sge %i, 10 : i32
    jnz %cond, @exit, @body : i32

@body
    %sum_next = add %sum, %i : i32
    %i_next = add %i, 1 : i32
    jmp @header : i32

@exit
    ret %sum : i32
}

export function $one_iter_loop() : i32 {
@entry
    %i_init = copy 9 : i32
    %sum_init = copy 0 : i32
    jmp @header : i32

@header
    %i = phi @entry %i_init, @body %i_next : i32
    %sum = phi @entry %sum_init, @body %sum_next : i32
    %cond = sge %i, 10 : i32
    jnz %cond, @exit, @body : i32

@body
    %sum_next = add %sum, %i : i32
    %i_next = add %i, 1 : i32
    jmp @header : i32

@exit
    ret %sum : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        // WAT text generation test for loop instructions
        std::stringstream watStream;
        auto targetInfoWat = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenWat(*module, std::move(targetInfoWat), &watStream);
        codeGenWat.emit();
        std::string wat = watStream.str();
        assert(wat.find("block") != std::string::npos);
        assert(wat.find("loop") != std::string::npos);
        assert(wat.find("br_if") != std::string::npos);
        assert(wat.find("br") != std::string::npos);

        // Binary execution test
        auto targetInfoBin = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenBin(*module, std::move(targetInfoBin));
        codeGenBin.emit();

        const auto& code = codeGenBin.getAssembler().getCode();
        std::cout << "Loop module WASM size: " << code.size() << " bytes" << std::endl;

        runNodeVerification(code, R"(if (i.exports.sum_loop() !== 45) process.exit(1); if (i.exports.zero_iter_loop() !== 0) process.exit(2); if (i.exports.one_iter_loop() !== 9) process.exit(3);)");
        std::cout << "Gate C Reducible Natural Loop Execution Suite passed (sum_loop==45, zero_iter==0, one_iter==9)!" << std::endl;
    }

    // Test 7: Gate D Extended Loop Suite (Nested Loops, Multiple Exits, Continue-like Edges, Internal Conditionals)
    {
        std::string src = R"(
export function $nested_loop() : i32 {
@entry
    %i_init = copy 0 : i32
    %count_init = copy 0 : i32
    jmp @out_header : i32

@out_header
    %i = phi @entry %i_init, @out_latch %i_next : i32
    %count = phi @entry %count_init, @out_latch %count_inner : i32
    %c_out = sge %i, 3 : i32
    jnz %c_out, @exit, @in_init : i32

@in_init
    %j_init = copy 0 : i32
    jmp @in_header : i32

@in_header
    %j = phi @in_init %j_init, @in_body %j_next : i32
    %count_in = phi @in_init %count, @in_body %count_in_next : i32
    %c_in = sge %j, 4 : i32
    jnz %c_in, @out_latch, @in_body : i32

@in_body
    %count_in_next = add %count_in, 1 : i32
    %j_next = add %j, 1 : i32
    jmp @in_header : i32

@out_latch
    %count_inner = phi @in_header %count_in : i32
    %i_next = add %i, 1 : i32
    jmp @out_header : i32

@exit
    ret %count : i32
}

export function $multi_exit_loop() : i32 {
@entry
    %i_init = copy 0 : i32
    jmp @header : i32

@header
    %i = phi @entry %i_init, @latch %i_next : i32
    %c1 = sge %i, 20 : i32
    jnz %c1, @normal_exit, @body : i32

@body
    %c_early = eq %i, 7 : i32
    jnz %c_early, @early_exit, @latch : i32

@latch
    %i_next = add %i, 1 : i32
    jmp @header : i32

@normal_exit
    ret 100 : i32

@early_exit
    ret %i : i32
}

export function $continue_loop() : i32 {
@entry
    %i_init = copy 0 : i32
    %sum_init = copy 0 : i32
    jmp @header : i32

@header
    %i = phi @entry %i_init, @latch %i_next : i32
    %sum = phi @entry %sum_init, @latch %sum_next : i32
    %c_exit = sge %i, 10 : i32
    jnz %c_exit, @exit, @body : i32

@body
    %rem = rem %i, 2 : i32
    %is_even = eq %rem, 0 : i32
    jnz %is_even, @latch_skip, @add_odd : i32

@add_odd
    %sum_added = add %sum, %i : i32
    jmp @latch : i32

@latch_skip
    jmp @latch : i32

@latch
    %sum_next = phi @add_odd %sum_added, @latch_skip %sum : i32
    %i_next = add %i, 1 : i32
    jmp @header : i32

@exit
    ret %sum : i32
}

export function $internal_cond_loop() : i32 {
@entry
    %i_init = copy 0 : i32
    %sum_init = copy 0 : i32
    jmp @header : i32

@header
    %i = phi @entry %i_init, @latch %i_next : i32
    %sum = phi @entry %sum_init, @latch %sum_next : i32
    %c_exit = sge %i, 10 : i32
    jnz %c_exit, @exit, @body : i32

@body
    %cond = slt %i, 5 : i32
    jnz %cond, @then, @else : i32

@then
    %sum_then = add %sum, 1 : i32
    jmp @latch : i32

@else
    %sum_else = add %sum, 2 : i32
    jmp @latch : i32

@latch
    %sum_next = phi @then %sum_then, @else %sum_else : i32
    %i_next = add %i, 1 : i32
    jmp @header : i32

@exit
    ret %sum : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        // WAT text generation test
        std::stringstream watStream;
        auto targetInfoWat = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenWat(*module, std::move(targetInfoWat), &watStream);
        codeGenWat.emit();
        std::string wat = watStream.str();
        assert(wat.find("block") != std::string::npos);
        assert(wat.find("loop") != std::string::npos);

        // Binary execution test
        auto targetInfoBin = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGenBin(*module, std::move(targetInfoBin));
        codeGenBin.emit();

        const auto& code = codeGenBin.getAssembler().getCode();
        std::cout << "Gate D Extended Loop module WASM size: " << code.size() << " bytes" << std::endl;

        runNodeVerification(code, R"(if (i.exports.nested_loop() !== 12) process.exit(1); if (i.exports.multi_exit_loop() !== 7) process.exit(2); if (i.exports.continue_loop() !== 25) process.exit(3); if (i.exports.internal_cond_loop() !== 15) process.exit(4);)");
        std::cout << "Gate D Extended Loop Execution Suite passed (nested_loop==12, multi_exit==7, continue_loop==25, internal_cond==15)!" << std::endl;
    }

    // Test 8: Target-Local Irreducible / Unsupported CFG Rejection Test
    {
        std::string src = R"(
export function $irreducible_cfg() : i32 {
@entry
    %cond = copy 1 : i32
    jnz %cond, @b1, @b2 : i32

@b1
    jmp @b2 : i32

@b2
    jmp @b1 : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        bool caught = false;
        try {
            auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
            codegen::CodeGen codeGen(*module, std::move(targetInfo));
            codeGen.emit();
        } catch (const std::runtime_error& ex) {
            std::string msg = ex.what();
            if (msg.find("wasm32: unsupported irreducible/cyclic CFG") != std::string::npos ||
                msg.find("wasm32: unsupported CFG backedge/cycle") != std::string::npos) {
                caught = true;
            }
        }
        assert(caught);
        std::cout << "Target-local irreducible CFG rejection test passed successfully!" << std::endl;
    }

    // Test 9: Real Nested Conditional CFG Execution Test
    {
        std::string src = R"(
export function $nested_if(%x : i32, %y : i32) : i32 {
@start
    %c1 = sgt %x, 0 : i32
    jnz %c1, @pos_x, @neg_x : i32

@pos_x
    %c2 = sgt %y, 0 : i32
    jnz %c2, @both_pos, @pos_x_neg_y : i32

@both_pos
    ret 100 : i32

@pos_x_neg_y
    ret 200 : i32

@neg_x
    ret 300 : i32
}
)";
        std::stringstream ss(src);
        parser::Parser parser(ss, parser::FileFormat::FYRA);
        auto module = parser.parseModule();
        assert(module != nullptr);

        auto targetInfo = target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI});
        codegen::CodeGen codeGen(*module, std::move(targetInfo));
        codeGen.emit();

        const auto& code = codeGen.getAssembler().getCode();
        runNodeVerification(code, R"(if (i.exports.nested_if(5, 5) !== 100) process.exit(1); if (i.exports.nested_if(5, -5) !== 200) process.exit(2); if (i.exports.nested_if(-5, 5) !== 300) process.exit(3);)");
        std::cout << "Real Nested Conditional CFG execution test (100, 200, 300) passed successfully!" << std::endl;
    }

    std::cout << "All WASM target execution and verification tests passed successfully!" << std::endl;
    return 0;
}
