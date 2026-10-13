#include "vm/arith.h"

#include <cmath>

#include "cas/bridge.h"
#include "common/error.h"
#include "ga/ga.h"
#include "hyper/hyper.h"
#include "linalg/matrix.h"
#include "numeric/ops.h"
#include "numeric/radical.h"
#include "sets/set.h"
#include "units/units.h"

const char* binOpSymbol(BinOp op) {
    switch (op) {
        case BinOp::Add: return "+";
        case BinOp::Sub: return "-";
        case BinOp::Mul: return "*";
        case BinOp::Div: return "/";
        case BinOp::Mod: return "%";
        case BinOp::Pow: return "^";
        case BinOp::LDiv: return "\\";
        case BinOp::Dot: return "·";
        case BinOp::Cross: return "×";
    }
    return "?";
}

namespace {

Value listBinary(BinOp op, const Value& a, const Value& b) {
    bool la = isList(a), lb = isList(b);
    if (la && lb) {
        if (op == BinOp::Dot) return dotProduct(a, b);
        if (op == BinOp::Cross) return crossProduct(a, b);
        const auto& x = as<ObjList>(a)->items;
        const auto& y = as<ObjList>(b)->items;
        if (op == BinOp::LDiv) fail("'\\' is not defined for two lists");
        if (x.size() != y.size())
            fail("list lengths differ (" + std::to_string(x.size()) + " and " + std::to_string(y.size()) + ")");
        std::vector<Value> r(x.size());
        for (size_t i = 0; i < x.size(); i++) r[i] = binaryOp(op, x[i], y[i]);
        return newList(std::move(r));
    }
    if (op == BinOp::Dot || op == BinOp::Cross) op = BinOp::Mul;
    if (la) {
        const auto& x = as<ObjList>(a)->items;
        std::vector<Value> r(x.size());
        for (size_t i = 0; i < x.size(); i++) r[i] = binaryOp(op, x[i], b);
        return newList(std::move(r));
    }
    const auto& y = as<ObjList>(b)->items;
    std::vector<Value> r(y.size());
    for (size_t i = 0; i < y.size(); i++) r[i] = binaryOp(op, a, y[i]);
    return newList(std::move(r));
}

[[noreturn]] void unsupported(BinOp op, const Value& a, const Value& b) {
    fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": " + typeName(a) + " and " + typeName(b));
}

bool isNaNValue(const Value& v) { return v.isReal() && std::isnan(v.as.d); }

bool numericValue(const Value& v, double& out) {
    if (isRealNumber(v)) {
        out = toDouble(v);
        return true;
    }
    if (isSym(v)) return symToDouble(v, out);
    return false;
}

}  // namespace

Value binaryOp(BinOp op, const Value& a, const Value& b) {
    if (isRadical(a) || isRadical(b)) {
        if (isSym(a) || isSym(b)) {
            Value x = isRadical(a) ? radicalAsSym(a) : a;
            Value y = isRadical(b) ? radicalAsSym(b) : b;
            return symBinary(op, x, y);
        }
        return radicalBinary(op, a, b);
    }
    if (isNumber(a) && isNumber(b)) {
        switch (op) {
            case BinOp::Add: return numAdd(a, b);
            case BinOp::Sub: return numSub(a, b);
            case BinOp::Mul:
            case BinOp::Dot:
            case BinOp::Cross: return numMul(a, b);
            case BinOp::Div: return numDiv(a, b);
            case BinOp::Mod: return numMod(a, b);
            case BinOp::Pow: return numPow(a, b);
            case BinOp::LDiv: return numDiv(b, a);
        }
    }
    if (isRange(a)) return binaryOp(op, rangeToList(a), b);
    if (isRange(b)) return binaryOp(op, a, rangeToList(b));
    if (isMatrix(a) || isMatrix(b)) return matrixBinary(op, a, b);
    if (isList(a) || isList(b)) return listBinary(op, a, b);
    if (isSet(a) || isSet(b)) {
        if (isSet(a) && isSet(b)) {
            if (op == BinOp::Cross) return cartesianProduct(a, b);
            if (op == BinOp::LDiv || op == BinOp::Sub) return setDifference(a, b);
        }
        unsupported(op, a, b);
    }
    if (isQuantity(a) || isQuantity(b)) return quantityBinary(op, a, b);
    if (isMulti(a) || isMulti(b)) return multiBinary(op, a, b);
    if (isHyper(a) || isHyper(b)) return hyperBinary(op, a, b);
    if (isSym(a) || isSym(b)) return symBinary(op, a, b);
    if (op == BinOp::Add && (isString(a) || isString(b))) return newString(toString(a) + toString(b));
    if (op == BinOp::Mul && isString(a) && b.isInt() && b.as.i >= 0) {
        std::string r;
        for (int64_t i = 0; i < b.as.i; i++) r += as<ObjString>(a)->s;
        return newString(r);
    }
    unsupported(op, a, b);
}

