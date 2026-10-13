#pragma once

#include <stdexcept>
#include <string>

// Runtime/math error raised anywhere in the interpreter; reported with a stack trace by the VM.
struct SedError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Lexing, parsing and compile errors. `incomplete` marks errors caused by reaching end of input,
// which lets the REPL ask for another line instead of reporting the error.
struct SyntaxError : std::runtime_error {
    int line;
    int col;
    bool incomplete;
    SyntaxError(const std::string& msg, int l, int c, bool inc = false)
        : std::runtime_error(msg), line(l), col(c), incomplete(inc) {}
};

[[noreturn]] inline void fail(const std::string& msg) { throw SedError(msg); }
