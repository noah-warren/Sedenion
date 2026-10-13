#pragma once

#include <string>

#include "numeric/bigint.h"

// Exact rational number num/den, always normalized: den > 0 and gcd(num, den) == 1.
struct Rational {
    BigInt num{0};
    BigInt den{1};

    Rational() = default;
    Rational(int64_t n) : num(n), den(1) {}
    Rational(BigInt n) : num(std::move(n)), den(1) {}
    Rational(BigInt n, BigInt d);

    static Rational raw(BigInt n, BigInt d);  // caller guarantees normalization
    static bool fromDouble(double d, Rational& out);  // exact binary value of d

    bool isInteger() const { return den.isOne(); }
    bool isZero() const { return num.isZero(); }
    bool isOne() const { return num.isOne() && den.isOne(); }
    int sign() const { return num.sign(); }

    double toDouble() const;
    std::string toString() const;
    size_t hash() const { return num.hash() * 31 + den.hash(); }

    Rational operator-() const { return raw(-num, den); }
    Rational abs() const { return raw(num.abs(), den); }
    BigInt floor() const;
    BigInt ceil() const;

    friend Rational operator+(const Rational& a, const Rational& b);
    friend Rational operator-(const Rational& a, const Rational& b);
    friend Rational operator*(const Rational& a, const Rational& b);
    friend Rational operator/(const Rational& a, const Rational& b);
    friend int compare(const Rational& a, const Rational& b);
    friend bool operator==(const Rational& a, const Rational& b) { return a.num == b.num && a.den == b.den; }
    friend bool operator!=(const Rational& a, const Rational& b) { return !(a == b); }
    friend bool operator<(const Rational& a, const Rational& b) { return compare(a, b) < 0; }
    friend bool operator>(const Rational& a, const Rational& b) { return compare(a, b) > 0; }
    friend bool operator<=(const Rational& a, const Rational& b) { return compare(a, b) <= 0; }
    friend bool operator>=(const Rational& a, const Rational& b) { return compare(a, b) >= 0; }

    static Rational pow(const Rational& b, int64_t e);
};
