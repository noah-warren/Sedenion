#include "numeric/ops.h"

#include <cmath>
#include <cstdio>
#include <limits>

#include "common/error.h"
#include "numeric/radical.h"
#include "vm/object.h"

NumKind numKind(const Value& v) {
    switch (v.type) {
        case ValType::Int: return NumKind::Int;
        case ValType::Real: return NumKind::Real;
        case ValType::Obj:
            switch (v.as.o->type) {
                case ObjType::BigInt: return NumKind::Big;
                case ObjType::Rational: return NumKind::Rat;
                case ObjType::Complex: return NumKind::Cplx;
                default: return NumKind::None;
            }
        default: return NumKind::None;
    }
}

bool isIntegerValue(const Value& v) {
    switch (numKind(v)) {
        case NumKind::Int:
        case NumKind::Big: return true;
        case NumKind::Real: return std::isfinite(v.as.d) && std::floor(v.as.d) == v.as.d;
        default: return false;
    }
}

BigInt toBigInt(const Value& v) {
    switch (numKind(v)) {
        case NumKind::Int: return BigInt(v.as.i);
        case NumKind::Big: return as<ObjBigInt>(v)->v;
        case NumKind::Rat: return as<ObjRational>(v)->v.num / as<ObjRational>(v)->v.den;
        case NumKind::Real: return BigInt::fromDouble(v.as.d);
        default: fail("expected an integer");
    }
}

Rational toRational(const Value& v) {
    switch (numKind(v)) {
        case NumKind::Int: return Rational(v.as.i);
        case NumKind::Big: return Rational(as<ObjBigInt>(v)->v);
        case NumKind::Rat: return as<ObjRational>(v)->v;
        case NumKind::Real: {
            Rational r;
            if (!Rational::fromDouble(v.as.d, r)) fail("cannot convert a non-finite number to a rational");
            return r;
        }
        default: fail("expected a real number");
    }
}

double toDouble(const Value& v) {
    if (isRadical(v)) return radicalToDouble(v);
    switch (numKind(v)) {
        case NumKind::Int: return static_cast<double>(v.as.i);
        case NumKind::Big: return as<ObjBigInt>(v)->v.toDouble();
        case NumKind::Rat: return as<ObjRational>(v)->v.toDouble();
        case NumKind::Real: return v.as.d;
        case NumKind::Cplx: fail("expected a real number, got a complex number");
        default: fail("expected a number");
    }
}

std::complex<double> toComplex(const Value& v) {
    if (numKind(v) == NumKind::Cplx) return as<ObjComplex>(v)->v;
    return {toDouble(v), 0.0};
}

int64_t toInt64(const Value& v, const char* what) {
    if (v.isInt()) return v.as.i;
    if (isIntegerValue(v)) {
        BigInt b = toBigInt(v);
        if (b.fitsInt64()) return b.toInt64();
    }
    fail(std::string("expected ") + what);
}

Value fromBigInt(BigInt b) {
    if (b.fitsInt64()) return Value::integer(b.toInt64());
    return Value::object(Heap::instance().make<ObjBigInt>(std::move(b)));
}

Value fromRational(Rational r) {
    if (r.isInteger()) return fromBigInt(std::move(r.num));
    return Value::object(Heap::instance().make<ObjRational>(std::move(r)));
}

Value fromComplex(std::complex<double> c) {
    if (c.imag() == 0.0) return Value::real(c.real());
    return Value::object(Heap::instance().make<ObjComplex>(c));
}

namespace {

bool addOverflow(int64_t a, int64_t b, int64_t& r) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return true;
    r = a + b;
    return false;
}

bool subOverflow(int64_t a, int64_t b, int64_t& r) {
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b)) return true;
    r = a - b;
    return false;
}

bool mulOverflow(int64_t a, int64_t b, int64_t& r) {
    const int64_t L = 3037000499LL;
    if (a > -L && a < L && b > -L && b < L) {
        r = a * b;
        return false;
    }
    if (a == 0 || b == 0) {
        r = 0;
        return false;
    }
    if (a > 0) {
        if (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a) return true;
    } else {
        if (b > 0 ? a < INT64_MIN / b : (a != 0 && b < INT64_MAX / a)) return true;
    }
    r = a * b;
    return false;
}

NumKind maxKind(const Value& a, const Value& b) {
    NumKind ka = numKind(a), kb = numKind(b);
    if (ka == NumKind::None || kb == NumKind::None) fail("expected numbers");
    return ka > kb ? ka : kb;
}

std::complex<double> complexPowInt(std::complex<double> base, int64_t e) {
    bool neg = e < 0;
    uint64_t m = neg ? static_cast<uint64_t>(-(e + 1)) + 1 : static_cast<uint64_t>(e);
    std::complex<double> result(1.0, 0.0);
    while (m) {
        if (m & 1) result *= base;
        m >>= 1;
        if (m) base *= base;
    }
    return neg ? 1.0 / result : result;
}

