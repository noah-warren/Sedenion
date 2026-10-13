#include "numeric/radical.h"
#include "vm/object.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "cas/bridge.h"
#include "cas/expr.h"
#include "common/error.h"
#include "common/utf8.h"
#include "ga/ga.h"
#include "hyper/hyper.h"
#include "numeric/ops.h"
#include "sets/set.h"
#include "units/units.h"

Value newString(std::string s) { return Value::object(Heap::instance().make<ObjString>(std::move(s))); }

Value newList(std::vector<Value> items) { return Value::object(Heap::instance().make<ObjList>(std::move(items))); }

Value newMatrix(size_t rows, size_t cols, std::vector<Value> data) {
    return Value::object(Heap::instance().make<ObjMatrix>(rows, cols, std::move(data)));
}

Value newSet(std::vector<Value> items) {
    sortUnique(items);
    return Value::object(Heap::instance().make<ObjSet>(std::move(items)));
}

Value newRange(Value start, Value stop, Value step) {
    return Value::object(Heap::instance().make<ObjRange>(start, stop, step));
}

Value newSym(cas::E e) { return Value::object(Heap::instance().make<ObjSym>(std::move(e))); }

Value newRecord() { return Value::object(Heap::instance().make<ObjRecord>()); }

int recordFind(const Value& rec, const Value& key) {
    const ObjRecord* r = as<ObjRecord>(rec);
    for (size_t i = 0; i < r->keys.size(); i++)
        if (valuesEqual(r->keys[i], key)) return static_cast<int>(i);
    return -1;
}

bool recordHas(const Value& rec, const Value& key) { return recordFind(rec, key) >= 0; }

Value recordGet(const Value& rec, const Value& key) {
    int i = recordFind(rec, key);
    if (i < 0) fail("record has no field " + repr(key));
    return as<ObjRecord>(rec)->vals[static_cast<size_t>(i)];
}

void recordSet(const Value& rec, const Value& key, const Value& val) {
    ObjRecord* r = as<ObjRecord>(rec);
    int i = recordFind(rec, key);
    if (i >= 0) {
        r->vals[static_cast<size_t>(i)] = val;
        return;
    }
    r->keys.push_back(key);
    r->vals.push_back(val);
}

std::string typeName(const Value& v) {
    switch (v.type) {
        case ValType::Nil: return "nil";
        case ValType::Undef: return "undefined";
        case ValType::Bool: return "bool";
        case ValType::Int: return "integer";
        case ValType::Real: return "real";
        case ValType::Obj: break;
    }
    switch (v.as.o->type) {
        case ObjType::String: return "string";
        case ObjType::BigInt: return "integer";
        case ObjType::Rational: return "rational";
        case ObjType::Complex: return "complex";
        case ObjType::List: return "list";
        case ObjType::Matrix: return "matrix";
        case ObjType::Set: return "set";
        case ObjType::Range: return "range";
        case ObjType::Function:
        case ObjType::Closure: return "function";
        case ObjType::Native: return "builtin";
        case ObjType::Upvalue: return "upvalue";
        case ObjType::Sym: return "symbolic";
        case ObjType::Quantity: return "quantity";
        case ObjType::Hyper: return hyperTypeName(static_cast<ObjHyper*>(v.as.o));
        case ObjType::Record: return "record";
        case ObjType::Multi: return "multivector";
        case ObjType::Radical: return "radical";
    }
    return "unknown";
}

namespace {

std::string quoteString(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break;
            default: r += c;
        }
    }
    return r + "\"";
}

