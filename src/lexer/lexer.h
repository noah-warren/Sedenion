#pragma once

#include <string>
#include <vector>

#include "lexer/token.h"

// Tokenizes UTF-8 source. Newlines inside brackets, or after a token that cannot end an
// expression, are dropped so expressions can span lines. Throws SyntaxError.
std::vector<Token> lex(const std::string& src);