Value powRationalInt(const Rational& base, const BigInt& e) {
    if (base.isZero()) {
        if (e.isNeg()) fail("division by zero");
        return Value::integer(e.isZero() ? 1 : 0);
    }
    if (base.isOne()) return Value::integer(1);
    if (base.isInteger() && base.num == BigInt(-1)) return Value::integer(e.isEven() ? 1 : -1);
    if (!e.fitsInt64()) fail("exponent too large");
    int64_t ee = e.toInt64();
    double mag = std::fabs(static_cast<double>(ee));
    double bits = static_cast<double>(base.num.bitLength() + base.den.bitLength()) * mag;
    if (bits > 3.2e7) fail("result too large (about " + std::to_string(static_cast<long long>(bits * 0.30103)) + " digits)");
    return fromRational(Rational::pow(base, ee));
}

}  // namespace

Value numAdd(const Value& a, const Value& b) {
    switch (maxKind(a, b)) {
        case NumKind::Int: {
            int64_t r;
            if (!addOverflow(a.as.i, b.as.i, r)) return Value::integer(r);
            return fromBigInt(BigInt(a.as.i) + BigInt(b.as.i));
        }
        case NumKind::Big: return fromBigInt(toBigInt(a) + toBigInt(b));
        case NumKind::Rat: return fromRational(toRational(a) + toRational(b));
        case NumKind::Real: return Value::real(toDouble(a) + toDouble(b));
        default: return fromComplex(toComplex(a) + toComplex(b));
    }
}

Value numSub(const Value& a, const Value& b) {
    switch (maxKind(a, b)) {
        case NumKind::Int: {
            int64_t r;
            if (!subOverflow(a.as.i, b.as.i, r)) return Value::integer(r);
            return fromBigInt(BigInt(a.as.i) - BigInt(b.as.i));
        }
        case NumKind::Big: return fromBigInt(toBigInt(a) - toBigInt(b));
        case NumKind::Rat: return fromRational(toRational(a) - toRational(b));
        case NumKind::Real: return Value::real(toDouble(a) - toDouble(b));
        default: return fromComplex(toComplex(a) - toComplex(b));
    }
}

Value numMul(const Value& a, const Value& b) {
    switch (maxKind(a, b)) {
        case NumKind::Int: {
            int64_t r;
            if (!mulOverflow(a.as.i, b.as.i, r)) return Value::integer(r);
            return fromBigInt(BigInt(a.as.i) * BigInt(b.as.i));
        }
        case NumKind::Big: return fromBigInt(toBigInt(a) * toBigInt(b));
        case NumKind::Rat: return fromRational(toRational(a) * toRational(b));
        case NumKind::Real: return Value::real(toDouble(a) * toDouble(b));
        default: return fromComplex(toComplex(a) * toComplex(b));
    }
}

Value numDiv(const Value& a, const Value& b) {
    NumKind k = maxKind(a, b);
    if (k <= NumKind::Rat) {
        if (numIsZero(b)) fail("division by zero");
        if (k == NumKind::Int && b.as.i != -1 && a.as.i % b.as.i == 0) return Value::integer(a.as.i / b.as.i);
        return fromRational(toRational(a) / toRational(b));
    }
    if (k == NumKind::Real) return Value::real(toDouble(a) / toDouble(b));
    return fromComplex(toComplex(a) / toComplex(b));
}

Value numFloorDiv(const Value& a, const Value& b) {
    NumKind k = maxKind(a, b);
    if (k == NumKind::Cplx) fail("floor division is not defined for complex numbers");
    if (k <= NumKind::Rat) {
        if (numIsZero(b)) fail("division by zero");
        return fromBigInt((toRational(a) / toRational(b)).floor());
    }
    return Value::real(std::floor(toDouble(a) / toDouble(b)));
}

Value numMod(const Value& a, const Value& b) {
    NumKind k = maxKind(a, b);
    if (k == NumKind::Cplx) fail("modulo is not defined for complex numbers");
    if (k <= NumKind::Rat) {
        if (numIsZero(b)) fail("modulo by zero");
        if (k == NumKind::Int) {
            if (b.as.i == -1) return Value::integer(0);
            int64_t r = a.as.i % b.as.i;
            if (r != 0 && ((r < 0) != (b.as.i < 0))) r += b.as.i;
            return Value::integer(r);
        }
        Rational x = toRational(a), y = toRational(b);
        return fromRational(x - y * Rational((x / y).floor()));
    }
    double x = toDouble(a), y = toDouble(b);
    double r = std::fmod(x, y);
    if (r != 0 && ((r < 0) != (y < 0))) r += y;
    return Value::real(r);
}

