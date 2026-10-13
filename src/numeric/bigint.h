#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Arbitrary-precision signed integer: sign + magnitude in base 2^32, little-endian limbs.
// Zero is represented by an empty magnitude with neg_ == false.
class BigInt {
public:
    BigInt() = default;
    BigInt(int64_t v);

    static BigInt fromString(const std::string& s);
    static BigInt fromDouble(double d);  // truncates toward zero

    bool isZero() const { return mag_.empty(); }
    bool isNeg() const { return neg_; }
    bool isOne() const { return !neg_ && mag_.size() == 1 && mag_[0] == 1; }
    bool isEven() const { return mag_.empty() || (mag_[0] & 1u) == 0; }
    int sign() const { return mag_.empty() ? 0 : (neg_ ? -1 : 1); }

    bool fitsInt64() const;
    int64_t toInt64() const;
    double toDouble() const;
    std::string toString() const;
    size_t bitLength() const;
    size_t hash() const;
    uint32_t modSmall(uint32_t m) const;  // |this| mod m

    BigInt abs() const;
    BigInt operator-() const;
    BigInt shl(size_t bits) const;
    BigInt shr(size_t bits) const;

    friend BigInt operator+(const BigInt& a, const BigInt& b);
    friend BigInt operator-(const BigInt& a, const BigInt& b);
    friend BigInt operator*(const BigInt& a, const BigInt& b);
    friend BigInt operator/(const BigInt& a, const BigInt& b);
    friend BigInt operator%(const BigInt& a, const BigInt& b);
    friend int compare(const BigInt& a, const BigInt& b);
    friend bool operator==(const BigInt& a, const BigInt& b) { return a.neg_ == b.neg_ && a.mag_ == b.mag_; }
    friend bool operator!=(const BigInt& a, const BigInt& b) { return !(a == b); }
    friend bool operator<(const BigInt& a, const BigInt& b) { return compare(a, b) < 0; }
    friend bool operator<=(const BigInt& a, const BigInt& b) { return compare(a, b) <= 0; }
    friend bool operator>(const BigInt& a, const BigInt& b) { return compare(a, b) > 0; }
    friend bool operator>=(const BigInt& a, const BigInt& b) { return compare(a, b) >= 0; }

    // Truncated division: q = trunc(a / b), r = a - q*b (r has the sign of a).
    static void divMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);
    static BigInt pow(const BigInt& base, uint64_t exp);
    static BigInt gcd(BigInt a, BigInt b);
    static BigInt isqrt(const BigInt& a);              // floor(sqrt(a)), a >= 0
    static BigInt iroot(const BigInt& a, unsigned n);  // floor(a^(1/n)), a >= 0

private:
    bool neg_ = false;
    std::vector<uint32_t> mag_;
};
