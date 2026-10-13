#include "interpreter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cas/rewrite.h"
#include "common/error.h"
#include "compiler/compiler.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

bool readSourceFile(const std::string& path, std::string& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

Interpreter::Interpreter() : err_(&std::cerr) {
    cas::resetRewriteState();
    vm_.importLoader = [this](const std::string& path) -> ObjFunction* {
        std::string source;
        if (!readSourceFile(path, source)) fail("import: cannot read '" + path + "'");
        std::string shown = std::filesystem::u8path(path).filename().u8string();
        try {
            std::vector<Token> tokens = lex(source);
            std::vector<NodeP> program = parseProgram(tokens);
            return compileProgram(vm_, program, false, path);
        } catch (const SyntaxError& e) {
            fail(shown + ":" + std::to_string(e.line) + ":" + std::to_string(e.col) + ": syntax error: " + e.what());
        }
    };
}

RunStatus Interpreter::run(const std::string& source, const std::string& sourceName, bool allowIncomplete) {
    ObjFunction* script = nullptr;
    try {
        std::vector<Token> tokens = lex(source);
        std::vector<NodeP> program = parseProgram(tokens);
        script = compileProgram(vm_, program, true, sourceName);
    } catch (const SyntaxError& e) {
        if (e.incomplete && allowIncomplete) return RunStatus::Incomplete;
        *err_ << sourceName << ":" << e.line << ":" << e.col << ": syntax error: " << e.what() << "\n";
        return RunStatus::CompileError;
    } catch (const SedError& e) {
        *err_ << sourceName << ": error: " << e.what() << "\n";
        return RunStatus::CompileError;
    }
    if (disassembleOnly) {
        disassemble(script, *vm_.out);
        return RunStatus::Ok;
    }
    try {
        vm_.execute(script);
        vm_.out->flush();
        return RunStatus::Ok;
    } catch (const std::exception& e) {
        vm_.out->flush();
        *err_ << "error: " << e.what() << "\n" << vm_.stackTrace();
        vm_.resetAfterError();
        return RunStatus::RuntimeError;
    }
}
