#pragma once

#include "vm/object.h"

class VM;

void registerStdlib(VM& vm);
void registerCore(VM& vm);
void registerMath(VM& vm);

// Math helpers shared with the VM's built-in operators (√, ∛, |x|, n!).
Value sqrtValue(const Value& v);
Value cbrtValue(const Value& v);
Value absValue(const Value& v);
Value factorialValue(const Value& v);