std::string toStringImpl(const Value& v, bool quote) {
    switch (v.type) {
        case ValType::Nil: return "nil";
        case ValType::Undef: return "<undefined>";
        case ValType::Bool: return v.as.b ? "true" : "false";
        case ValType::Int: return std::to_string(v.as.i);
        case ValType::Real: return formatDouble(v.as.d, true);
        case ValType::Obj: break;
    }
    Obj* o = v.as.o;
    switch (o->type) {
        case ObjType::String: return quote ? quoteString(as<ObjString>(v)->s) : as<ObjString>(v)->s;
        case ObjType::BigInt: return as<ObjBigInt>(v)->v.toString();
        case ObjType::Rational: return as<ObjRational>(v)->v.toString();
        case ObjType::Complex: return formatComplex(as<ObjComplex>(v)->v);
        case ObjType::List: {
            std::string s = "[";
            const auto& items = as<ObjList>(v)->items;
            for (size_t i = 0; i < items.size(); i++) {
                if (i) s += ", ";
                s += toStringImpl(items[i], true);
            }
            return s + "]";
        }
        case ObjType::Matrix: {
            const ObjMatrix* m = as<ObjMatrix>(v);
            std::string s = "[";
            for (size_t i = 0; i < m->rows; i++) {
                if (i) s += "; ";
                for (size_t j = 0; j < m->cols; j++) {
                    if (j) s += ", ";
                    s += toStringImpl(m->at(i, j), true);
                }
            }
            return s + "]";
        }
        case ObjType::Set: {
            const auto& items = as<ObjSet>(v)->items;
            if (items.empty()) return "{}";
            std::string s = "{";
            for (size_t i = 0; i < items.size(); i++) {
                if (i) s += ", ";
                s += toStringImpl(items[i], true);
            }
            return s + "}";
        }
        case ObjType::Range: {
            const ObjRange* r = as<ObjRange>(v);
            if (r->step.isInt() && r->step.as.i == 1) return toStringImpl(r->start, true) + ".." + toStringImpl(r->stop, true);
            return toStringImpl(r->start, true) + ".." + toStringImpl(r->stop, true) + " step " +
                   toStringImpl(r->step, true);
        }
        case ObjType::Function: return "<fn " + static_cast<ObjFunction*>(o)->name + ">";
        case ObjType::Closure: return "<fn " + as<ObjClosure>(v)->fn->name + ">";
        case ObjType::Native: return "<builtin " + as<ObjNative>(v)->name + ">";
        case ObjType::Upvalue: return "<upvalue>";
        case ObjType::Sym: return cas::toString(as<ObjSym>(v)->e);
        case ObjType::Quantity: return formatQuantity(as<ObjQuantity>(v));
        case ObjType::Hyper: return hyperToString(as<ObjHyper>(v));
        case ObjType::Record: {
            const ObjRecord* r = as<ObjRecord>(v);
            if (r->keys.empty()) return "{:}";
            auto bare = [](const std::string& s) {
                if (s.empty()) return false;
                unsigned char c0 = static_cast<unsigned char>(s[0]);
                if (!(std::isalpha(c0) || c0 == '_' || c0 >= 128)) return false;
                for (unsigned char c : s)
                    if (!(std::isalnum(c) || c == '_' || c >= 128)) return false;
                return true;
            };
            std::string s = "{";
            for (size_t i = 0; i < r->keys.size(); i++) {
                if (i) s += ", ";
                if (isString(r->keys[i]) && bare(as<ObjString>(r->keys[i])->s))
                    s += as<ObjString>(r->keys[i])->s;
                else
                    s += toStringImpl(r->keys[i], true);
                s += ": " + toStringImpl(r->vals[i], true);
            }
            return s + "}";
        }
        case ObjType::Multi: return multiToString(as<ObjMulti>(v));
        case ObjType::Radical: return radicalToString(as<ObjRadical>(v));
    }
    return "?";
}

// Symbolic constants with a real value (sqrt(2), π/3) are ordered among the numbers.
bool symReal(const Value& v, double& d) { return isObjType(v, ObjType::Sym) && symToDouble(v, d); }

int typeRank(const Value& v) {
    double d;
    if (symReal(v, d)) return 2;
    switch (v.type) {
        case ValType::Nil:
        case ValType::Undef: return 0;
        case ValType::Bool: return 1;
        case ValType::Int:
        case ValType::Real: return 2;
        case ValType::Obj: break;
    }
    switch (v.as.o->type) {
        case ObjType::BigInt:
        case ObjType::Rational:
        case ObjType::Radical: return 2;
        case ObjType::Complex: return 3;
        case ObjType::Sym: return 4;
        case ObjType::String: return 5;
        case ObjType::List: return 6;
        case ObjType::Set: return 7;
        case ObjType::Matrix: return 8;
        case ObjType::Range: return 9;
        case ObjType::Quantity: return 10;
        case ObjType::Hyper: return 11;
        case ObjType::Record: return 12;
        case ObjType::Multi: return 13;
        default: return 14;
    }
}

int compareSeq(const std::vector<Value>& a, const std::vector<Value>& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        int c = compareValues(a[i], b[i]);
        if (c) return c;
    }
    return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
}

}  // namespace

