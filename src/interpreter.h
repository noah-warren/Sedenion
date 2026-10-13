#pragma once

#include <iosfwd>
#include <string>

#include "vm/vm.h"

enum class RunStatus { Ok, CompileError, RuntimeError, Incomplete };

// Reads a UTF-8 source file (dropping a byte-order mark). Returns false if it cannot be opened.
bool readSourceFile(const std::string& path, std::string& out);

// Runs source text through lexer -> parser -> compiler -> VM and reports errors to `err`.
// Top-level expression statements print their value.
class Interpreter {
public:
    Interpreter();
    // With allowIncomplete, input that ends mid-statement returns Incomplete instead of an error.
    RunStatus run(const std::string& source, const std::string& sourceName = "<input>", bool allowIncomplete = false);

    VM& vm() { return vm_; }
    void setOutput(std::ostream& out) { vm_.out = &out; }
    void setErrorStream(std::ostream& err) { err_ = &err; }
    bool disassembleOnly = false;

private:
    VM vm_;
    std::ostream* err_;
};
