#include "ga/ga.h"

#include <cmath>

#include "cas/bridge.h"
#include "common/error.h"
#include "numeric/ops.h"
#include "vm/vm.h"

namespace {

int popcount(int m) {
    int n = 0;
    while (m) {
        n += m & 1;
        m >>= 1;
    }
    return n;
}

int dimOf(int p, int q) { return 1 << (p + q); }

void checkSig(int p, int q) {
    if (p < 0 || q < 0 || p + q < 1 || p + q > 6)
        fail("geometric algebra: the signature Cl(p, q) needs 1 to 6 generators");
}

// e_a * e_b = sign * e_{a xor b}. sig[i] is e_{i+1}^2.
void basisMul(int a, int b, const int* sig, int n, int& out, int& sign) {
    out = a ^ b;
    sign = 1;
    for (int i = 0; i < n; i++) {
        if ((a & (1 << i)) == 0) continue;
        for (int j = 0; j < i; j++)
            if (b & (1 << j)) sign = -sign;
        if (b & (1 << i)) sign *= sig[i];
    }
}

Value rawMulti(int p, int q, std::vector<Value> c) {
    return Value::object(Heap::instance().make<ObjMulti>(p, q, std::move(c)));
}

bool allOtherZero(const std::vector<Value>& c) {
    for (size_t i = 1; i < c.size(); i++)
        if (!isZeroValue(c[i])) return false;
    return true;
}

Value finishMulti(int p, int q, std::vector<Value> c) {
    if (allOtherZero(c)) return c.empty() ? Value::integer(0) : c[0];
    return rawMulti(p, q, std::move(c));
}

std::vector<int> signature(int p, int q) {
    std::vector<int> s(static_cast<size_t>(p + q), 1);
    for (int i = 0; i < q; i++) s[static_cast<size_t>(p + i)] = -1;
    return s;
}

Value embed(const Value& v, int p, int q) {
    if (isMulti(v)) {
        const ObjMulti* m = as<ObjMulti>(v);
        if (m->p != p || m->q != q)
            fail("geometric algebra: Cl(" + std::to_string(m->p) + "," + std::to_string(m->q) + ") and Cl(" +
                 std::to_string(p) + "," + std::to_string(q) + ") cannot be mixed");
        return v;
    }
    if (!(isNumber(v) || isSym(v)))
        fail("geometric algebra: cannot combine a multivector with a value of type " + typeName(v));
    std::vector<Value> c(static_cast<size_t>(dimOf(p, q)), Value::integer(0));
    c[0] = v;
    return rawMulti(p, q, std::move(c));
}

enum class Prod { Geo, Wedge, Inner };

Value product(const Value& a, const Value& b, Prod kind) {
    int p, q;
    if (isMulti(a)) {
        p = as<ObjMulti>(a)->p;
        q = as<ObjMulti>(a)->q;
    } else {
        p = as<ObjMulti>(b)->p;
        q = as<ObjMulti>(b)->q;
    }
    Value x = embed(a, p, q), y = embed(b, p, q);
    const ObjMulti *mx = as<ObjMulti>(x), *my = as<ObjMulti>(y);
    int n = p + q;
    std::vector<int> sig = signature(p, q);
    std::vector<Value> acc(mx->c.size(), Value::integer(0));
    for (size_t i = 0; i < mx->c.size(); i++) {
        if (isZeroValue(mx->c[i])) continue;
        for (size_t j = 0; j < my->c.size(); j++) {
            if (isZeroValue(my->c[j])) continue;
            int ia = static_cast<int>(i), ib = static_cast<int>(j);
            if (kind == Prod::Wedge && (ia & ib)) continue;
            int out = 0, sign = 1;
            basisMul(ia, ib, sig.data(), n, out, sign);
            if (kind == Prod::Inner && popcount(out) != std::abs(popcount(ia) - popcount(ib))) continue;
            Value term = binaryOp(BinOp::Mul, mx->c[i], my->c[j]);
            if (sign < 0) term = negateValue(term);
            acc[static_cast<size_t>(out)] = binaryOp(BinOp::Add, acc[static_cast<size_t>(out)], term);
        }
    }
    return finishMulti(p, q, std::move(acc));
}

std::string coeffText(const Value& c, bool& negative) {
    negative = false;
    if (isNumber(c) && numEqual(c, Value::integer(-1))) {
        negative = true;
        return "";
    }
    if (isNumber(c) && numEqual(c, Value::integer(1))) return "";
    std::string s = toString(c);
    if (!s.empty() && s[0] == '-') {
        negative = true;
        s.erase(s.begin());
    }
    return s;
}

}  // namespace