std::string toString(const Value& v) { return toStringImpl(v, false); }
std::string repr(const Value& v) { return toStringImpl(v, true); }

std::string display(const Value& v) {
    if (!isMatrix(v)) return toString(v);
    const ObjMatrix* m = as<ObjMatrix>(v);
    if (m->rows == 0 || m->cols == 0) return "[]";
    std::vector<std::string> cells(m->data.size());
    std::vector<size_t> width(m->cols, 0);
    for (size_t i = 0; i < m->rows; i++) {
        for (size_t j = 0; j < m->cols; j++) {
            std::string s = repr(m->at(i, j));
            width[j] = std::max(width[j], utf8::length(s));
            cells[i * m->cols + j] = std::move(s);
        }
    }
    std::string out;
    for (size_t i = 0; i < m->rows; i++) {
        out += i == 0 ? "[" : " ";
        for (size_t j = 0; j < m->cols; j++) {
            const std::string& s = cells[i * m->cols + j];
            if (j) out += "  ";
            out += std::string(width[j] - utf8::length(s), ' ') + s;
        }
        out += i + 1 == m->rows ? "]" : "\n";
    }
    return out;
}

bool valuesEqual(const Value& a, const Value& b) {
    if (isNumber(a) && isNumber(b)) return numEqual(a, b);
    if (a.type != b.type) return false;
    switch (a.type) {
        case ValType::Nil:
        case ValType::Undef: return true;
        case ValType::Bool: return a.as.b == b.as.b;
        case ValType::Int:
        case ValType::Real: return false;
        case ValType::Obj: break;
    }
    if (a.as.o == b.as.o) return true;
    if (a.as.o->type != b.as.o->type) return false;
    switch (a.as.o->type) {
        case ObjType::String: return as<ObjString>(a)->s == as<ObjString>(b)->s;
        case ObjType::List: {
            const auto &x = as<ObjList>(a)->items, &y = as<ObjList>(b)->items;
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); i++)
                if (!valuesEqual(x[i], y[i])) return false;
            return true;
        }
        case ObjType::Set: {
            const auto &x = as<ObjSet>(a)->items, &y = as<ObjSet>(b)->items;
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); i++)
                if (!valuesEqual(x[i], y[i])) return false;
            return true;
        }
        case ObjType::Matrix: {
            const ObjMatrix *x = as<ObjMatrix>(a), *y = as<ObjMatrix>(b);
            if (x->rows != y->rows || x->cols != y->cols) return false;
            for (size_t i = 0; i < x->data.size(); i++)
                if (!valuesEqual(x->data[i], y->data[i])) return false;
            return true;
        }
        case ObjType::Range: {
            const ObjRange *x = as<ObjRange>(a), *y = as<ObjRange>(b);
            return valuesEqual(x->start, y->start) && valuesEqual(x->stop, y->stop) && valuesEqual(x->step, y->step);
        }
        case ObjType::Sym: return as<ObjSym>(a)->e == as<ObjSym>(b)->e;
        case ObjType::Quantity: return quantityEqual(a, b);
        case ObjType::Hyper: return hyperEqual(a, b);
        case ObjType::Record: {
            const ObjRecord *x = as<ObjRecord>(a), *y = as<ObjRecord>(b);
            if (x->keys.size() != y->keys.size()) return false;
            for (size_t i = 0; i < x->keys.size(); i++) {
                int j = recordFind(b, x->keys[i]);
                if (j < 0 || !valuesEqual(x->vals[i], y->vals[static_cast<size_t>(j)])) return false;
            }
            return true;
        }
        case ObjType::Multi: return multiEqual(a, b);
        case ObjType::Radical: return radicalEqual(a, b);
        default: return false;
    }
}

