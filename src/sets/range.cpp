#include <cmath>

#include "cas/bridge.h"
#include "common/error.h"
#include "common/utf8.h"
#include "numeric/ops.h"
#include "sets/set.h"

namespace {

// Real numbers, plus symbolic constants such as π/4 that evaluate to one.
bool realBound(const Value& v, double& out) {
    if (isRealNumber(v)) {
        out = toDouble(v);
        return true;
    }
    return isSym(v) && symToDouble(v, out);
}

double boundValue(const Value& v) {
    double d = 0;
    realBound(v, d);
    return d;
}

}  // namespace

Value makeRange(const Value& start, const Value& stop, const Value& step) {
    double a, b, s;
    if (!realBound(start, a) || !realBound(stop, b) || !realBound(step, s))
        fail("range bounds must be real numbers, got " + typeName(start) + ".." + typeName(stop));
    if (s == 0) fail("range step cannot be zero");
    return newRange(start, stop, step);
}

int64_t rangeCount(const ObjRange* r) {
    if (r->start.isInt() && r->stop.isInt() && r->step.isInt()) {
        int64_t a = r->start.as.i, b = r->stop.as.i, s = r->step.as.i;
        if (s > 0) return b < a ? 0 : (b - a) / s + 1;
        return a < b ? 0 : (a - b) / (-s) + 1;
    }
    if (isExactNumber(r->start) && isExactNumber(r->stop) && isExactNumber(r->step)) {
        Rational n = (toRational(r->stop) - toRational(r->start)) / toRational(r->step);
        if (n.sign() < 0) return 0;
        BigInt f = n.floor();
        return f.fitsInt64() ? f.toInt64() + 1 : INT64_MAX;
    }
    double n = (boundValue(r->stop) - boundValue(r->start)) / boundValue(r->step);
    if (n < 0 || std::isnan(n)) return 0;
    return static_cast<int64_t>(std::floor(n + 1e-9)) + 1;
}

Value rangeAt(const ObjRange* r, int64_t k) {
    if (r->start.isInt() && r->step.isInt()) return Value::integer(r->start.as.i + k * r->step.as.i);
    if (isSym(r->start) || isSym(r->step)) return vadd(r->start, vmul(Value::integer(k), r->step));
    return numAdd(r->start, numMul(Value::integer(k), r->step));
}

Value rangeToList(const Value& v) {
    const ObjRange* r = as<ObjRange>(v);
    int64_t n = rangeCount(r);
    if (n > 100000000) fail("range is too large to convert to a list");
    std::vector<Value> items;
    items.reserve(static_cast<size_t>(n));
    for (int64_t k = 0; k < n; k++) items.push_back(rangeAt(r, k));
    return newList(std::move(items));
}

bool isIterable(const Value& v) {
    return isList(v) || isSet(v) || isRange(v) || isString(v) || isMatrix(v) || isRecord(v);
}

std::vector<Value> iterableToVector(const Value& v) {
    if (isList(v)) return as<ObjList>(v)->items;
    if (isSet(v)) return as<ObjSet>(v)->items;
    if (isRange(v)) return as<ObjList>(rangeToList(v))->items;
    std::vector<Value> out;
    Value element;
    int64_t k = 0;
    if (!isIterable(v)) fail("a value of type " + typeName(v) + " is not iterable");
    while (iterNext(v, k, element)) out.push_back(element);
    return out;
}

bool iterNext(const Value& it, int64_t& k, Value& out) {
    if (isRange(it)) {
        const ObjRange* r = as<ObjRange>(it);
        if (r->start.isInt() && r->stop.isInt() && r->step.isInt()) {
            int64_t s = r->step.as.i;
            int64_t v = r->start.as.i + k * s;
            if (s > 0 ? v > r->stop.as.i : v < r->stop.as.i) return false;
            out = Value::integer(v);
            k++;
            return true;
        }
        if (k >= rangeCount(r)) return false;
        out = rangeAt(r, k++);
        return true;
    }
    if (isList(it)) {
        const auto& items = as<ObjList>(it)->items;
        if (k >= static_cast<int64_t>(items.size())) return false;
        out = items[static_cast<size_t>(k++)];
        return true;
    }
    if (isSet(it)) {
        const auto& items = as<ObjSet>(it)->items;
        if (k >= static_cast<int64_t>(items.size())) return false;
        out = items[static_cast<size_t>(k++)];
        return true;
    }
    if (isMatrix(it)) {
        const ObjMatrix* m = as<ObjMatrix>(it);
        if (k >= static_cast<int64_t>(m->rows)) return false;
        size_t r = static_cast<size_t>(k++);
        out = newList(std::vector<Value>(m->data.begin() + static_cast<long>(r * m->cols),
                                         m->data.begin() + static_cast<long>((r + 1) * m->cols)));
        return true;
    }
    if (isRecord(it)) {
        const auto& keys = as<ObjRecord>(it)->keys;
        if (k >= static_cast<int64_t>(keys.size())) return false;
        out = keys[static_cast<size_t>(k++)];
        return true;
    }
    if (isString(it)) {
        const std::string& s = as<ObjString>(it)->s;
        if (k >= static_cast<int64_t>(s.size())) return false;
        size_t i = static_cast<size_t>(k);
        utf8::decode(s, i);
        out = newString(s.substr(static_cast<size_t>(k), i - static_cast<size_t>(k)));
        k = static_cast<int64_t>(i);
        return true;
    }
    fail("a value of type " + typeName(it) + " is not iterable");
}
