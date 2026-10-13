#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <utility>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "cas/bridge.h"
#include "cas/expr.h"
#include "common/error.h"
#include "hyper/hyper.h"
#include "linalg/matrix.h"
#include "numeric/ops.h"
#include "numeric/radical.h"
#include "sets/set.h"
#include "stdlib/stdlib.h"
#include "units/units.h"
#include "vm/arith.h"
#include "vm/vm.h"

using cplx = std::complex<double>;

namespace {

// ---------------------------------------------------------------- elementary functions

struct MathFn {
    const char* name;
    double (*rf)(double);
    cplx (*cf)(const cplx&);
    bool (*domain)(double);  // nullptr: every real argument gives a real result
    const char* doc;
};

cplx cgamma(cplx z) {
    if (z.real() < 0.5) return M_PI / (std::sin(M_PI * z) * cgamma(1.0 - z));
    z -= 1.0;
    static const double p[] = {0.99999999999980993,  676.5203681218851,     -1259.1392167224028,
                               771.32342877765313,   -176.61502916214059,   12.507343278686905,
                               -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7};
    cplx x = p[0];
    for (int i = 1; i < 9; i++) x += p[i] / (z + static_cast<double>(i));
    cplx t = z + 7.5;
    return std::sqrt(2 * M_PI) * std::pow(t, z + 0.5) * std::exp(-t) * x;
}

cplx realOnly(const char* name, const cplx& z) {
    if (z.imag() == 0) return z;
    fail(std::string(name) + " is only defined for real arguments");
}

const MathFn kMath[] = {
    {"sin", [](double x) { return std::sin(x); }, [](const cplx& z) { return std::sin(z); }, nullptr, "sine"},
    {"cos", [](double x) { return std::cos(x); }, [](const cplx& z) { return std::cos(z); }, nullptr, "cosine"},
    {"tan", [](double x) { return std::tan(x); }, [](const cplx& z) { return std::tan(z); }, nullptr, "tangent"},
    {"sec", [](double x) { return 1 / std::cos(x); }, [](const cplx& z) { return 1.0 / std::cos(z); }, nullptr,
     "secant"},
    {"csc", [](double x) { return 1 / std::sin(x); }, [](const cplx& z) { return 1.0 / std::sin(z); }, nullptr,
     "cosecant"},
    {"cot", [](double x) { return 1 / std::tan(x); }, [](const cplx& z) { return 1.0 / std::tan(z); }, nullptr,
     "cotangent"},
    {"asin", [](double x) { return std::asin(x); }, [](const cplx& z) { return std::asin(z); },
     [](double x) { return x >= -1 && x <= 1; }, "inverse sine"},
    {"acos", [](double x) { return std::acos(x); }, [](const cplx& z) { return std::acos(z); },
     [](double x) { return x >= -1 && x <= 1; }, "inverse cosine"},
    {"atan", [](double x) { return std::atan(x); }, [](const cplx& z) { return std::atan(z); }, nullptr,
     "inverse tangent"},
    {"sinh", [](double x) { return std::sinh(x); }, [](const cplx& z) { return std::sinh(z); }, nullptr,
     "hyperbolic sine"},
    {"cosh", [](double x) { return std::cosh(x); }, [](const cplx& z) { return std::cosh(z); }, nullptr,
     "hyperbolic cosine"},
    {"tanh", [](double x) { return std::tanh(x); }, [](const cplx& z) { return std::tanh(z); }, nullptr,
     "hyperbolic tangent"},
    {"asinh", [](double x) { return std::asinh(x); }, [](const cplx& z) { return std::asinh(z); }, nullptr,
     "inverse hyperbolic sine"},
    {"acosh", [](double x) { return std::acosh(x); }, [](const cplx& z) { return std::acosh(z); },
     [](double x) { return x >= 1; }, "inverse hyperbolic cosine"},
    {"atanh", [](double x) { return std::atanh(x); }, [](const cplx& z) { return std::atanh(z); },
     [](double x) { return x > -1 && x < 1; }, "inverse hyperbolic tangent"},
    {"exp", [](double x) { return std::exp(x); }, [](const cplx& z) { return std::exp(z); }, nullptr,
     "exponential e^x"},
    {"ln", [](double x) { return std::log(x); }, [](const cplx& z) { return std::log(z); },
     [](double x) { return x >= 0; }, "natural logarithm"},
    {"log10", [](double x) { return std::log10(x); }, [](const cplx& z) { return std::log10(z); },
     [](double x) { return x >= 0; }, "base-10 logarithm"},
    {"log2", [](double x) { return std::log2(x); }, [](const cplx& z) { return std::log(z) / std::log(2.0); },
     [](double x) { return x >= 0; }, "base-2 logarithm"},
    {"gamma", [](double x) { return std::tgamma(x); }, [](const cplx& z) { return cgamma(z); }, nullptr,
     "gamma function, gamma(n) = (n-1)!"},
    {"erf", [](double x) { return std::erf(x); }, [](const cplx& z) { return realOnly("erf", z); }, nullptr,
     "error function"},
    {"erfc", [](double x) { return std::erfc(x); }, [](const cplx& z) { return realOnly("erfc", z); }, nullptr,
     "complementary error function"},
};
constexpr size_t kMathCount = sizeof(kMath) / sizeof(kMath[0]);

cas::E casApply(const std::string& name, const cas::E& e) {
    if (name == "exp") return cas::pow(cas::eConst(), e);
    if (name == "log10") return cas::div(cas::fn("ln", e), cas::fn("ln", cas::num(10)));
    if (name == "log2") return cas::div(cas::fn("ln", e), cas::fn("ln", cas::num(2)));
    return cas::fn(name, e);
}

bool containsFn(const cas::E& e) {
    if (e->kind == cas::Kind::Fn) return true;
    for (const auto& a : e->args)
        if (containsFn(a)) return true;
    return false;
}

Value numericFromExpr(const cas::E& e, const char* fname) {
    cplx c;
    if (!cas::evalComplex(e, nullptr, c)) fail(std::string(fname) + ": could not evaluate numerically");
    return fromComplex(c);
}

// Symbolic arguments: constants such as π/6 give exact results when the CAS knows them and
// fall back to a floating-point value otherwise; expressions with free symbols stay symbolic.
Value symMath(const std::string& name, const Value& x) {
    cas::E e = toExpr(x);
    cas::E r = casApply(name, e);
    if (cas::freeSymbols(e).empty() && containsFn(r)) return numericFromExpr(r, name.c_str());
    return fromExpr(r);
}

Value applyMath(const MathFn& m, const Value& x) {
    if (isList(x) || isMatrix(x) || isRange(x)) return mapElements(x, [&](const Value& e) { return applyMath(m, e); });
    if (isQuantity(x)) return applyMath(m, dimensionlessMagnitude(x, m.name));
    if (isSym(x)) return symMath(m.name, x);
    if (!isNumber(x)) fail(std::string(m.name) + " expects a number, got " + typeName(x));
    if (isExactNumber(x)) {
        if (std::string(m.name) == "gamma" && isIntegerValue(x) && numSign(x) > 0)
            return numFactorial(numSub(x, Value::integer(1)));
        cas::E r = casApply(m.name, toExpr(x));
        if (cas::isNum(r) && r->exact) return fromExpr(r);
    }
    if (numKind(x) == NumKind::Cplx) return fromComplex(m.cf(toComplex(x)));
    double d = toDouble(x);
    if (m.domain && !m.domain(d)) return fromComplex(m.cf(cplx(d, 0)));
    return Value::real(m.rf(d));
}

template <size_t K>
Value n_math(VM&, int, Value* args) {
    return applyMath(kMath[K], args[0]);
}

template <size_t... K>
void registerMathTable(VM& vm, std::index_sequence<K...>) {
    (void)std::initializer_list<int>{(vm.defineNative(kMath[K].name, 1, 1, n_math<K>, kMath[K].doc, "math"), 0)...};
}

// Applies an exact numeric operation elementwise, through units and symbolic constants.
Value mapNumeric(const Value& x, const char* fname, Value (*f)(const Value&)) {
    if (isList(x) || isMatrix(x) || isRange(x))
        return mapElements(x, [&](const Value& e) { return mapNumeric(e, fname, f); });
    if (isSym(x)) {
        double d;
        if (symToDouble(x, d)) return f(Value::real(d));
        fail(std::string(fname) + " needs a numeric argument, got the expression " + toString(x));
    }
    if (isQuantity(x)) return mapNumeric(dimensionlessMagnitude(x, fname), fname, f);
    if (isRadical(x)) return f(Value::real(toDouble(x)));
    if (!isNumber(x)) fail(std::string(fname) + " expects a number, got " + typeName(x));
    return f(x);
}

Value n_sqrt(VM&, int, Value* args) {
    if (isList(args[0]) || isRange(args[0])) return mapElements(args[0], sqrtValue);
    return sqrtValue(args[0]);
}
Value n_cbrt(VM&, int, Value* args) {
    if (isList(args[0]) || isRange(args[0])) return mapElements(args[0], cbrtValue);
    return cbrtValue(args[0]);
}

Value n_root(VM&, int, Value* args) {
    if (!isIntegerValue(args[1]) || numSign(args[1]) <= 0) fail("nthroot(x, n) needs a positive integer n");
    return vpow(args[0], fromRational(Rational(BigInt(1), toBigInt(args[1]))));
}

Value n_abs(VM&, int, Value* args) {
    const Value& x = args[0];
    if (isList(x) || isMatrix(x) || isRange(x)) return mapElements(x, absValue);
    return absValue(x);
}

constexpr size_t kLn = 16;

Value n_log(VM&, int argc, Value* args) {
    Value x = args[0];
    if (argc == 1) return applyMath(kMath[kLn], x);
    Value b = args[1];
    if (isIntegerValue(x) && isIntegerValue(b) && isExactNumber(x) && isExactNumber(b) && numSign(x) > 0 &&
        numCompare(b, Value::integer(1)) > 0) {
        BigInt xv = toBigInt(x), bv = toBigInt(b), p(1);
        int64_t k = 0;
        while (p < xv) {
            p = p * bv;
            k++;
        }
        if (p == xv) return Value::integer(k);
    }
    return vdiv(applyMath(kMath[kLn], x), applyMath(kMath[kLn], b));
}

Value n_atan2(VM&, int, Value* args) {
    return Value::real(std::atan2(toDouble(mapNumeric(args[0], "atan2", [](const Value& v) { return v; })),
                                  toDouble(mapNumeric(args[1], "atan2", [](const Value& v) { return v; }))));
}

Value n_hypot(VM&, int argc, Value* args) {
    Value acc = Value::integer(0);
    for (int i = 0; i < argc; i++) acc = vadd(acc, vmul(args[i], args[i]));
    return sqrtValue(acc);
}

Value n_floor(VM&, int, Value* args) { return mapNumeric(args[0], "floor", numFloor); }
Value n_ceil(VM&, int, Value* args) { return mapNumeric(args[0], "ceil", numCeil); }
Value n_trunc(VM&, int, Value* args) { return mapNumeric(args[0], "trunc", numTrunc); }

Value n_div(VM&, int, Value* args) {
    if (!isRealNumber(args[0]) || !isRealNumber(args[1])) fail("div expects two real numbers");
    if (numIsZero(args[1])) fail("division by zero");
    return numFloorDiv(args[0], args[1]);
}

Value n_round(VM&, int argc, Value* args) {
    if (argc == 1) return mapNumeric(args[0], "round", numRound);
    int64_t digits = toInt64(args[1], "a digit count");
    Value scale = numPow(Value::integer(10), Value::integer(digits));
    return mapElements(args[0], [&](const Value& v) {
        Value x = mapNumeric(v, "round", [](const Value& y) { return y; });
        Value r = numDiv(numRound(numMul(x, scale)), scale);
        return isExactNumber(x) ? r : Value::real(toDouble(r));
    });
}

Value n_frac(VM&, int, Value* args) {
    return mapNumeric(args[0], "frac", [](const Value& v) { return numSub(v, numFloor(v)); });
}

Value n_sign(VM&, int, Value* args) {
    return mapNumeric(args[0], "sign", [](const Value& v) {
        if (numKind(v) == NumKind::Cplx) {
            cplx z = toComplex(v);
            return z == 0.0 ? Value::integer(0) : fromComplex(z / std::abs(z));
        }
        return Value::integer(numSign(v));
    });
}

// ---------------------------------------------------------------- complex parts

Value complexPart(const Value& x, const char* fname, int which) {
    if (isList(x) || isMatrix(x)) return mapElements(x, [&](const Value& e) { return complexPart(e, fname, which); });
    Value v = x;
    if (isSym(x)) v = numericFromExpr(toExpr(x), fname);
    if (!isNumber(v)) fail(std::string(fname) + " expects a number, got " + typeName(x));
    bool isC = numKind(v) == NumKind::Cplx;
    cplx z = toComplex(v);
    switch (which) {
        case 0: return isC ? Value::real(z.real()) : v;
        case 1: return isC ? Value::real(z.imag()) : Value::integer(0);
        case 2: return isC ? fromComplex(std::conj(z)) : v;
        default:
            if (!isC && numSign(v) >= 0) return Value::integer(0);
            return Value::real(std::arg(z));
    }
}

Value n_re(VM&, int, Value* args) {
    if (isHyper(args[0])) return as<ObjHyper>(args[0])->c[0];
    return complexPart(args[0], "re", 0);
}
Value n_im(VM&, int, Value* args) {
    if (isHyper(args[0])) {
        const auto& c = as<ObjHyper>(args[0])->c;
        return newList(std::vector<Value>(c.begin() + 1, c.end()));
    }
    return complexPart(args[0], "im", 1);
}
Value n_conj(VM&, int, Value* args) {
    if (isHyper(args[0])) return hyperConj(args[0]);
    return complexPart(args[0], "conj", 2);
}
Value n_arg(VM&, int, Value* args) { return complexPart(args[0], "arg", 3); }

// ---------------------------------------------------------------- conversions

Value toFloat(const Value& x) {
    if (isList(x) || isMatrix(x) || isRange(x)) return mapElements(x, toFloat);
    if (isSym(x)) {
        cas::E e = cas::evalf(toExpr(x));
        return fromExpr(e);
    }
    if (isQuantity(x)) {
        const ObjQuantity* q = as<ObjQuantity>(x);
        return makeQuantity(toFloat(q->mag), q->dim, q->label, q->labelFactor);
    }
    if (isRadical(x)) return Value::real(toDouble(x));
    if (!isNumber(x)) fail("float expects a number, got " + typeName(x));
    if (numKind(x) == NumKind::Cplx) return x;
    return Value::real(toDouble(x));
}

Value n_float(VM&, int, Value* args) { return toFloat(args[0]); }

Rational bestRational(double x, double tol) {
    if (!std::isfinite(x)) fail("rationalize: value is not finite");
    BigInt h0(0), h1(1), k0(1), k1(0);
    double r = x;
    for (int iter = 0; iter < 64; iter++) {
        double a = std::floor(r);
        BigInt ai = BigInt::fromDouble(a);
        BigInt h2 = ai * h1 + h0, k2 = ai * k1 + k0;
        h0 = h1;
        h1 = h2;
        k0 = k1;
        k1 = k2;
        double approx = h1.toDouble() / k1.toDouble();
        if (std::fabs(approx - x) <= tol * std::max(1.0, std::fabs(x))) break;
        double f = r - a;
        if (f == 0) break;
        r = 1 / f;
    }
    return Rational(h1, k1);
}

Value toExact(const Value& x, double tol) {
    if (isList(x) || isMatrix(x)) return mapElements(x, [&](const Value& e) { return toExact(e, tol); });
    if (isSym(x)) return x;
    if (!isNumber(x)) fail("rationalize expects a number, got " + typeName(x));
    if (isExactNumber(x)) return x;
    if (numKind(x) == NumKind::Cplx) {
        cplx z = toComplex(x);
        return vadd(fromRational(bestRational(z.real(), tol)),
                    vmul(fromRational(bestRational(z.imag(), tol)), fromComplex(cplx(0, 1))));
    }
    return fromRational(bestRational(toDouble(x), tol));
}

Value n_rationalize(VM&, int argc, Value* args) {
    double tol = argc > 1 ? toDouble(args[1]) : 1e-12;
    return toExact(args[0], tol);
}

Value n_numer(VM&, int, Value* args) {
    const Value& x = args[0];
    if (isSym(x)) {
        cas::E n, d;
        cas::numDen(toExpr(x), n, d);
        return fromExpr(n);
    }
    if (!isExactNumber(x)) fail("numer expects an exact number or expression");
    return fromBigInt(toRational(x).num);
}

Value n_denom(VM&, int, Value* args) {
    const Value& x = args[0];
    if (isSym(x)) {
        cas::E n, d;
        cas::numDen(toExpr(x), n, d);
        return fromExpr(d);
    }
    if (!isExactNumber(x)) fail("denom expects an exact number or expression");
    return fromBigInt(toRational(x).den);
}

Value n_isint(VM&, int, Value* args) { return Value::boolean(isNumber(args[0]) && isIntegerValue(args[0])); }
Value n_iseven(VM&, int, Value* args) {
    return Value::boolean(isIntegerValue(args[0]) && toBigInt(args[0]).modSmall(2) == 0);
}
Value n_isodd(VM&, int, Value* args) {
    return Value::boolean(isIntegerValue(args[0]) && toBigInt(args[0]).modSmall(2) != 0);
}

// ---------------------------------------------------------------- number theory

BigInt intArg(const Value& v, const char* fname) {
    if (!isNumber(v) || !isIntegerValue(v)) fail(std::string(fname) + " expects an integer, got " + toString(v));
    return toBigInt(v);
}

uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) {
#ifdef _MSC_VER
    uint64_t hi;
    uint64_t lo = _umul128(a, b, &hi);
    uint64_t rem;
    _udiv128(hi, lo, m, &rem);
    return rem;
#else
    return static_cast<uint64_t>((static_cast<unsigned __int128>(a) * b) % m);
#endif
}

