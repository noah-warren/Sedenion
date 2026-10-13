#pragma once

#include <functional>

class VM;
struct Value;

// Converts a callable, a symbolic expression in `var`, or a constant into a real function.
// `var` may be nullptr when f is a callable or an expression with a single free symbol.
std::function<double(double)> makeRealFunction(VM& vm, const Value& f, const Value* var);

double integrateNumeric(const std::function<double(double)>& f, double a, double b);

void registerCalculus(VM& vm);
