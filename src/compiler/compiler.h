#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "parser/ast.h"

class VM;
struct ObjFunction;

// Compiles a parsed program into a script function. When printExprs is set, top-level
// expression statements print their value (REPL and script behaviour). `source` names the
// file, which imports resolve against. Throws SyntaxError.
ObjFunction* compileProgram(VM& vm, std::vector<NodeP>& program, bool printExprs, const std::string& source = "");

void disassemble(const ObjFunction* fn, std::ostream& os);
