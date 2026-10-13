#pragma once

#include "numeric/rational.h"
#include "vm/arith.h"
#include "vm/object.h"

// Square root of a non-negative rational, with square factors pulled out.
// sqrt(8) is 2*sqrt(2); a perfect square is an ordinary rational.
Value sqrtExact(const Rational& r);

Value radicalBinary(BinOp op, const Value& a, const Value& b);
Value radicalNeg(const Value& v);
Value radicalAbs(const Value& v);
Value radicalAsSym(const Value& v);

double radicalToDouble(const Value& v);
int radicalCompare(const Value& a, const Value& b);
bool radicalEqual(const Value& a, const Value& b);
std::string radicalToString(const ObjRadical* r);