Value negateValue(const Value& a) {
    if (isRadical(a)) return radicalNeg(a);
    if (isNumber(a)) return numNeg(a);
    if (isList(a) || isMatrix(a) || isRange(a)) return mapElements(a, negateValue);
    if (isQuantity(a)) return quantityNeg(a);
    if (isMulti(a)) return multiNeg(a);
    if (isHyper(a)) return hyperNeg(a);
    if (isSym(a)) return symBinary(BinOp::Mul, Value::integer(-1), a);
    fail("cannot negate a value of type " + typeName(a));
}

Value compareOp(CmpOp op, const Value& a, const Value& b) {
    if (op == CmpOp::Eq || op == CmpOp::Ne) {
        bool symbolic = (isSym(a) && (isSym(b) || isNumber(b))) || (isSym(b) && isNumber(a));
        if (symbolic) {
            Value eqn = symEquation(a, b);
            if (op == CmpOp::Eq) return eqn;
            return Value::boolean(!symTruth(eqn));
        }
        bool e = (isQuantity(a) || isQuantity(b)) ? quantityEqual(a, b) : valuesEqual(a, b);
        return Value::boolean(op == CmpOp::Eq ? e : !e);
    }
    if (isNaNValue(a) || isNaNValue(b)) return Value::boolean(false);
    int c;
    double x, y;
    if (isRadical(a) || isRadical(b)) {
        bool otherA = isRadical(a) || isRealNumber(a);
        bool otherB = isRadical(b) || isRealNumber(b);
        if (!otherA || !otherB) fail("cannot compare " + typeName(a) + " and " + typeName(b));
        c = radicalCompare(a, b);
    } else if (isRealNumber(a) && isRealNumber(b)) {
        c = numCompare(a, b);
    } else if (isQuantity(a) || isQuantity(b)) {
        c = quantityCompare(a, b);
    } else if (isString(a) && isString(b)) {
        int s = as<ObjString>(a)->s.compare(as<ObjString>(b)->s);
        c = s < 0 ? -1 : (s > 0 ? 1 : 0);
    } else if ((isSym(a) || isSym(b)) && numericValue(a, x) && numericValue(b, y)) {
        c = x < y ? -1 : (x > y ? 1 : 0);
    } else if (isSym(a) || isSym(b)) {
        fail("cannot order symbolic expressions that are not numeric constants");
    } else {
        fail("cannot compare " + typeName(a) + " and " + typeName(b));
    }
    switch (op) {
        case CmpOp::Lt: return Value::boolean(c < 0);
        case CmpOp::Le: return Value::boolean(c <= 0);
        case CmpOp::Gt: return Value::boolean(c > 0);
        default: return Value::boolean(c >= 0);
    }
}

bool isZeroValue(const Value& v) {
    if (isNumber(v)) return numIsZero(v);
    if (isSym(v)) {
        Value s = simplifyValue(v);
        return isNumber(s) && numIsZero(s);
    }
    if (isQuantity(v)) return isZeroValue(as<ObjQuantity>(v)->mag);
    return false;
}

Value mapElements(const Value& v, const std::function<Value(const Value&)>& f) {
    if (isList(v)) {
        const auto& items = as<ObjList>(v)->items;
        std::vector<Value> r(items.size());
        for (size_t i = 0; i < items.size(); i++) r[i] = mapElements(items[i], f);
        return newList(std::move(r));
    }
    if (isMatrix(v)) {
        const ObjMatrix* m = as<ObjMatrix>(v);
        std::vector<Value> r(m->data.size());
        for (size_t i = 0; i < m->data.size(); i++) r[i] = f(m->data[i]);
        return newMatrix(m->rows, m->cols, std::move(r));
    }
    if (isRange(v)) return mapElements(rangeToList(v), f);
    return f(v);
}
