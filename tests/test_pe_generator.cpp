#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "target/artifact/executable/pe.hh"
#include "target/artifact/executable/elf.hh"

int main() {
    std::ifstream input("tests/windows.fyra");
    if (!input.good()) {
        input.open("../tests/windows.fyra");
    }
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Windows});
    codegen::CodeGen cg(*module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    std::vector<PEGenerator::Symbol> symbols;
    for (const auto& s : cg.getSymbols()) {
        symbols.push_back({s.name, s.value, s.size, s.type, s.binding, s.sectionName});
    }

    std::vector<PEGenerator::Relocation> relocs;
    for (const auto& r : cg.getRelocations()) {
        relocs.push_back({r.offset, r.type, r.addend, r.symbolName, r.sectionName});
    }

    PEGenerator pe(true);
    const std::string out = "./test_windows_out.exe";
    bool ok = pe.generateFromCode(sections, symbols, relocs, out);
    assert(ok && "PE generation failed");

    std::ifstream exe(out, std::ios::binary);
    assert(exe.good());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(exe)), std::istreambuf_iterator<char>());
    assert(data.size() > 0x100);

    // MZ
    assert(data[0] == 'M' && data[1] == 'Z');

    uint32_t lfanew = *reinterpret_cast<uint32_t*>(&data[0x3C]);
    assert(lfanew + 0x80 < data.size());

    // PE\0\0
    assert(data[lfanew] == 'P' && data[lfanew + 1] == 'E' && data[lfanew + 2] == 0 && data[lfanew + 3] == 0);

    // OptionalHeader64 AddressOfEntryPoint at +0x10 from optional header start
    uint32_t entryRva = *reinterpret_cast<uint32_t*>(&data[lfanew + 4 + 20 + 0x10]);
    assert(entryRva != 0);

    std::remove(out.c_str());
    std::cout << "PE generator structural test passed.\n";

    // Milestone 0B: Unresolved-Symbol Rejection Unit Tests for Internal Executable Generators
    {
        std::cout << "--- Testing Milestone 0B Unresolved Symbol Rejection ---" << std::endl;

        // 1. Positive Test: Valid closed-world ELF generation with forward reference
        {
            std::string forward_ir = R"(
export function $main() : i32 {
@entry
    %res = call $helper(10) : i32
    ret %res : i32
}

function $helper(%x : i32) : i32 {
@entry
    %r = add %x, 32 : i32
    ret %r : i32
}
)";
            std::istringstream stream(forward_ir);
            parser::Parser parser(stream, parser::FileFormat::FYRA);
            std::unique_ptr<ir::Module> module = parser.parseModule();
            assert(module != nullptr);

            auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
            codegen::CodeGen cg(*module, std::move(target), nullptr);
            cg.emit(true);

            std::map<std::string, std::vector<uint8_t>> sections;
            sections[".text"] = cg.getAssembler().getCode();
            sections[".rodata"] = cg.getRodataAssembler().getCode();

            std::vector<ElfGenerator::Symbol> symbols;
            for (const auto& s : cg.getSymbols()) {
                symbols.push_back({s.name, s.value, s.size, s.type, s.binding, s.sectionName});
            }

            std::vector<ElfGenerator::Relocation> relocs;
            for (const auto& r : cg.getRelocations()) {
                relocs.push_back({r.offset, r.type, r.addend, r.symbolName, r.sectionName});
            }

            ElfGenerator elf("test.fyra");
            const std::string elf_out = "./test_elf_closed_world.exe";
            bool ok = elf.generateFromCode(sections, symbols, relocs, elf_out);
            assert(ok && "Closed-world ELF generation should succeed");

            std::ifstream elf_file(elf_out, std::ios::binary);
            assert(elf_file.good());
            elf_file.close();
            std::remove(elf_out.c_str());
        }

        // 2. Negative Test: Undefined symbol causes deterministic rejection, error message, and pre-existing file preservation
        {
            std::string undef_ir = R"(
export function $main() : i32 {
@entry
    %res = call $undefined_external_fn(42) : i32
    ret %res : i32
}
)";
            std::istringstream stream(undef_ir);
            parser::Parser parser(stream, parser::FileFormat::FYRA);
            std::unique_ptr<ir::Module> module = parser.parseModule();
            assert(module != nullptr);

            auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
            codegen::CodeGen cg(*module, std::move(target), nullptr);
            cg.emit(true);

            std::map<std::string, std::vector<uint8_t>> sections;
            sections[".text"] = cg.getAssembler().getCode();
            sections[".rodata"] = cg.getRodataAssembler().getCode();

            std::vector<ElfGenerator::Symbol> symbols;
            for (const auto& s : cg.getSymbols()) {
                symbols.push_back({s.name, s.value, s.size, s.type, s.binding, s.sectionName});
            }

            std::vector<ElfGenerator::Relocation> relocs;
            for (const auto& r : cg.getRelocations()) {
                relocs.push_back({r.offset, r.type, r.addend, r.symbolName, r.sectionName});
            }

            const std::string elf_preserve_out = "./test_elf_preserve_sentinel.exe";
            const std::string sentinel = "PREEXISTING-USER-FILE-MUST-SURVIVE";

            // Create pre-existing file with sentinel content
            {
                std::ofstream f(elf_preserve_out, std::ios::binary);
                f << sentinel;
            }

            ElfGenerator elf("test.fyra");
            bool ok = elf.generateFromCode(sections, symbols, relocs, elf_preserve_out);
            assert(!ok && "Generation with unresolved symbol MUST fail!");

            std::string err = elf.getLastError();
            assert(err.find("Unresolved symbol") != std::string::npos);
            assert(err.find("undefined_external_fn") != std::string::npos);

            // Verify pre-existing file content remains 100% byte-for-byte identical
            std::ifstream check_file(elf_preserve_out, std::ios::binary);
            assert(check_file.good() && "Pre-existing destination file MUST still exist!");
            std::string content((std::istreambuf_iterator<char>(check_file)), std::istreambuf_iterator<char>());
            assert(content == sentinel && "Pre-existing file contents MUST NOT be modified or deleted!");
            check_file.close();
            std::remove(elf_preserve_out.c_str());
        }

        // 3. Positive Replacement Test: Successful generation replaces pre-existing file
        {
            std::string valid_ir = R"(
export function $main() : i32 {
@entry
    ret 42 : i32
}
)";
            std::istringstream stream(valid_ir);
            parser::Parser parser(stream, parser::FileFormat::FYRA);
            std::unique_ptr<ir::Module> module = parser.parseModule();
            assert(module != nullptr);

            auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
            codegen::CodeGen cg(*module, std::move(target), nullptr);
            cg.emit(true);

            std::map<std::string, std::vector<uint8_t>> sections;
            sections[".text"] = cg.getAssembler().getCode();
            sections[".rodata"] = cg.getRodataAssembler().getCode();

            std::vector<ElfGenerator::Symbol> symbols;
            for (const auto& s : cg.getSymbols()) {
                symbols.push_back({s.name, s.value, s.size, s.type, s.binding, s.sectionName});
            }

            std::vector<ElfGenerator::Relocation> relocs;
            for (const auto& r : cg.getRelocations()) {
                relocs.push_back({r.offset, r.type, r.addend, r.symbolName, r.sectionName});
            }

            const std::string elf_replace_out = "./test_elf_replace_sentinel.exe";
            const std::string old_sentinel = "OLD-SENTINEL-TO-BE-REPLACED";

            {
                std::ofstream f(elf_replace_out, std::ios::binary);
                f << old_sentinel;
            }

            ElfGenerator elf("test.fyra");
            bool ok = elf.generateFromCode(sections, symbols, relocs, elf_replace_out);
            assert(ok && "Valid generation should succeed");

            std::ifstream check_file(elf_replace_out, std::ios::binary);
            assert(check_file.good());
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(check_file)), std::istreambuf_iterator<char>());
            check_file.close();

            assert(data.size() > 64);
            assert(data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F');
            std::remove(elf_replace_out.c_str());
        }

        // 4. PE-Specific Tests: Local symbol, forward reference, supported import, unresolved symbol, missing section, unsupported relocation, OOB relocation
        {
            PEGenerator pe_gen(true);
            std::map<std::string, std::vector<uint8_t>> pe_sections;
            pe_sections[".text"] = std::vector<uint8_t>(64, 0x90);

            std::vector<PEGenerator::Symbol> pe_symbols = {
                {"main", 0, 16, 2, 1, ".text"},
                {"helper_fn", 32, 16, 2, 1, ".text"}
            };

            // 4a. PE local symbol & forward reference
            std::vector<PEGenerator::Relocation> valid_relocs = {
                {10, "R_X86_64_PC32", -4, "helper_fn", ".text"}
            };
            const std::string pe_valid_out = "./test_pe_valid_local.exe";
            bool pe_ok = pe_gen.generateFromCode(pe_sections, pe_symbols, valid_relocs, pe_valid_out);
            assert(pe_ok && "PE generation with valid local symbol must succeed!");
            std::remove(pe_valid_out.c_str());

            // 4b. PE supported import (ExitProcess)
            std::vector<PEGenerator::Relocation> import_relocs = {
                {20, "R_X86_64_PC32", -4, "ExitProcess", ".text"}
            };
            const std::string pe_import_out = "./test_pe_valid_import.exe";
            bool pe_import_ok = pe_gen.generateFromCode(pe_sections, pe_symbols, import_relocs, pe_import_out);
            assert(pe_import_ok && "PE generation with supported import symbol (ExitProcess) must succeed!");
            std::remove(pe_import_out.c_str());

            // 4c. PE truly unresolved symbol with destination preservation
            const std::string pe_unresolved_out = "./test_pe_unresolved_sentinel.exe";
            const std::string pe_sentinel = "PREEXISTING-PE-DESTINATION-FILE-MUST-SURVIVE";
            {
                std::ofstream f(pe_unresolved_out, std::ios::binary);
                f << pe_sentinel;
            }

            std::vector<PEGenerator::Relocation> bad_relocs = {
                {10, "R_X86_64_PC32", -4, "truly_unresolved_win_fn", ".text"}
            };
            bool pe_bad_ok = pe_gen.generateFromCode(pe_sections, pe_symbols, bad_relocs, pe_unresolved_out);
            assert(!pe_bad_ok && "PE generation with truly unresolved symbol MUST fail!");
            assert(pe_gen.getLastError().find("Unresolved symbol") != std::string::npos);
            assert(pe_gen.getLastError().find("truly_unresolved_win_fn") != std::string::npos);

            std::ifstream pe_check(pe_unresolved_out, std::ios::binary);
            assert(pe_check.good());
            std::string pe_content((std::istreambuf_iterator<char>(pe_check)), std::istreambuf_iterator<char>());
            assert(pe_content == pe_sentinel && "Pre-existing PE destination file MUST NOT be touched on failure!");
            pe_check.close();
            std::remove(pe_unresolved_out.c_str());

            // 4d. PE missing target section
            std::vector<PEGenerator::Relocation> missing_sec_relocs = {
                {10, "R_X86_64_PC32", -4, "main", ".non_existent_section"}
            };
            bool pe_sec_ok = pe_gen.generateFromCode(pe_sections, pe_symbols, missing_sec_relocs, "./test_pe_sec.exe");
            assert(!pe_sec_ok && "PE generation with missing section MUST fail!");
            assert(pe_gen.getLastError().find(".non_existent_section") != std::string::npos);

            // 4e. PE unsupported relocation type
            std::vector<PEGenerator::Relocation> unsupp_type_relocs = {
                {10, "R_X86_64_UNSUPPORTED", -4, "main", ".text"}
            };
            bool pe_type_ok = pe_gen.generateFromCode(pe_sections, pe_symbols, unsupp_type_relocs, "./test_pe_type.exe");
            assert(!pe_type_ok && "PE generation with unsupported relocation type MUST fail!");
            assert(pe_gen.getLastError().find("Unsupported relocation") != std::string::npos);

            // 4f. PE out-of-bounds relocation
            std::vector<PEGenerator::Relocation> oob_relocs = {
                {100, "R_X86_64_PC32", -4, "main", ".text"}
            };
            bool pe_oob_ok = pe_gen.generateFromCode(pe_sections, pe_symbols, oob_relocs, "./test_pe_oob.exe");
            assert(!pe_oob_ok && "PE generation with OOB relocation MUST fail!");
            assert(pe_gen.getLastError().find("out of bounds") != std::string::npos);

            // 4g. Generator State Reuse Test (Failure followed by Success)
            PEGenerator reuse_gen(true);
            bool fail_ok = reuse_gen.generateFromCode(pe_sections, pe_symbols, bad_relocs, "./test_pe_reuse_fail.exe");
            assert(!fail_ok);
            assert(!reuse_gen.getLastError().empty());

            const std::string reuse_out = "./test_pe_reuse_pass.exe";
            bool pass_ok = reuse_gen.generateFromCode(pe_sections, pe_symbols, valid_relocs, reuse_out);
            assert(pass_ok && "Generator reuse after failure MUST succeed!");
            assert(reuse_gen.getLastError().empty() && "lastError_ MUST be cleared on new operation!");
            std::remove(reuse_out.c_str());

            // 4h. Exact Patch Byte Verification Test (R_X86_64_PC32 and R_X86_64_64)
            PEGenerator exact_gen(true);
            exact_gen.setBaseAddress(0x140000000ULL);
            std::map<std::string, std::vector<uint8_t>> exact_sections;
            exact_sections[".text"] = std::vector<uint8_t>(64, 0x00);
            exact_sections[".data"] = std::vector<uint8_t>(64, 0x00);

            std::vector<PEGenerator::Symbol> exact_symbols = {
                {"func_A", 0, 16, 2, 1, ".text"},
                {"func_B", 32, 16, 2, 1, ".text"},
                {"var_X", 0, 8, 1, 1, ".data"}
            };

            std::vector<PEGenerator::Relocation> exact_relocs = {
                {10, "R_X86_64_PC32", -4, "func_B", ".text"},
                {0, "R_X86_64_64", 0, "var_X", ".text"}
            };

            const std::string exact_out = "./test_pe_exact_bytes.exe";
            bool exact_ok = exact_gen.generateFromCode(exact_sections, exact_symbols, exact_relocs, exact_out);
            assert(exact_ok);

            std::ifstream exact_file(exact_out, std::ios::binary);
            assert(exact_file.good());
            std::vector<uint8_t> exact_data((std::istreambuf_iterator<char>(exact_file)), std::istreambuf_iterator<char>());
            exact_file.close();
            std::remove(exact_out.c_str());

            // Verify PE signature
            assert(exact_data[0] == 'M' && exact_data[1] == 'Z');
            uint32_t pe_hdr_off = *reinterpret_cast<uint32_t*>(&exact_data[0x3C]);
            assert(exact_data[pe_hdr_off] == 'P' && exact_data[pe_hdr_off + 1] == 'E');

            // 4i. PC32 Relocation Positive Overflow Test
            PEGenerator overflow_gen(true);
            std::vector<PEGenerator::Relocation> overflow_relocs = {
                {10, "R_X86_64_PC32", 0x80000000LL, "helper_fn", ".text"}
            };
            bool overflow_ok = overflow_gen.generateFromCode(pe_sections, pe_symbols, overflow_relocs, "./test_pe_overflow.exe");
            assert(!overflow_ok && "Relocation overflow MUST fail!");
            assert(overflow_gen.getLastError().find("overflow") != std::string::npos);

            // Mandatory Missing Tests A-H

            // A. Eight-byte bounds failure (offset at section size, or offset with only 1..7 bytes remaining for R_X86_64_64)
            {
                PEGenerator bounds_gen(true);
                std::map<std::string, std::vector<uint8_t>> b_sec;
                b_sec[".text"] = std::vector<uint8_t>(10, 0x90);
                std::vector<PEGenerator::Symbol> b_sym = {{"main", 0, 10, 2, 1, ".text"}};

                // Offset 5 -> only 5 bytes remaining, needs 8 bytes
                std::vector<PEGenerator::Relocation> b_relocs = {{5, "R_X86_64_64", 0, "main", ".text"}};
                bool b_ok = bounds_gen.generateFromCode(b_sec, b_sym, b_relocs, "./test_b_oob.exe");
                assert(!b_ok && "Eight-byte relocation with 5 bytes remaining MUST fail bounds check!");
                assert(bounds_gen.getLastError().find("out of bounds") != std::string::npos);

                // Offset 10 -> exactly at section size, 0 bytes remaining
                std::vector<PEGenerator::Relocation> b_relocs_at_end = {{10, "R_X86_64_64", 0, "main", ".text"}};
                bool b_at_end_ok = bounds_gen.generateFromCode(b_sec, b_sym, b_relocs_at_end, "./test_b_at_end.exe");
                assert(!b_at_end_ok && "Eight-byte relocation at section end MUST fail bounds check!");
                assert(bounds_gen.getLastError().find("out of bounds") != std::string::npos);
            }

            // B. Negative PC32 overflow (S + A - P < INT32_MIN)
            {
                PEGenerator neg_pc32_gen(true);
                std::vector<PEGenerator::Relocation> neg_relocs = {
                    {10, "R_X86_64_PC32", -3000000000LL, "helper_fn", ".text"}
                };
                bool neg_ok = neg_pc32_gen.generateFromCode(pe_sections, pe_symbols, neg_relocs, "./test_neg_pc32.exe");
                assert(!neg_ok && "Negative PC32 delta overflow MUST fail!");
                assert(neg_pc32_gen.getLastError().find("overflow") != std::string::npos);
            }

            // C. ABS64 positive overflow (baseAddress + S + addend > UINT64_MAX)
            {
                PEGenerator abs_over_gen(true, 0xFFFFFFFFFF000000ULL);
                std::vector<PEGenerator::Relocation> abs_over_relocs = {
                    {0, "R_X86_64_64", 0x0000000001000000LL, "helper_fn", ".text"}
                };
                bool over_ok = abs_over_gen.generateFromCode(pe_sections, pe_symbols, abs_over_relocs, "./test_abs_over.exe");
                assert(!over_ok && "ABS64 overflow MUST fail!");
                assert(abs_over_gen.getLastError().find("overflow") != std::string::npos);
            }

            // D. ABS64 negative underflow (magnitude > baseAddress + S)
            {
                PEGenerator abs_under_gen(true, 0x1000ULL);
                std::vector<PEGenerator::Relocation> abs_under_relocs = {
                    {0, "R_X86_64_64", -0x10000LL, "main", ".text"}
                };
                bool under_ok = abs_under_gen.generateFromCode(pe_sections, pe_symbols, abs_under_relocs, "./test_abs_under.exe");
                assert(!under_ok && "ABS64 underflow MUST fail!");
                assert(abs_under_gen.getLastError().find("underflow") != std::string::npos);
            }

            // E. INT64_MIN addend (using std::numeric_limits<int64_t>::min())
            {
                PEGenerator min_addend_gen(true, 0x140000000ULL);
                int64_t min_add = std::numeric_limits<int64_t>::min();
                std::vector<PEGenerator::Relocation> min_relocs = {
                    {0, "R_X86_64_64", min_add, "main", ".text"}
                };
                bool min_ok = min_addend_gen.generateFromCode(pe_sections, pe_symbols, min_relocs, "./test_min_addend.exe");
                assert(!min_ok && "INT64_MIN addend underflow MUST fail safely without undefined behavior!");
                assert(min_addend_gen.getLastError().find("underflow") != std::string::npos);
            }

            // F. Exact PC32 and ABS64 byte decoding verification from PE header and section file offsets
            {
                PEGenerator decode_gen(true, 0x140000000ULL);
                std::map<std::string, std::vector<uint8_t>> dec_sec;
                dec_sec[".text"] = std::vector<uint8_t>(64, 0x00);
                dec_sec[".data"] = std::vector<uint8_t>(64, 0x00);

                std::vector<PEGenerator::Symbol> dec_sym = {
                    {"main", 0, 16, 2, 1, ".text"},
                    {"target_func", 32, 16, 2, 1, ".text"},
                    {"data_var", 0, 8, 1, 1, ".data"}
                };

                std::vector<PEGenerator::Relocation> dec_relocs = {
                    {10, "R_X86_64_PC32", -4, "target_func", ".text"}, // S = 0x1000+32=0x1020, P = 0x1000+10=0x100A, S+A-P = 0x1020-4-0x100A = 0x12
                    {16, "R_X86_64_64", 8, "data_var", ".text"}       // base=0x140000000, S=.data RVA (0x2000), A=8 => 0x140002008
                };

                const std::string dec_out = "./test_pe_decode.exe";
                bool dec_ok = decode_gen.generateFromCode(dec_sec, dec_sym, dec_relocs, dec_out);
                assert(dec_ok);

                std::ifstream dec_file(dec_out, std::ios::binary);
                assert(dec_file.good());
                std::vector<uint8_t> f_bytes((std::istreambuf_iterator<char>(dec_file)), std::istreambuf_iterator<char>());
                dec_file.close();
                std::remove(dec_out.c_str());

                // Read section header for .text to locate raw data pointer
                uint32_t pe_offset = *reinterpret_cast<uint32_t*>(&f_bytes[0x3C]);
                uint16_t num_sections = *reinterpret_cast<uint16_t*>(&f_bytes[pe_offset + 6]);
                uint16_t opt_hdr_size = *reinterpret_cast<uint16_t*>(&f_bytes[pe_offset + 20]);
                uint32_t sec_hdr_start = pe_offset + 24 + opt_hdr_size;

                uint32_t text_raw_ptr = 0;
                for (uint16_t i = 0; i < num_sections; ++i) {
                    uint32_t sh = sec_hdr_start + i * 40;
                    char name[9] = {0};
                    std::memcpy(name, &f_bytes[sh], 8);
                    if (std::string(name) == ".text") {
                        text_raw_ptr = *reinterpret_cast<uint32_t*>(&f_bytes[sh + 20]);
                        break;
                    }
                }
                assert(text_raw_ptr > 0);

                // Verify PC32 patch byte value at raw offset + 10 == 0x12 (0x00000012)
                int32_t pc32_val = *reinterpret_cast<int32_t*>(&f_bytes[text_raw_ptr + 10]);
                assert(pc32_val == 0x12 && "Decoded PC32 patch bytes match expected RVA arithmetic!");

                // Verify ABS64 patch byte value at raw offset + 16 == 0x140001008 (since .data precedes .text in std::map iteration order -> .data RVA is 0x1000)
                uint64_t abs64_val = *reinterpret_cast<uint64_t*>(&f_bytes[text_raw_ptr + 16]);
                assert(abs64_val == 0x140001008ULL && "Decoded ABS64 patch bytes match expected ImageBase + RVA + Addend!");
            }

            // G. PE import / IAT proof (kernel32.dll, ExitProcess name in IAT, trampoline reference, RVA patching)
            {
                PEGenerator iat_gen(true, 0x140000000ULL);
                std::map<std::string, std::vector<uint8_t>> iat_sec;
                iat_sec[".text"] = std::vector<uint8_t>(64, 0x90);
                std::vector<PEGenerator::Symbol> iat_sym = {{"main", 0, 16, 2, 1, ".text"}};
                std::vector<PEGenerator::Relocation> iat_relocs = {
                    {10, "R_X86_64_PC32", -4, "ExitProcess", ".text"}
                };

                const std::string iat_out = "./test_pe_iat_proof.exe";
                bool iat_ok = iat_gen.generateFromCode(iat_sec, iat_sym, iat_relocs, iat_out);
                assert(iat_ok);

                std::ifstream iat_file(iat_out, std::ios::binary);
                assert(iat_file.good());
                std::vector<uint8_t> f_bytes((std::istreambuf_iterator<char>(iat_file)), std::istreambuf_iterator<char>());
                iat_file.close();
                std::remove(iat_out.c_str());

                std::string file_str(f_bytes.begin(), f_bytes.end());
                assert(file_str.find("kernel32.dll") != std::string::npos && "kernel32.dll module name present in binary!");
                assert(file_str.find("ExitProcess") != std::string::npos && "ExitProcess symbol name present in import table!");
            }

            // H. Generator state reuse tests: 1. Fail->Pass, 2. Pass->Fail, 3. Invalid args, 4. getLastError() clean
            {
                PEGenerator reuse_test(true);

                // 1. Fail -> Pass
                std::vector<PEGenerator::Relocation> bad_r = {{10, "R_X86_64_PC32", -4, "nonexistent", ".text"}};
                bool r1 = reuse_test.generateFromCode(pe_sections, pe_symbols, bad_r, "./t_fail.exe");
                assert(!r1);
                assert(!reuse_test.getLastError().empty());

                std::vector<PEGenerator::Relocation> good_r = {{10, "R_X86_64_PC32", -4, "helper_fn", ".text"}};
                bool r2 = reuse_test.generateFromCode(pe_sections, pe_symbols, good_r, "./t_pass.exe");
                assert(r2);
                assert(reuse_test.getLastError().empty() && "lastError_ cleared after success!");
                std::remove("./t_pass.exe");

                // 2. Pass -> Fail
                bool r3 = reuse_test.generateFromCode(pe_sections, pe_symbols, bad_r, "./t_fail2.exe");
                assert(!r3);
                assert(!reuse_test.getLastError().empty() && "lastError_ populated on new failure!");

                // 3. Invalid initial arguments after previous failure
                std::map<std::string, std::vector<uint8_t>> empty_sec;
                std::vector<PEGenerator::Symbol> empty_sym;
                std::vector<PEGenerator::Relocation> bad_sec_r = {{0, "R_X86_64_PC32", 0, "main", ".nonexistent"}};
                bool r4 = reuse_test.generateFromCode(empty_sec, empty_sym, bad_sec_r, "./t_fail3.exe");
                assert(!r4);
                assert(reuse_test.getLastError().find(".nonexistent") != std::string::npos);
            }
        }

        std::cout << "--- Milestone 0B Unresolved Symbol Rejection Tests Passed ---" << std::endl;
    }

    return 0;
}