std::string multiToString(const ObjMulti* m) {
    int n = m->p + m->q;
    std::string s;
    for (size_t mask = 0; mask < m->c.size(); mask++) {
        if (isZeroValue(m->c[mask])) continue;
        std::string blade;
        if (mask != 0) {
            blade = "e";
            for (int i = 0; i < n; i++)
                if (mask & (1u << i)) blade += std::to_string(i + 1);
        }
        bool neg = false;
        std::string coeff = coeffText(m->c[mask], neg);
        if (!s.empty()) s += neg ? " - " : " + ";
        else if (neg) s += "-";
        if (blade.empty())
            s += coeff.empty() ? "1" : coeff;
        else if (coeff.empty())
            s += blade;
        else if (coeff.find_first_of("+-*/^ ") == std::string::npos)
            s += coeff + blade;
        else
            s += "(" + coeff + ")" + blade;
    }
    return s.empty() ? "0" : s;
}

bool multiEqual(const Value& a, const Value& b) {
    const ObjMulti *x = as<ObjMulti>(a), *y = as<ObjMulti>(b);
    if (x->p != y->p || x->q != y->q || x->c.size() != y->c.size()) return false;
    for (size_t i = 0; i < x->c.size(); i++)
        if (!valuesEqual(x->c[i], y->c[i]) && !isZeroValue(binaryOp(BinOp::Sub, x->c[i], y->c[i]))) return false;
    return true;
}

Value multiNeg(const Value& a) {
    const ObjMulti* m = as<ObjMulti>(a);
    std::vector<Value> c = m->c;
    for (auto& v : c) v = negateValue(v);
    return finishMulti(m->p, m->q, std::move(c));
}

Value multiReverse(const Value& a) {
    if (!isMulti(a)) return a;
    const ObjMulti* m = as<ObjMulti>(a);
    std::vector<Value> c = m->c;
    for (size_t mask = 0; mask < c.size(); mask++) {
        int k = popcount(static_cast<int>(mask));
        if ((k * (k - 1) / 2) % 2) c[mask] = negateValue(c[mask]);
    }
    return finishMulti(m->p, m->q, std::move(c));
}

Value multiGrade(const Value& a, int k) {
    if (!isMulti(a)) return k == 0 ? a : Value::integer(0);
    const ObjMulti* m = as<ObjMulti>(a);
    std::vector<Value> c(m->c.size(), Value::integer(0));
    for (size_t mask = 0; mask < m->c.size(); mask++)
        if (popcount(static_cast<int>(mask)) == k) c[mask] = m->c[mask];
    return finishMulti(m->p, m->q, std::move(c));
}

Value multiWedge(const Value& a, const Value& b) { return product(a, b, Prod::Wedge); }
Value multiInner(const Value& a, const Value& b) { return product(a, b, Prod::Inner); }

Value multiBinary(BinOp op, const Value& a, const Value& b) {
    switch (op) {
        case BinOp::Add:
        case BinOp::Sub: {
            Value y = op == BinOp::Sub ? (isMulti(b) ? multiNeg(b) : negateValue(b)) : b;
            int p, q;
            if (isMulti(a)) {
                p = as<ObjMulti>(a)->p;
                q = as<ObjMulti>(a)->q;
            } else {
                p = as<ObjMulti>(y)->p;
                q = as<ObjMulti>(y)->q;
            }
            Value x = embed(a, p, q), z = embed(y, p, q);
            const ObjMulti *mx = as<ObjMulti>(x), *mz = as<ObjMulti>(z);
            std::vector<Value> c(mx->c.size());
            for (size_t i = 0; i < c.size(); i++) c[i] = binaryOp(BinOp::Add, mx->c[i], mz->c[i]);
            return finishMulti(p, q, std::move(c));
        }
        case BinOp::Mul:
        case BinOp::Dot:
            return product(a, b, op == BinOp::Dot ? Prod::Inner : Prod::Geo);
        case BinOp::Div:
            if (isMulti(b)) fail("multivectors can be divided only by a scalar");
            return product(a, binaryOp(BinOp::Div, Value::integer(1), b), Prod::Geo);
        case BinOp::Pow: {
            if (!isMulti(a) || !b.isInt() || b.as.i < 0 || b.as.i > 16)
                fail("multivector powers must be integers from 0 to 16");
            Value r = Value::integer(1);
            for (int64_t i = 0; i < b.as.i; i++) r = product(r, a, Prod::Geo);
            return r;
        }
        default: fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": multivectors");
    }
}