uint64_t powmod64(uint64_t b, uint64_t e, uint64_t m) {
    uint64_t r = 1 % m;
    b %= m;
    while (e) {
        if (e & 1) r = mulmod(r, b, m);
        b = mulmod(b, b, m);
        e >>= 1;
    }
    return r;
}

bool isPrime64(uint64_t n) {
    if (n < 2) return false;
    static const uint64_t small[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (uint64_t p : small) {
        if (n % p == 0) return n == p;
    }
    uint64_t d = n - 1;
    int s = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        s++;
    }
    for (uint64_t a : small) {
        uint64_t x = powmod64(a, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composite = true;
        for (int r = 1; r < s; r++) {
            x = mulmod(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

BigInt powmodBig(BigInt b, BigInt e, const BigInt& m) {
    BigInt r(1);
    b = b % m;
    if (b < BigInt(0)) b = b + m;
    while (e > BigInt(0)) {
        if (e.modSmall(2) == 1) r = (r * b) % m;
        b = (b * b) % m;
        e = e.shr(1);
    }
    return r;
}

bool isPrimeBig(const BigInt& n) {
    if (n.fitsInt64()) {
        int64_t v = n.toInt64();
        return v > 1 && isPrime64(static_cast<uint64_t>(v));
    }
    if (n.modSmall(2) == 0) return false;
    static const int64_t bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};
    for (int64_t p : bases)
        if (n.modSmall(static_cast<uint32_t>(p)) == 0) return false;
    BigInt nm1 = n - BigInt(1), d = nm1;
    int s = 0;
    while (d.modSmall(2) == 0) {
        d = d.shr(1);
        s++;
    }
    for (int64_t a : bases) {
        BigInt x = powmodBig(BigInt(a), d, n);
        if (x == BigInt(1) || x == nm1) continue;
        bool composite = true;
        for (int r = 1; r < s; r++) {
            x = (x * x) % n;
            if (x == nm1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

uint64_t gcd64(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

uint64_t pollardRho(uint64_t n) {
    if (n % 2 == 0) return 2;
    for (uint64_t c = 1; c < 100; c++) {
        uint64_t x = 2, y = 2, d = 1;
        auto f = [&](uint64_t v) { return (mulmod(v, v, n) + c) % n; };
        while (d == 1) {
            x = f(x);
            y = f(f(y));
            d = gcd64(x > y ? x - y : y - x, n);
        }
        if (d != n) return d;
    }
    return n;
}

void factor64(uint64_t n, std::vector<uint64_t>& out) {
    if (n == 1) return;
    if (isPrime64(n)) {
        out.push_back(n);
        return;
    }
    uint64_t d = pollardRho(n);
    if (d == n) fail("factorint: failed to factor " + std::to_string(n));
    factor64(d, out);
    factor64(n / d, out);
}

BigInt pollardRhoBig(const BigInt& n) {
    for (int64_t c = 1; c < 20; c++) {
        BigInt x(2), y(2), d(1), cc(c);
        for (int iter = 0; iter < 2000000 && d == BigInt(1); iter++) {
            x = (x * x + cc) % n;
            y = (y * y + cc) % n;
            y = (y * y + cc) % n;
            d = BigInt::gcd((x - y).abs(), n);
        }
        if (d != n && d != BigInt(1)) return d;
    }
    fail("factorint: number is too hard to factor");
}

void factorBig(const BigInt& n, std::vector<BigInt>& out) {
    if (n == BigInt(1)) return;
    if (n.fitsInt64()) {
        std::vector<uint64_t> f;
        factor64(static_cast<uint64_t>(n.toInt64()), f);
        for (uint64_t p : f) out.push_back(BigInt(static_cast<int64_t>(p)));
        return;
    }
    if (isPrimeBig(n)) {
        out.push_back(n);
        return;
    }
    BigInt d = pollardRhoBig(n);
    factorBig(d, out);
    factorBig(n / d, out);
}

// Prime factorisation as ascending (prime, exponent) pairs.
std::vector<std::pair<BigInt, int64_t>> factorize(BigInt n) {
    std::vector<std::pair<BigInt, int64_t>> result;
    if (n < BigInt(0)) n = -n;
    if (n < BigInt(2)) return result;
    std::vector<BigInt> primes;
    for (uint32_t p = 2; p < 1000; p++) {
        while (n.modSmall(p) == 0) {
            primes.push_back(BigInt(static_cast<int64_t>(p)));
            n = n / BigInt(static_cast<int64_t>(p));
        }
    }
    factorBig(n, primes);
    std::sort(primes.begin(), primes.end());
    for (const auto& p : primes) {
        if (!result.empty() && result.back().first == p)
            result.back().second++;
        else
            result.push_back({p, 1});
    }
    return result;
}

Value n_isprime(VM&, int, Value* args) {
    if (!isNumber(args[0]) || !isIntegerValue(args[0])) return Value::boolean(false);
    return Value::boolean(isPrimeBig(toBigInt(args[0])));
}

Value n_nextprime(VM&, int, Value* args) {
    BigInt n = intArg(args[0], "nextprime") + BigInt(1);
    if (n < BigInt(2)) return Value::integer(2);
    while (!isPrimeBig(n)) n = n + BigInt(1);
    return fromBigInt(n);
}

Value n_prevprime(VM&, int, Value* args) {
    BigInt n = intArg(args[0], "prevprime") - BigInt(1);
    while (n >= BigInt(2) && !isPrimeBig(n)) n = n - BigInt(1);
    if (n < BigInt(2)) fail("prevprime: no prime below 2");
    return fromBigInt(n);
}

std::vector<int64_t> sieve(int64_t n) {
    std::vector<int64_t> primes;
    if (n < 2) return primes;
    std::vector<bool> composite(static_cast<size_t>(n + 1), false);
    for (int64_t i = 2; i <= n; i++) {
        if (composite[static_cast<size_t>(i)]) continue;
        primes.push_back(i);
        for (int64_t j = i * i; j <= n; j += i) composite[static_cast<size_t>(j)] = true;
    }
    return primes;
}

Value n_primes(VM&, int argc, Value* args) {
    int64_t lo = 2, hi;
    if (argc == 1) {
        hi = toInt64(args[0]);
    } else {
        lo = toInt64(args[0]);
        hi = toInt64(args[1]);
    }
    if (hi > 100000000) fail("primes: limit too large (max 10^8)");
    std::vector<Value> out;
    for (int64_t p : sieve(hi))
        if (p >= lo) out.push_back(Value::integer(p));
    return newList(std::move(out));
}

Value n_prime(VM&, int, Value* args) {
    int64_t k = toInt64(args[0]);
    if (k < 1) fail("prime(k) needs k >= 1");
    int64_t limit = k < 6 ? 15 : static_cast<int64_t>(k * (std::log(k) + std::log(std::log(k)))) + 10;
    std::vector<int64_t> ps = sieve(limit);
    return Value::integer(ps[static_cast<size_t>(k - 1)]);
}

Value n_factorint(VM&, int, Value* args) {
    std::vector<Value> out;
    for (auto& pe : factorize(intArg(args[0], "factorint")))
        out.push_back(newList({fromBigInt(pe.first), Value::integer(pe.second)}));
    return newList(std::move(out));
}

Value n_primefactors(VM&, int, Value* args) {
    std::vector<Value> out;
    for (auto& pe : factorize(intArg(args[0], "primefactors")))
        for (int64_t k = 0; k < pe.second; k++) out.push_back(fromBigInt(pe.first));
    return newList(std::move(out));
}

Value n_divisors(VM&, int, Value* args) {
    BigInt n = intArg(args[0], "divisors");
    if (n == BigInt(0)) fail("divisors: 0 has infinitely many divisors");
    std::vector<BigInt> divs{BigInt(1)};
    for (auto& pe : factorize(n)) {
        size_t size = divs.size();
        BigInt pk(1);
        for (int64_t k = 1; k <= pe.second; k++) {
            pk = pk * pe.first;
            for (size_t i = 0; i < size; i++) divs.push_back(divs[i] * pk);
        }
        if (divs.size() > 1000000) fail("divisors: too many divisors");
    }
    std::sort(divs.begin(), divs.end());
    std::vector<Value> out;
    for (auto& d : divs) out.push_back(fromBigInt(d));
    return newList(std::move(out));
}

Value n_totient(VM&, int, Value* args) {
    BigInt n = intArg(args[0], "totient");
    if (n < BigInt(1)) fail("totient needs a positive integer");
    BigInt r = n;
    for (auto& pe : factorize(n)) r = r / pe.first * (pe.first - BigInt(1));
    return fromBigInt(r);
}

Value n_gcd(VM&, int argc, Value* args) {
    std::vector<Value> xs = (argc == 1 && isIterable(args[0])) ? iterableToVector(args[0])
                                                                : std::vector<Value>(args, args + argc);
    BigInt g(0);
    for (const auto& x : xs) g = BigInt::gcd(g, intArg(x, "gcd"));
    return fromBigInt(g);
}

Value n_lcm(VM&, int argc, Value* args) {
    std::vector<Value> xs = (argc == 1 && isIterable(args[0])) ? iterableToVector(args[0])
                                                                : std::vector<Value>(args, args + argc);
    BigInt l(1);
    for (const auto& x : xs) {
        BigInt v = intArg(x, "lcm").abs();
        if (v == BigInt(0)) return Value::integer(0);
        l = l / BigInt::gcd(l, v) * v;
    }
    return fromBigInt(l);
}

Value n_powmod(VM&, int, Value* args) {
    BigInt b = intArg(args[0], "powmod"), e = intArg(args[1], "powmod"), m = intArg(args[2], "powmod");
    if (m <= BigInt(0)) fail("powmod: modulus must be positive");
    if (e < BigInt(0)) fail("powmod: negative exponent (use modinv)");
    return fromBigInt(powmodBig(b, e, m));
}

Value n_modinv(VM&, int, Value* args) {
    BigInt a = intArg(args[0], "modinv"), m = intArg(args[1], "modinv");
    BigInt r0 = m, r1 = a % m, s0(0), s1(1);
    if (r1 < BigInt(0)) r1 = r1 + m;
    while (r1 != BigInt(0)) {
        BigInt q = r0 / r1;
        BigInt r2 = r0 - q * r1, s2 = s0 - q * s1;
        r0 = r1;
        r1 = r2;
        s0 = s1;
        s1 = s2;
    }
    if (r0 != BigInt(1)) fail("modinv: " + a.toString() + " has no inverse modulo " + m.toString());
    BigInt r = s0 % m;
    if (r < BigInt(0)) r = r + m;
    return fromBigInt(r);
}

Value n_binomial(VM&, int, Value* args) {
    if (isSym(args[0]) || isSym(args[1])) {
        cas::E n = toExpr(args[0]), k = toExpr(args[1]);
        return fromExpr(cas::div(cas::fn("factorial", n),
                                 cas::mul(cas::fn("factorial", k), cas::fn("factorial", cas::sub(n, k)))));
    }
    if (!isIntegerValue(args[0]) || !isIntegerValue(args[1])) {
        double n = toDouble(args[0]), k = toDouble(args[1]);
        return Value::real(std::exp(std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1)));
    }
    BigInt n = toBigInt(args[0]);
    int64_t k = toInt64(args[1]);
    if (k < 0) return Value::integer(0);
    if (n >= BigInt(0) && BigInt(k) > n) return Value::integer(0);
    if (n >= BigInt(0) && BigInt(2 * k) > n && n.fitsInt64()) k = n.toInt64() - k;
    BigInt r(1);
    for (int64_t i = 0; i < k; i++) r = r * (n - BigInt(i)) / BigInt(i + 1);
    return fromBigInt(r);
}

Value n_fib(VM&, int, Value* args) {
    int64_t n = toInt64(args[0]);
    bool negate = n < 0 && (-n) % 2 == 0;
    if (n < 0) n = -n;
    BigInt a(0), b(1);
    for (int bit = 62; bit >= 0; bit--) {
        BigInt c = a * (b.shl(1) - a);
        BigInt d = a * a + b * b;
        if ((n >> bit) & 1) {
            a = d;
            b = c + d;
        } else {
            a = c;
            b = d;
        }
    }
    return fromBigInt(negate ? -a : a);
}

Value n_digits(VM&, int argc, Value* args) {
    BigInt n = intArg(args[0], "digits").abs();
    int64_t base = argc > 1 ? toInt64(args[1]) : 10;
    if (base < 2) fail("digits: base must be at least 2");
    std::vector<Value> out;
    if (n == BigInt(0)) out.push_back(Value::integer(0));
    BigInt b(base);
    while (n > BigInt(0)) {
        out.push_back(Value::integer(static_cast<int64_t>(n.modSmall(static_cast<uint32_t>(base)))));
        n = n / b;
    }
    std::reverse(out.begin(), out.end());
    return newList(std::move(out));
}

Value n_factorial(VM&, int, Value* args) { return factorialValue(args[0]); }

Value n_beta(VM&, int, Value* args) {
    double a = toDouble(args[0]), b = toDouble(args[1]);
    return Value::real(std::exp(std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b)));
}

// ---------------------------------------------------------------- statistics, min/max

std::vector<Value> varArgs(int argc, Value* args) {
    if (argc == 1 && isIterable(args[0])) return iterableToVector(args[0]);
    return std::vector<Value>(args, args + argc);
}

Value extremum(int argc, Value* args, bool wantMax, const char* fname) {
    std::vector<Value> xs = varArgs(argc, args);
    if (xs.empty()) fail(std::string(fname) + ": no values");
    Value best = xs[0];
    for (size_t i = 1; i < xs.size(); i++) {
        bool better = isTruthy(compareOp(wantMax ? CmpOp::Gt : CmpOp::Lt, xs[i], best));
        if (better) best = xs[i];
    }
    return best;
}

Value n_min(VM&, int argc, Value* args) { return extremum(argc, args, false, "min"); }
Value n_max(VM&, int argc, Value* args) { return extremum(argc, args, true, "max"); }

Value meanOf(const std::vector<Value>& xs) {
    if (xs.empty()) fail("mean: no values");
    Value s = Value::integer(0);
    for (const auto& x : xs) s = vadd(s, x);
    return vdiv(s, Value::integer(static_cast<int64_t>(xs.size())));
}

Value n_mean(VM&, int argc, Value* args) { return meanOf(varArgs(argc, args)); }

Value n_median(VM&, int argc, Value* args) {
    std::vector<Value> xs = varArgs(argc, args);
    if (xs.empty()) fail("median: no values");
    std::sort(xs.begin(), xs.end(), [](const Value& a, const Value& b) { return isTruthy(compareOp(CmpOp::Lt, a, b)); });
    size_t n = xs.size();
    if (n % 2) return xs[n / 2];
    return vdiv(vadd(xs[n / 2 - 1], xs[n / 2]), Value::integer(2));
}

Value varianceOf(const std::vector<Value>& xs) {
    if (xs.size() < 2) fail("variance needs at least two values");
    Value m = meanOf(xs);
    Value s = Value::integer(0);
    for (const auto& x : xs) {
        Value d = vsub(x, m);
        s = vadd(s, vmul(d, d));
    }
    return vdiv(s, Value::integer(static_cast<int64_t>(xs.size() - 1)));
}

Value n_variance(VM&, int argc, Value* args) { return varianceOf(varArgs(argc, args)); }
Value n_stdev(VM&, int argc, Value* args) { return sqrtValue(varianceOf(varArgs(argc, args))); }

Value n_linspace(VM&, int argc, Value* args) {
    double a = toDouble(args[0]), b = toDouble(args[1]);
    int64_t n = argc > 2 ? toInt64(args[2]) : 50;
    if (n < 2) fail("linspace needs at least 2 points");
    std::vector<Value> out;
    for (int64_t k = 0; k < n; k++) out.push_back(Value::real(a + (b - a) * static_cast<double>(k) / static_cast<double>(n - 1)));
    return newList(std::move(out));
}

Value n_deg(VM&, int, Value* args) {
    return mapNumeric(args[0], "deg", [](const Value& v) { return Value::real(toDouble(v) * 180.0 / M_PI); });
}
Value n_rad(VM&, int, Value* args) {
    return mapNumeric(args[0], "rad", [](const Value& v) { return Value::real(toDouble(v) * M_PI / 180.0); });
}

}  // namespace

// ---------------------------------------------------------------- shared operator helpers

Value sqrtValue(const Value& v) {
    if (isRadical(v)) return radicalBinary(BinOp::Pow, v, fromRational(Rational(BigInt(1), BigInt(2))));
    if (isNumber(v)) return numSqrt(v);
    if (isSym(v)) return fromExpr(cas::sqrt(toExpr(v)));
    if (isQuantity(v)) return vpow(v, fromRational(Rational(BigInt(1), BigInt(2))));
    if (isList(v) || isMatrix(v) || isRange(v)) return mapElements(v, sqrtValue);
    fail("sqrt expects a number, got " + typeName(v));
}

Value cbrtValue(const Value& v) {
    if (isNumber(v)) return numCbrt(v);
    if (isSym(v)) return fromExpr(cas::pow(toExpr(v), cas::num(Rational(BigInt(1), BigInt(3)))));
    if (isQuantity(v)) return vpow(v, fromRational(Rational(BigInt(1), BigInt(3))));
    if (isList(v) || isMatrix(v) || isRange(v)) return mapElements(v, cbrtValue);
    fail("cbrt expects a number, got " + typeName(v));
}

// |x|: magnitude of numbers, length of vectors, determinant of matrices, cardinality of sets.
Value absValue(const Value& v) {
    if (numKind(v) == NumKind::Cplx) {
        std::complex<double> z = toComplex(v);
        if (z.real() == std::floor(z.real()) && z.imag() == std::floor(z.imag()) && std::fabs(z.real()) < 1e9 &&
            std::fabs(z.imag()) < 1e9) {
            int64_t a = static_cast<int64_t>(z.real()), b = static_cast<int64_t>(z.imag());
            Value s = numSqrt(Value::integer(a * a + b * b));
            if (isExactNumber(s)) return s;
        }
    }
    if (isRadical(v)) return radicalAbs(v);
    if (isNumber(v)) return numAbs(v);
    if (isHyper(v)) return hyperNorm(v);
    if (isList(v)) return vectorNorm(v);
    if (isMatrix(v)) return determinant(v);
    if (isSet(v)) return Value::integer(static_cast<int64_t>(as<ObjSet>(v)->items.size()));
    if (isRange(v)) return Value::integer(rangeCount(as<ObjRange>(v)));
    if (isSym(v)) return symMath("abs", v);
    if (isQuantity(v)) {
        const ObjQuantity* q = as<ObjQuantity>(v);
        return makeQuantity(absValue(q->mag), q->dim, q->label, q->labelFactor);
    }
    fail("|x| is not defined for a value of type " + typeName(v));
}

Value factorialValue(const Value& v) {
    if (isNumber(v)) return numFactorial(v);
    if (isSym(v)) return fromExpr(cas::fn("factorial", toExpr(v)));
    if (isList(v) || isRange(v)) return mapElements(v, factorialValue);
    fail("factorial expects a number, got " + typeName(v));
}

void registerMath(VM& vm) {
    registerMathTable(vm, std::make_index_sequence<kMathCount>{});
    const char* m = "math";
    vm.defineNative("sqrt", 1, 1, n_sqrt, "sqrt(x) - square root; exact for rationals, as a radical when it is irrational", m);
    vm.defineNative("cbrt", 1, 1, n_cbrt, "cbrt(x) - real cube root", m);
    vm.defineNative("nthroot", 2, 2, n_root, "nthroot(x, n) - real n-th root", m);
    vm.defineNative("abs", 1, 1, n_abs, "abs(x) - absolute value, elementwise on lists", m);
    vm.defineNative("log", 1, 2, n_log, "log(x) - natural log; log(x, b) - log base b", m);
    vm.defineNative("atan2", 2, 2, n_atan2, "atan2(y, x) - angle of the point (x, y)", m);
    vm.defineNative("hypot", 1, -1, n_hypot, "hypot(a, b, ...) - sqrt(a^2 + b^2 + ...)", m);
    vm.defineNative("floor", 1, 1, n_floor, "floor(x) - largest integer <= x", m);
    vm.defineNative("ceil", 1, 1, n_ceil, "ceil(x) - smallest integer >= x", m);
    vm.defineNative("round", 1, 2, n_round, "round(x) - nearest integer; round(x, n) - n decimals", m);
    vm.defineNative("trunc", 1, 1, n_trunc, "trunc(x) - integer part", m);
    vm.defineNative("div", 2, 2, n_div, "div(a, b) - floor division: floor(a / b)", m);
    vm.defineNative("frac", 1, 1, n_frac, "frac(x) - x - floor(x)", m);
    vm.defineNative("sign", 1, 1, n_sign, "sign(x) - -1, 0 or 1", m);
    vm.defineNative("deg", 1, 1, n_deg, "deg(x) - radians to degrees", m);
    vm.defineNative("rad", 1, 1, n_rad, "rad(x) - degrees to radians", m);
    vm.defineNative("factorial", 1, 1, n_factorial, "factorial(n) - n!", m);
    vm.defineNative("beta", 2, 2, n_beta, "beta(a, b) - Euler beta function", m);

    const char* c = "complex";
    vm.defineNative("re", 1, 1, n_re, "re(z) - real part", c);
    vm.defineNative("im", 1, 1, n_im, "im(z) - imaginary part", c);
    vm.defineNative("conj", 1, 1, n_conj, "conj(z) - complex conjugate", c);
    vm.defineNative("arg", 1, 1, n_arg, "arg(z) - phase angle", c);

    const char* cv = "conversion";
    vm.defineNative("float", 1, 1, n_float, "float(x) - floating-point value of x", cv);
    vm.defineNative("N", 1, 1, n_float, "N(x) - numeric value of x (same as float)", cv);
    vm.defineNative("rationalize", 1, 2, n_rationalize, "rationalize(x, tol) - nearby exact fraction", cv);
    vm.defineNative("exact", 1, 2, n_rationalize, "exact(x) - same as rationalize", cv);
    vm.defineNative("numer", 1, 1, n_numer, "numer(q) - numerator", cv);
    vm.defineNative("denom", 1, 1, n_denom, "denom(q) - denominator", cv);
    vm.defineNative("isint", 1, 1, n_isint, "isint(x) - true if x is an integer", cv);
    vm.defineNative("iseven", 1, 1, n_iseven, "iseven(n)", cv);
    vm.defineNative("isodd", 1, 1, n_isodd, "isodd(n)", cv);

    const char* nt = "number theory";
    vm.defineNative("gcd", 1, -1, n_gcd, "gcd(a, b, ...) - greatest common divisor", nt);
    vm.defineNative("lcm", 1, -1, n_lcm, "lcm(a, b, ...) - least common multiple", nt);
    vm.defineNative("isprime", 1, 1, n_isprime, "isprime(n) - primality test", nt);
    vm.defineNative("nextprime", 1, 1, n_nextprime, "nextprime(n) - smallest prime > n", nt);
    vm.defineNative("prevprime", 1, 1, n_prevprime, "prevprime(n) - largest prime < n", nt);
    vm.defineNative("primes", 1, 2, n_primes, "primes(n) - primes up to n; primes(a, b)", nt);
    vm.defineNative("prime", 1, 1, n_prime, "prime(k) - the k-th prime", nt);
    vm.defineNative("factorint", 1, 1, n_factorint, "factorint(n) - [[prime, exponent], ...]", nt);
    vm.defineNative("primefactors", 1, 1, n_primefactors, "primefactors(n) - prime factors with repetition", nt);
    vm.defineNative("divisors", 1, 1, n_divisors, "divisors(n) - sorted positive divisors", nt);
    vm.defineNative("totient", 1, 1, n_totient, "totient(n) - Euler's phi", nt);
    vm.defineNative("powmod", 3, 3, n_powmod, "powmod(b, e, m) - b^e mod m", nt);
    vm.defineNative("modinv", 2, 2, n_modinv, "modinv(a, m) - inverse of a modulo m", nt);
    vm.defineNative("binomial", 2, 2, n_binomial, "binomial(n, k) - n choose k", nt);
    vm.defineNative("choose", 2, 2, n_binomial, "choose(n, k) - n choose k", nt);
    vm.defineNative("fib", 1, 1, n_fib, "fib(n) - n-th Fibonacci number", nt);
    vm.defineNative("digits", 1, 2, n_digits, "digits(n, base) - list of digits", nt);

    const char* st = "statistics";
    vm.defineNative("min", 1, -1, n_min, "min(a, b, ...) or min(xs)", st);
    vm.defineNative("max", 1, -1, n_max, "max(a, b, ...) or max(xs)", st);
    vm.defineNative("mean", 1, -1, n_mean, "mean(xs) - arithmetic mean", st);
    vm.defineNative("median", 1, -1, n_median, "median(xs)", st);
    vm.defineNative("variance", 1, -1, n_variance, "variance(xs) - sample variance", st);
    vm.defineNative("stdev", 1, -1, n_stdev, "stdev(xs) - sample standard deviation", st);
    vm.defineNative("linspace", 2, 3, n_linspace, "linspace(a, b, n) - n evenly spaced reals", st);
}
