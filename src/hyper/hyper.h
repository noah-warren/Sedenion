#pragma once

#include <string>
#include <vector>

#include "vm/arith.h"
#include "vm/object.h"

// Hypercomplex numbers from the Cayley-Dickson construction: quaternions (4 components),
// octonions (8), sedenions (16) and beyond. Reals and complex numbers embed as the
// lower-dimensional algebras, so they mix freely with hypercomplex values.

// Builds a hypercomplex value; one whose imaginary components are all zero collapses to its real part.
Value makeHyper(std::vector<Value> components);
Value hyperBinary(BinOp op, const Value& a, const Value& b);
Value hyperNeg(const Value& a);
Value hyperConj(const Value& a);
Value hyperNorm(const Value& a);  // |q| = sqrt(sum of squared components)
bool hyperEqual(const Value& a, const Value& b);
std::string hyperToString(const ObjHyper* h);
std::string hyperTypeName(const ObjHyper* h);

class VM;
void registerHyper(VM& vm);