int compareValues(const Value& a, const Value& b) {
    int ra = typeRank(a), rb = typeRank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (ra) {
        case 0: return 0;
        case 1: return (a.as.b == b.as.b) ? 0 : (a.as.b ? 1 : -1);
        case 2: {
            if (isRadical(a) || isRadical(b)) return radicalCompare(a, b);
            double x, y;
            bool sa = symReal(a, x), sb = symReal(b, y);
            if (sa || sb) {
                if (!sa) x = toDouble(a);
                if (!sb) y = toDouble(b);
                if (x != y) return x < y ? -1 : 1;
                if (sa && sb) return cas::compare(as<ObjSym>(a)->e, as<ObjSym>(b)->e);
                return sa ? 1 : -1;
            }
            bool na = isRealNumber(a) && numKind(a) == NumKind::Real && std::isnan(a.as.d);
            bool nb = isRealNumber(b) && numKind(b) == NumKind::Real && std::isnan(b.as.d);
            if (na || nb) return na == nb ? 0 : (na ? 1 : -1);
            return numCompare(a, b);
        }
        case 3: {
            auto x = toComplex(a), y = toComplex(b);
            if (x.real() != y.real()) return x.real() < y.real() ? -1 : 1;
            if (x.imag() != y.imag()) return x.imag() < y.imag() ? -1 : 1;
            return 0;
        }
        case 4: return cas::compare(as<ObjSym>(a)->e, as<ObjSym>(b)->e);
        case 5: {
            int c = as<ObjString>(a)->s.compare(as<ObjString>(b)->s);
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        case 6: return compareSeq(as<ObjList>(a)->items, as<ObjList>(b)->items);
        case 7: return compareSeq(as<ObjSet>(a)->items, as<ObjSet>(b)->items);
        case 8: {
            const ObjMatrix *x = as<ObjMatrix>(a), *y = as<ObjMatrix>(b);
            if (x->rows != y->rows) return x->rows < y->rows ? -1 : 1;
            if (x->cols != y->cols) return x->cols < y->cols ? -1 : 1;
            return compareSeq(x->data, y->data);
        }
        case 9: {
            const ObjRange *x = as<ObjRange>(a), *y = as<ObjRange>(b);
            int c = compareValues(x->start, y->start);
            if (c) return c;
            c = compareValues(x->stop, y->stop);
            if (c) return c;
            return compareValues(x->step, y->step);
        }
        case 10: {
            const ObjQuantity *x = as<ObjQuantity>(a), *y = as<ObjQuantity>(b);
            int c = std::memcmp(x->dim.e, y->dim.e, sizeof x->dim.e);
            if (c) return c < 0 ? -1 : 1;
            return compareValues(x->mag, y->mag);
        }
        case 11: return compareSeq(as<ObjHyper>(a)->c, as<ObjHyper>(b)->c);
        case 12: {
            auto pairs = [](const ObjRecord* r) {
                std::vector<std::pair<Value, Value>> p;
                for (size_t i = 0; i < r->keys.size(); i++) p.push_back({r->keys[i], r->vals[i]});
                std::sort(p.begin(), p.end(), [](const auto& x, const auto& y) { return compareValues(x.first, y.first) < 0; });
                return p;
            };
            auto x = pairs(as<ObjRecord>(a)), y = pairs(as<ObjRecord>(b));
            size_t n = std::min(x.size(), y.size());
            for (size_t i = 0; i < n; i++) {
                int c = compareValues(x[i].first, y[i].first);
                if (c) return c;
                c = compareValues(x[i].second, y[i].second);
                if (c) return c;
            }
            return x.size() < y.size() ? -1 : (x.size() > y.size() ? 1 : 0);
        }
        case 13: {
            const ObjMulti *x = as<ObjMulti>(a), *y = as<ObjMulti>(b);
            if (x->p != y->p) return x->p < y->p ? -1 : 1;
            if (x->q != y->q) return x->q < y->q ? -1 : 1;
            return compareSeq(x->c, y->c);
        }
        default: return a.as.o < b.as.o ? -1 : (a.as.o > b.as.o ? 1 : 0);
    }
}

bool isTruthy(const Value& v) {
    switch (v.type) {
        case ValType::Nil:
        case ValType::Undef: return false;
        case ValType::Bool: return v.as.b;
        case ValType::Int: return v.as.i != 0;
        case ValType::Real: return v.as.d != 0.0;
        case ValType::Obj: break;
    }
    switch (v.as.o->type) {
        case ObjType::String: return !as<ObjString>(v)->s.empty();
        case ObjType::List: return !as<ObjList>(v)->items.empty();
        case ObjType::Set: return !as<ObjSet>(v)->items.empty();
        case ObjType::Record: return !as<ObjRecord>(v)->keys.empty();
        case ObjType::Complex: return !numIsZero(v);
        case ObjType::Sym: return symTruth(v);
        default: return true;
    }
}
