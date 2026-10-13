#pragma once

#include "vm/arith.h"
#include "vm/object.h"

Value matrixBinary(BinOp op, const Value& a, const Value& b);
Value transposeValue(const Value& v);
Value determinant(const Value& m);
Value inverse(const Value& m);
Value solveLinear(const Value& A, const Value& b);
Value vectorNorm(const Value& v);
Value dotProduct(const Value& a, const Value& b);
Value crossProduct(const Value& a, const Value& b);
Value matrixFromValue(const Value& v);  // matrix, or list of equal-length lists

class VM;
void registerLinalg(VM& vm);