Value numNeg(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int:
            if (a.as.i == INT64_MIN) return fromBigInt(-BigInt(a.as.i));
            return Value::integer(-a.as.i);
        case NumKind::Big: return fromBigInt(-as<ObjBigInt>(a)->v);
        case NumKind::Rat: return fromRational(-as<ObjRational>(a)->v);
        case NumKind::Real: return Value::real(-a.as.d);
        case NumKind::Cplx: return fromComplex(-as<ObjComplex>(a)->v);
        default: fail("expected a number");
    }
}

Value numPow(const Value& a, const Value& b) {
    NumKind ka = numKind(a), kb = numKind(b);
    if (ka == NumKind::None || kb == NumKind::None) fail("expected numbers");

    if (kb == NumKind::Int || kb == NumKind::Big) {
        if (ka <= NumKind::Rat) return powRationalInt(toRational(a), toBigInt(b));
        if (ka == NumKind::Cplx) {
            if (kb == NumKind::Int) return fromComplex(complexPowInt(as<ObjComplex>(a)->v, b.as.i));
            return fromComplex(std::pow(as<ObjComplex>(a)->v, toDouble(b)));
        }
        return Value::real(std::pow(toDouble(a), toDouble(b)));
    }

    if (kb == NumKind::Rat && ka <= NumKind::Rat) {
        Rational ex = as<ObjRational>(b)->v;
        Rational base = toRational(a);
        if (ex.den.fitsInt64() && ex.den.toInt64() <= 64) {
            unsigned q = static_cast<unsigned>(ex.den.toInt64());
            bool negBase = base.sign() < 0;
            if (!negBase || (q % 2 == 1)) {
                Rational ab = base.abs();
                BigInt rn = BigInt::iroot(ab.num, q), rd = BigInt::iroot(ab.den, q);
                if (BigInt::pow(rn, q) == ab.num && BigInt::pow(rd, q) == ab.den) {
                    Rational root(rn, rd);
                    if (negBase) root = -root;
                    return powRationalInt(root, ex.num);
                }
            }
        }
        double x = base.toDouble(), y = ex.toDouble();
        if (x < 0) {
            if (!ex.den.isEven()) {
                double r = std::pow(-x, y);
                return Value::real(ex.num.isEven() ? r : -r);
            }
            return fromComplex(std::pow(std::complex<double>(x, 0.0), std::complex<double>(y, 0.0)));
        }
        return Value::real(std::pow(x, y));
    }

    if (ka == NumKind::Cplx || kb == NumKind::Cplx) return fromComplex(std::pow(toComplex(a), toComplex(b)));
    double x = toDouble(a), y = toDouble(b);
    if (x < 0 && std::floor(y) != y)
        return fromComplex(std::pow(std::complex<double>(x, 0.0), std::complex<double>(y, 0.0)));
    return Value::real(std::pow(x, y));
}

int numCompare(const Value& a, const Value& b) {
    NumKind k = maxKind(a, b);
    if (k == NumKind::Cplx) fail("complex numbers cannot be ordered");
    if (k == NumKind::Int) return a.as.i < b.as.i ? -1 : (a.as.i > b.as.i ? 1 : 0);
    if (k == NumKind::Real) {
        double x = toDouble(a), y = toDouble(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (k == NumKind::Big) return compare(toBigInt(a), toBigInt(b));
    return compare(toRational(a), toRational(b));
}

bool numEqual(const Value& a, const Value& b) {
    NumKind k = maxKind(a, b);
    if (k == NumKind::Cplx) return toComplex(a) == toComplex(b);
    if (k == NumKind::Real) return toDouble(a) == toDouble(b);
    return numCompare(a, b) == 0;
}

bool numIsZero(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int: return a.as.i == 0;
        case NumKind::Real: return a.as.d == 0.0;
        case NumKind::Cplx: return as<ObjComplex>(a)->v == std::complex<double>(0, 0);
        default: return false;  // BigInt and Rational are never zero once normalized
    }
}

bool numIsOne(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int: return a.as.i == 1;
        case NumKind::Real: return a.as.d == 1.0;
        default: return false;
    }
}

int numSign(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int: return a.as.i < 0 ? -1 : (a.as.i > 0 ? 1 : 0);
        case NumKind::Big: return as<ObjBigInt>(a)->v.sign();
        case NumKind::Rat: return as<ObjRational>(a)->v.sign();
        case NumKind::Real: return a.as.d < 0 ? -1 : (a.as.d > 0 ? 1 : 0);
        default: fail("sign of a complex number is undefined");
    }
}

Value numAbs(const Value& a) {
    if (numKind(a) == NumKind::Cplx) return Value::real(std::abs(as<ObjComplex>(a)->v));
    return numSign(a) < 0 ? numNeg(a) : a;
}

