#include "numeric/rational.h"

#include <cmath>

#include "common/error.h"

Rational::Rational(BigInt n, BigInt d) : num(std::move(n)), den(std::move(d)) {
    if (den.isZero()) throw SedError("division by zero");
    if (den.isNeg()) {
        num = -num;
        den = -den;
    }
    if (num.isZero()) {
        den = BigInt(1);
        return;
    }
    BigInt g = BigInt::gcd(num, den);
    if (!g.isOne()) {
        num = num / g;
        den = den / g;
    }
}

Rational Rational::raw(BigInt n, BigInt d) {
    Rational r;
    r.num = std::move(n);
    r.den = std::move(d);
    return r;
}

bool Rational::fromDouble(double d, Rational& out) {
    if (!std::isfinite(d)) return false;
    if (d == 0) {
        out = Rational(0);
        return true;
    }
    int e;
    double m = std::frexp(d, &e);  // d = m * 2^e
    int64_t mant = static_cast<int64_t>(std::ldexp(m, 53));
    e -= 53;
    BigInt n(mant);
    if (e >= 0) {
        out = Rational(n.shl(static_cast<size_t>(e)));
    } else {
        out = Rational(n, BigInt(1).shl(static_cast<size_t>(-e)));
    }
    return true;
}

double Rational::toDouble() const {
    if (den.isOne()) return num.toDouble();
    size_t nb = num.bitLength(), db = den.bitLength();
    if (nb < 1000 && db < 1000) return num.toDouble() / den.toDouble();
    // Scale so the integer quotient carries ~64 significant bits.
    long long k = 64 - (static_cast<long long>(nb) - static_cast<long long>(db));
    BigInt q = k >= 0 ? num.shl(static_cast<size_t>(k)) / den : num / den.shl(static_cast<size_t>(-k));
    return std::ldexp(q.toDouble(), static_cast<int>(-k));
}

std::string Rational::toString() const {
    if (den.isOne()) return num.toString();
    return num.toString() + "/" + den.toString();
}

BigInt Rational::floor() const {
    BigInt q, r;
    BigInt::divMod(num, den, q, r);
    if (r.isNeg()) q = q - BigInt(1);
    return q;
}

BigInt Rational::ceil() const { return -((-*this).floor()); }

Rational operator+(const Rational& a, const Rational& b) {
    if (a.den.isOne() && b.den.isOne()) return Rational::raw(a.num + b.num, BigInt(1));
    return Rational(a.num * b.den + b.num * a.den, a.den * b.den);
}

Rational operator-(const Rational& a, const Rational& b) { return a + (-b); }

Rational operator*(const Rational& a, const Rational& b) {
    if (a.den.isOne() && b.den.isOne()) return Rational::raw(a.num * b.num, BigInt(1));
    return Rational(a.num * b.num, a.den * b.den);
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.isZero()) throw SedError("division by zero");
    return Rational(a.num * b.den, a.den * b.num);
}

int compare(const Rational& a, const Rational& b) {
    if (a.den == b.den) return compare(a.num, b.num);
    return compare(a.num * b.den, b.num * a.den);
}

Rational Rational::pow(const Rational& b, int64_t e) {
    if (e == 0) return Rational(1);
    if (e < 0) {
        if (b.isZero()) throw SedError("division by zero");
        uint64_t m = static_cast<uint64_t>(-(e + 1)) + 1;
        BigInt n = BigInt::pow(b.den, m), d = BigInt::pow(b.num, m);
        if (d.isNeg()) {
            n = -n;
            d = -d;
        }
        return raw(n, d);
    }
    return raw(BigInt::pow(b.num, static_cast<uint64_t>(e)), BigInt::pow(b.den, static_cast<uint64_t>(e)));
}
