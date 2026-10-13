#include "hyper/hyper.h"

#include <algorithm>
#include <cmath>

#include "cas/bridge.h"
#include "common/error.h"
#include "numeric/ops.h"
#include "stdlib/stdlib.h"
#include "vm/vm.h"

namespace {

using Vec = std::vector<Value>;

bool isPowerOfTwo(size_t n) { return n >= 2 && (n & (n - 1)) == 0; }

size_t dimOf(const Value& v) {
    if (isHyper(v)) return as<ObjHyper>(v)->c.size();
    if (numKind(v) == NumKind::Cplx) return 2;
    return 1;
}

// Components of v in an algebra of dimension n (n >= dimOf(v)).
Vec componentsIn(const Value& v, size_t n) {
    Vec out;
    if (isHyper(v)) {
        out = as<ObjHyper>(v)->c;
    } else if (numKind(v) == NumKind::Cplx) {
        std::complex<double> z = toComplex(v);
        auto part = [](double d) {
            return d == std::floor(d) && std::fabs(d) < 1e15 ? Value::integer(static_cast<int64_t>(d)) : Value::real(d);
        };
        out = {part(z.real()), part(z.imag())};
    } else if (isNumber(v) || isSym(v)) {
        out = {v};
    } else {
        fail("cannot combine a hypercomplex number with a value of type " + typeName(v));
    }
    out.resize(n, Value::integer(0));
    return out;
}

Vec half(const Vec& v, bool upper) {
    size_t h = v.size() / 2;
    return upper ? Vec(v.begin() + static_cast<long>(h), v.end()) : Vec(v.begin(), v.begin() + static_cast<long>(h));
}

Vec join(const Vec& a, const Vec& b) {
    Vec r = a;
    r.insert(r.end(), b.begin(), b.end());
    return r;
}

Vec addV(const Vec& a, const Vec& b) {
    Vec r(a.size());
    for (size_t i = 0; i < a.size(); i++) r[i] = vadd(a[i], b[i]);
    return r;
}

Vec subV(const Vec& a, const Vec& b) {
    Vec r(a.size());
    for (size_t i = 0; i < a.size(); i++) r[i] = vsub(a[i], b[i]);
    return r;
}

Vec negV(const Vec& a) {
    Vec r(a.size());
    for (size_t i = 0; i < a.size(); i++) r[i] = negateValue(a[i]);
    return r;
}

// Cayley-Dickson conjugate: (a, b)* = (a*, -b).
Vec conjV(const Vec& x) {
    if (x.size() == 1) return x;
    return join(conjV(half(x, false)), negV(half(x, true)));
}

// Cayley-Dickson product: (a, b)(c, d) = (ac - d*b, da + bc*).
Vec mulV(const Vec& x, const Vec& y) {
    if (x.size() == 1) return {vmul(x[0], y[0])};
    Vec a = half(x, false), b = half(x, true), c = half(y, false), d = half(y, true);
    return join(subV(mulV(a, c), mulV(conjV(d), b)), addV(mulV(d, a), mulV(b, conjV(c))));
}

Value norm2V(const Vec& x) {
    Value s = Value::integer(0);
    for (const auto& c : x) s = vadd(s, vmul(c, c));
    return s;
}

Vec inverseV(const Vec& x) {
    Value n2 = norm2V(x);
    if (isZeroValue(n2)) fail("division by zero (hypercomplex number with norm 0)");
    Vec r = conjV(x);
    for (auto& c : r) c = vdiv(c, n2);
    return r;
}

Vec powV(Vec base, int64_t e) {
    if (e < 0) {
        base = inverseV(base);
        e = -e;
    }
    Vec r(base.size(), Value::integer(0));
    r[0] = Value::integer(1);
    while (e > 0) {
        if (e & 1) r = mulV(r, base);
        base = mulV(base, base);
        e >>= 1;
    }
    return r;
}

const char* const kQuatUnits[4] = {"", "i", "j", "k"};

std::string unitName(size_t dim, size_t k) {
    if (dim == 4) return kQuatUnits[k];
    return "e" + std::to_string(k);
}

}  // namespace

Value makeHyper(Vec components) {
    bool real = true;
    for (size_t i = 1; i < components.size() && real; i++)
        if (!isZeroValue(components[i])) real = false;
    if (real) return components.empty() ? Value::integer(0) : components[0];
    return Value::object(Heap::instance().make<ObjHyper>(std::move(components)));
}

