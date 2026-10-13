#pragma once

#include <functional>

#include "vm/object.h"

enum class BinOp { Add, Sub, Mul, Div, Mod, Pow, LDiv, Dot, Cross };
enum class CmpOp { Eq, Ne, Lt, Le, Gt, Ge };

const char* binOpSymbol(BinOp op);

// Generic arithmetic over every value kind: numbers, lists, matrices, sets, quantities,
// symbolic expressions and strings. Broadcasting rules live here.
Value binaryOp(BinOp op, const Value& a, const Value& b);
Value negateValue(const Value& a);
Value compareOp(CmpOp op, const Value& a, const Value& b);
bool isZeroValue(const Value& v);

// Applies f to every element of a list/matrix/range (recursively), or to v itself otherwise.
Value mapElements(const Value& v, const std::function<Value(const Value&)>& f);

inline Value vadd(const Value& a, const Value& b) { return binaryOp(BinOp::Add, a, b); }
inline Value vsub(const Value& a, const Value& b) { return binaryOp(BinOp::Sub, a, b); }
inline Value vmul(const Value& a, const Value& b) { return binaryOp(BinOp::Mul, a, b); }
inline Value vdiv(const Value& a, const Value& b) { return binaryOp(BinOp::Div, a, b); }
inline Value vpow(const Value& a, const Value& b) { return binaryOp(BinOp::Pow, a, b); }
