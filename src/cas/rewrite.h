#pragma once

#include "cas/expr.h"

namespace cas {

// sqrt(x^2) becomes x or -x when an assumption says which. Other expressions are unchanged.
E applyAssumptions(const E& e);

struct Rule {
    std::string name;
    E pattern, replacement;
};

void defineRule(const std::string& name, const E& pattern, const E& replacement);
E rewriteExpr(const E& e, const std::vector<std::string>& only);

// Rules and assumptions belong to one session. A new interpreter starts with none.
void resetRewriteState();

}  // namespace cas

class VM;
void registerRewrite(VM& vm);