Value hyperBinary(BinOp op, const Value& a, const Value& b) {
    size_t n = std::max(dimOf(a), dimOf(b));
    if (n < 4) n = 4;
    switch (op) {
        case BinOp::Add: return makeHyper(addV(componentsIn(a, n), componentsIn(b, n)));
        case BinOp::Sub: return makeHyper(subV(componentsIn(a, n), componentsIn(b, n)));
        case BinOp::Mul: return makeHyper(mulV(componentsIn(a, n), componentsIn(b, n)));
        case BinOp::Div: return makeHyper(mulV(componentsIn(a, n), inverseV(componentsIn(b, n))));
        case BinOp::LDiv: return makeHyper(mulV(inverseV(componentsIn(a, n)), componentsIn(b, n)));
        case BinOp::Dot: {
            Vec x = componentsIn(a, n), y = componentsIn(b, n);
            Value s = Value::integer(0);
            for (size_t i = 0; i < n; i++) s = vadd(s, vmul(x[i], y[i]));
            return s;
        }
        case BinOp::Pow: {
            if (isHyper(b)) fail("hypercomplex exponents are not supported");
            if (!isIntegerValue(b)) fail("hypercomplex numbers can only be raised to integer powers");
            return makeHyper(powV(componentsIn(a, n), toInt64(b, "an integer exponent")));
        }
        default: break;
    }
    fail(std::string("operator ") + binOpSymbol(op) + " is not defined for hypercomplex numbers");
}

Value hyperNeg(const Value& a) { return makeHyper(negV(as<ObjHyper>(a)->c)); }

Value hyperConj(const Value& a) {
    if (!isHyper(a)) return a;
    return makeHyper(conjV(as<ObjHyper>(a)->c));
}

Value hyperNorm(const Value& a) {
    Value n2 = norm2V(componentsIn(a, dimOf(a)));
    return isExactNumber(n2) || isSym(n2) ? symFunction("sqrt", n2) : sqrtValue(n2);
}

bool hyperEqual(const Value& a, const Value& b) {
    size_t n = std::max(dimOf(a), dimOf(b));
    Vec x = componentsIn(a, n), y = componentsIn(b, n);
    for (size_t i = 0; i < n; i++)
        if (!valuesEqual(x[i], y[i]) && !isZeroValue(vsub(x[i], y[i]))) return false;
    return true;
}

std::string hyperTypeName(const ObjHyper* h) {
    switch (h->c.size()) {
        case 4: return "quaternion";
        case 8: return "octonion";
        case 16: return "sedenion";
        default: return "hypercomplex";
    }
}

std::string hyperToString(const ObjHyper* h) {
    std::string out;
    size_t dim = h->c.size();
    for (size_t k = 0; k < dim; k++) {
        const Value& c = h->c[k];
        if (isZeroValue(c)) continue;
        std::string s = toString(c);
        bool negative = !s.empty() && s[0] == '-';
        if (negative) s = toString(negateValue(c));
        std::string unit = unitName(dim, k);
        std::string term;
        if (k == 0) {
            term = s;
        } else if (s == "1") {
            term = unit;
        } else {
            bool wrap = s.find_first_of(" /") != std::string::npos || (isSym(c) && s.find_first_of("+-") != std::string::npos);
            term = (wrap ? "(" + s + ")" : s) + unit;
        }
        if (out.empty())
            out = negative ? "-" + term : term;
        else
            out += (negative ? " - " : " + ") + term;
    }
    return out.empty() ? "0" : out;
}