namespace {

int intArg(const Value& v, const char* what) {
    if (!isIntegerValue(v)) fail(std::string(what) + " must be an integer");
    return static_cast<int>(toInt64(v, what));
}

Value n_blade(VM&, int argc, Value* args) {
    int p = intArg(args[0], "blade: p");
    int q = intArg(args[1], "blade: q");
    checkSig(p, q);
    int mask = 0;
    for (int i = 2; i < argc; i++) {
        int k = intArg(args[i], "blade: an index");
        if (k < 1 || k > p + q) fail("blade: index " + std::to_string(k) + " is outside 1.." + std::to_string(p + q));
        int bit = 1 << (k - 1);
        if (mask & bit) fail("blade: a repeated index squares the blade away; write it once");
        mask |= bit;
    }
    std::vector<Value> c(static_cast<size_t>(dimOf(p, q)), Value::integer(0));
    // The blade e_{i}∧e_{j}∧... in index order, which is the basis element of that mask.
    c[static_cast<size_t>(mask)] = Value::integer(1);
    return finishMulti(p, q, std::move(c));
}

Value n_mv(VM&, int argc, Value* args) {
    if (!isList(args[0])) fail("mv: expected a list of components");
    const auto& items = as<ObjList>(args[0])->items;
    if (items.empty() || (items.size() & (items.size() - 1)) != 0 || items.size() > 64)
        fail("mv: the number of components must be a power of two, at most 64");
    int n = 0;
    while ((1 << n) < static_cast<int>(items.size())) n++;
    int p = n, q = 0;
    if (argc == 3) {
        p = intArg(args[1], "mv: p");
        q = intArg(args[2], "mv: q");
        if (p + q != n) fail("mv: Cl(p, q) has 2^(p+q) components");
    }
    checkSig(p, q);
    return finishMulti(p, q, items);
}

Value n_wedge(VM&, int, Value* args) {
    if (!isMulti(args[0]) && !isMulti(args[1])) fail("wedge: expected a multivector");
    return multiWedge(args[0], args[1]);
}
Value n_inner(VM&, int, Value* args) {
    if (!isMulti(args[0]) && !isMulti(args[1])) fail("inner: expected a multivector");
    return multiInner(args[0], args[1]);
}

Value n_reverse(VM&, int, Value* args) {
    if (!isMulti(args[0])) fail("reverse: expected a multivector");
    return multiReverse(args[0]);
}

Value n_grade(VM&, int, Value* args) {
    if (!isMulti(args[0])) fail("grade: expected a multivector");
    return multiGrade(args[0], intArg(args[1], "grade"));
}

Value n_rotor(VM& vm, int, Value* args) {
    if (!isMulti(args[0])) fail("rotor: the plane must be a bivector");
    Value B = multiGrade(args[0], 2);
    if (!isMulti(B)) fail("rotor: the plane must be a bivector");
    Value half = binaryOp(BinOp::Div, args[1], Value::integer(2));
    Value c = vm.call(vm.getGlobal("cos"), {half});
    Value s = vm.call(vm.getGlobal("sin"), {half});
    if (isSym(c)) c = simplifyValue(c);
    if (isSym(s)) s = simplifyValue(s);
    return multiBinary(BinOp::Sub, c, multiBinary(BinOp::Mul, s, B));
}

Value n_sandwich(VM&, int, Value* args) {
    if (!isMulti(args[0])) fail("sandwich: the rotor must be a multivector");
    return multiBinary(BinOp::Mul, multiBinary(BinOp::Mul, args[0], args[1]), multiReverse(args[0]));
}

}  // namespace

void registerGa(VM& vm) {
    const char* c = "geometric";
    vm.defineNative("blade", 2, -1, n_blade, "blade(p, q, i, ...) - basis blade e_i∧e_j∧... of Cl(p, q)", c);
    vm.defineNative("mv", 1, 3, n_mv, "mv([components], p, q) - multivector; components are in binary order", c);
    vm.defineNative("wedge", 2, 2, n_wedge, "wedge(a, b) - exterior product a∧b", c);
    vm.defineNative("inner", 2, 2, n_inner, "inner(a, b) - inner product <ab> of grade |r-s|", c);
    vm.defineNative("reverse", 1, 1, n_reverse, "reverse(a) - reverse of a multivector", c);
    vm.defineNative("grade", 2, 2, n_grade, "grade(a, k) - the grade-k part of a multivector", c);
    vm.defineNative("rotor", 2, 2, n_rotor, "rotor(bivector, θ) - cos(θ/2) - sin(θ/2)·B", c);
    vm.defineNative("sandwich", 2, 2, n_sandwich, "sandwich(R, v) - R v reverse(R)", c);
}
