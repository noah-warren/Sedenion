#pragma once

#include <string>

#include "cas/expr.h"
#include "vm/arith.h"
#include "vm/object.h"

// Conversions between VM values and CAS expressions.
cas::E toExpr(const Value& v);
Value fromExpr(const cas::E& e);

Value symBinary(BinOp op, const Value& a, const Value& b);
Value symEquation(const Value& a, const Value& b);
Value symFunction(const std::string& name, const Value& arg);
bool symTruth(const Value& v);
bool symToDouble(const Value& v, double& out);  // true when v is a numeric constant like π/2
Value simplifyValue(const Value& v);

class VM;
void registerCas(VM& vm);
