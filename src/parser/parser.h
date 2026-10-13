#pragma once

#include <vector>

#include "lexer/token.h"
#include "parser/ast.h"

// Parses a token stream into statements. Throws SyntaxError.
std::vector<NodeP> parseProgram(const std::vector<Token>& tokens);
