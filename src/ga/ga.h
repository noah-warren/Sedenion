#pragma once

#include "vm/arith.h"
#include "vm/object.h"

// Geometric algebra: multivectors in Cl(p, q), with p + q <= 6.
Value multiNeg(const Value& a);
Value multiBinary(BinOp op, const Value& a, const Value& b);
Value multiReverse(const Value& a);
Value multiGrade(const Value& a, int k);
Value multiWedge(const Value& a, const Value& b);
Value multiInner(const Value& a, const Value& b);
bool multiEqual(const Value& a, const Value& b);
std::string multiToString(const ObjMulti* m);

class VM;
void registerGa(VM& vm);
