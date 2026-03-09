#pragma once

#include "parser/Lexer.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include <memory>
#include <map>

namespace parser {

// File format enumeration
enum class FileFormat {
    FYRA,     // .fyra files (Fyra format)  
    FY        // .fy files (Fyra format)
};

class Parser {
public:
    Parser(std::istream& input);
    Parser(std::istream& input, FileFormat format);

    std::unique_ptr<ir::Module> parseModule();

private:
    void getNextToken();

    // Parsing methods for different constructs
    void parseFunction();
    void parseData();
    void parseType();
    std::vector<ir::Type*> parseStructElements();

    void parseBasicBlock(ir::Function* func);
    ir::Instruction* parseInstruction(ir::BasicBlock* bb);
    ir::Instruction* parseCallInstruction(ir::Type* retType);
    ir::Value* parseValue();
    ir::Type* parseIRType();

    Lexer lexer;
    ir::IRBuilder builder;
    Token currentToken;
    std::unique_ptr<ir::Module> module;
    FileFormat fileFormat;

    // Symbol table to map temporary names to IR Values
    std::map<std::string, ir::Value*> valueMap;
    // Symbol table for basic block labels within a function
    std::map<std::string, ir::BasicBlock*> labelMap;
};

} // namespace parser