Value numFloor(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int:
        case NumKind::Big: return a;
        case NumKind::Rat: return fromBigInt(as<ObjRational>(a)->v.floor());
        case NumKind::Real:
            if (!std::isfinite(a.as.d)) return a;
            return fromBigInt(BigInt::fromDouble(std::floor(a.as.d)));
        default: fail("floor of a complex number is undefined");
    }
}

Value numCeil(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int:
        case NumKind::Big: return a;
        case NumKind::Rat: return fromBigInt(as<ObjRational>(a)->v.ceil());
        case NumKind::Real:
            if (!std::isfinite(a.as.d)) return a;
            return fromBigInt(BigInt::fromDouble(std::ceil(a.as.d)));
        default: fail("ceil of a complex number is undefined");
    }
}

Value numRound(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int:
        case NumKind::Big: return a;
        case NumKind::Rat: {
            const Rational& r = as<ObjRational>(a)->v;
            BigInt f = (r.abs() + Rational(BigInt(1), BigInt(2))).floor();
            return fromBigInt(r.sign() < 0 ? -f : f);
        }
        case NumKind::Real:
            if (!std::isfinite(a.as.d)) return a;
            return fromBigInt(BigInt::fromDouble(std::round(a.as.d)));
        default: fail("round of a complex number is undefined");
    }
}

Value numTrunc(const Value& a) {
    switch (numKind(a)) {
        case NumKind::Int:
        case NumKind::Big: return a;
        case NumKind::Rat: return fromBigInt(as<ObjRational>(a)->v.num / as<ObjRational>(a)->v.den);
        case NumKind::Real:
            if (!std::isfinite(a.as.d)) return a;
            return fromBigInt(BigInt::fromDouble(a.as.d));
        default: fail("trunc of a complex number is undefined");
    }
}

Value numSqrt(const Value& a) {
    NumKind k = numKind(a);
    if (k <= NumKind::Rat) {
        Rational r = toRational(a);
        Rational ab = r.abs();
        BigInt rn = BigInt::isqrt(ab.num), rd = BigInt::isqrt(ab.den);
        bool perfect = rn * rn == ab.num && rd * rd == ab.den;
        if (r.sign() >= 0) return sqrtExact(r);
        double im = perfect ? Rational(rn, rd).toDouble() : std::sqrt(ab.toDouble());
        return fromComplex({0.0, im});
    }
    if (k == NumKind::Real) {
        if (a.as.d >= 0) return Value::real(std::sqrt(a.as.d));
        return fromComplex({0.0, std::sqrt(-a.as.d)});
    }
    return fromComplex(std::sqrt(toComplex(a)));
}

Value numCbrt(const Value& a) {
    NumKind k = numKind(a);
    if (k <= NumKind::Rat) {
        Rational r = toRational(a);
        Rational ab = r.abs();
        BigInt rn = BigInt::iroot(ab.num, 3), rd = BigInt::iroot(ab.den, 3);
        if (BigInt::pow(rn, 3) == ab.num && BigInt::pow(rd, 3) == ab.den) {
            Rational root(rn, rd);
            return fromRational(r.sign() < 0 ? -root : root);
        }
        return Value::real(std::cbrt(r.toDouble()));
    }
    if (k == NumKind::Real) return Value::real(std::cbrt(a.as.d));
    return fromComplex(std::pow(toComplex(a), 1.0 / 3.0));
}

namespace {
BigInt productRange(int64_t lo, int64_t hi) {
    if (lo > hi) return BigInt(1);
    if (hi - lo < 16) {
        BigInt r(lo);
        for (int64_t k = lo + 1; k <= hi; k++) r = r * BigInt(k);
        return r;
    }
    int64_t mid = lo + (hi - lo) / 2;
    return productRange(lo, mid) * productRange(mid + 1, hi);
}
}  // namespace

Value numFactorial(const Value& a) {
    NumKind k = numKind(a);
    if (k == NumKind::Int || k == NumKind::Big) {
        if (numSign(a) < 0) fail("factorial of a negative integer");
        if (k == NumKind::Big || a.as.i > 100000) fail("factorial argument too large");
        return fromBigInt(productRange(1, a.as.i));
    }
    if (k == NumKind::Real || k == NumKind::Rat) return Value::real(std::tgamma(toDouble(a) + 1.0));
    fail("factorial of a complex number is not supported");
}

std::string formatDouble(double d, bool forceDecimal) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.15g", d);
    std::string s = buf;
    if (forceDecimal && s.find_first_of(".e") == std::string::npos) s += ".0";
    return s;
}

std::string formatComplex(std::complex<double> c) {
    double re = c.real(), im = c.imag();
    double aim = std::fabs(im);
    std::string mag = aim == 1.0 ? "" : formatDouble(aim, false);
    if (re == 0.0) return (im < 0 ? "-" : "") + mag + "i";
    return formatDouble(re, false) + (im < 0 ? " - " : " + ") + mag + "i";
}