namespace {

Value fromArgs(int argc, Value* args, size_t dim, const char* name) {
    Vec c;
    if (argc == 1 && isList(args[0])) {
        c = as<ObjList>(args[0])->items;
    } else {
        c.assign(args, args + argc);
    }
    if (c.size() > dim) fail(std::string(name) + " takes at most " + std::to_string(dim) + " components");
    for (const auto& v : c)
        if (!isRealNumber(v) && !isSym(v))
            fail(std::string(name) + " components must be real numbers or symbolic, got " + typeName(v));
    c.resize(dim, Value::integer(0));
    return makeHyper(std::move(c));
}

Value n_quat(VM&, int argc, Value* args) {
    // quat(s, [x, y, z]) is the scalar-plus-vector form.
    if (argc == 2 && isList(args[1])) {
        Vec c{args[0]};
        for (const auto& v : as<ObjList>(args[1])->items) c.push_back(v);
        if (c.size() != 4) fail("quat(s, v) needs a 3-component vector v");
        return fromArgs(4, c.data(), 4, "quat");
    }
    return fromArgs(argc, args, 4, "quat");
}

Value n_octonion(VM&, int argc, Value* args) { return fromArgs(argc, args, 8, "octonion"); }
Value n_sedenion(VM&, int argc, Value* args) { return fromArgs(argc, args, 16, "sedenion"); }

Value n_hyper(VM&, int, Value* args) {
    if (!isList(args[0])) fail("hyper expects a list of components");
    size_t n = as<ObjList>(args[0])->items.size();
    size_t dim = 4;
    while (dim < n) dim *= 2;
    return fromArgs(1, args, dim, "hyper");
}

Value n_basis(VM&, int, Value* args) {
    int64_t n = toInt64(args[0], "a dimension");
    int64_t k = toInt64(args[1], "a unit index");
    if (n < 4 || !isPowerOfTwo(static_cast<size_t>(n)) || n > 1024)
        fail("basis(n, k): n must be a power of two between 4 and 1024");
    if (k < 0 || k >= n) fail("basis(n, k): k must be between 0 and n - 1");
    Vec c(static_cast<size_t>(n), Value::integer(0));
    c[static_cast<size_t>(k)] = Value::integer(1);
    return makeHyper(std::move(c));
}

Value n_components(VM&, int, Value* args) { return newList(componentsIn(args[0], dimOf(args[0]))); }

Value n_commutator(VM&, int, Value* args) {
    return vsub(vmul(args[0], args[1]), vmul(args[1], args[0]));
}

Value n_associator(VM&, int, Value* args) {
    return vsub(vmul(vmul(args[0], args[1]), args[2]), vmul(args[0], vmul(args[1], args[2])));
}

Value callMath(VM& vm, const char* name, const Value& x) { return vm.call(vm.getGlobal(name), {x}); }

Value simplifyIfSym(const Value& v) { return isSym(v) ? simplifyValue(v) : v; }

// Rotates a 3-vector about an axis with the unit quaternion q = cos(θ/2) + sin(θ/2) n̂.
Value n_rotate(VM& vm, int, Value* args) {
    auto vec3 = [](const Value& v, const char* what) {
        if (!isList(v) || as<ObjList>(v)->items.size() != 3) fail(std::string("rotate: ") + what + " must be a 3-vector");
        return as<ObjList>(v)->items;
    };
    Vec v = vec3(args[0], "the point"), axis = vec3(args[1], "the axis");
    Value n2 = norm2V(axis);
    Value len = isExactNumber(n2) || isSym(n2) ? symFunction("sqrt", n2) : sqrtValue(n2);
    if (isZeroValue(len)) fail("rotate: the axis must not be zero");
    Value halfAngle = vdiv(args[2], Value::integer(2));
    Value s = vdiv(callMath(vm, "sin", halfAngle), len);
    Vec q{callMath(vm, "cos", halfAngle), vmul(s, axis[0]), vmul(s, axis[1]), vmul(s, axis[2])};
    Vec p{Value::integer(0), v[0], v[1], v[2]};
    Vec r = mulV(mulV(q, p), conjV(q));
    return newList({simplifyIfSym(r[1]), simplifyIfSym(r[2]), simplifyIfSym(r[3])});
}

}  // namespace

void registerHyper(VM& vm) {
    const char* c = "hypercomplex";
    vm.defineNative("quat", 1, 4, n_quat, "quat(a, b, c, d) = a + bi + cj + dk; also quat(s, [x, y, z])", c);
    vm.defineNative("quaternion", 1, 4, n_quat, "quaternion(a, b, c, d) - same as quat", c);
    vm.defineNative("octonion", 1, 8, n_octonion, "octonion(c0, ..., c7) or octonion([c0, ..., c7])", c);
    vm.defineNative("sedenion", 1, 16, n_sedenion, "sedenion([c0, ..., c15]) - 16-dimensional Cayley-Dickson number", c);
    vm.defineNative("hyper", 1, 1, n_hyper, "hyper(list) - Cayley-Dickson number with 2^n components", c);
    vm.defineNative("basis", 2, 2, n_basis, "basis(n, k) - unit e_k of the n-dimensional algebra (basis(4, 1) = i)", c);
    vm.defineNative("components", 1, 1, n_components, "components(q) - list of the real components", c);
    vm.defineNative("commutator", 2, 2, n_commutator, "commutator(a, b) = ab - ba", c);
    vm.defineNative("associator", 3, 3, n_associator, "associator(a, b, c) = (ab)c - a(bc)", c);
    vm.defineNative("rotate", 3, 3, n_rotate, "rotate(v, axis, θ) - rotate a 3-vector with a quaternion", c);
}
