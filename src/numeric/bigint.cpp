#include "numeric/bigint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "common/error.h"

using Mag = std::vector<uint32_t>;

namespace {

const size_t KARATSUBA_THRESHOLD = 32;
const uint64_t BASE = 1ULL << 32;

void trimMag(Mag& a) {
    while (!a.empty() && a.back() == 0) a.pop_back();
}

int cmpMag(const Mag& a, const Mag& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

Mag addMag(const Mag& a, const Mag& b) {
    const Mag& x = a.size() >= b.size() ? a : b;
    const Mag& y = a.size() >= b.size() ? b : a;
    Mag r(x.size() + 1);
    uint64_t carry = 0;
    for (size_t i = 0; i < x.size(); i++) {
        uint64_t s = static_cast<uint64_t>(x[i]) + (i < y.size() ? y[i] : 0) + carry;
        r[i] = static_cast<uint32_t>(s);
        carry = s >> 32;
    }
    r[x.size()] = static_cast<uint32_t>(carry);
    trimMag(r);
    return r;
}

// Requires a >= b.
Mag subMag(const Mag& a, const Mag& b) {
    Mag r(a.size());
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); i++) {
        int64_t d = static_cast<int64_t>(a[i]) - (i < b.size() ? b[i] : 0) - borrow;
        if (d < 0) {
            d += static_cast<int64_t>(BASE);
            borrow = 1;
        } else {
            borrow = 0;
        }
        r[i] = static_cast<uint32_t>(d);
    }
    trimMag(r);
    return r;
}

Mag mulSchool(const Mag& a, const Mag& b) {
    if (a.empty() || b.empty()) return {};
    Mag r(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); i++) {
        uint64_t ai = a[i];
        if (ai == 0) continue;
        uint64_t carry = 0;
        for (size_t j = 0; j < b.size(); j++) {
            uint64_t t = ai * b[j] + r[i + j] + carry;
            r[i + j] = static_cast<uint32_t>(t);
            carry = t >> 32;
        }
        size_t k = i + b.size();
        while (carry) {
            uint64_t t = static_cast<uint64_t>(r[k]) + carry;
            r[k] = static_cast<uint32_t>(t);
            carry = t >> 32;
            k++;
        }
    }
    trimMag(r);
    return r;
}

Mag shiftLimbs(const Mag& a, size_t n) {
    if (a.empty()) return a;
    Mag r(n, 0);
    r.insert(r.end(), a.begin(), a.end());
    return r;
}

Mag mulMag(const Mag& a, const Mag& b) {
    if (std::min(a.size(), b.size()) < KARATSUBA_THRESHOLD) return mulSchool(a, b);
    size_t m = std::max(a.size(), b.size()) / 2;
    auto lo = [m](const Mag& x) {
        Mag r(x.begin(), x.begin() + std::min(m, x.size()));
        trimMag(r);
        return r;
    };
    auto hi = [m](const Mag& x) {
        Mag r;
        if (x.size() > m) r.assign(x.begin() + m, x.end());
        trimMag(r);
        return r;
    };
    Mag a0 = lo(a), a1 = hi(a), b0 = lo(b), b1 = hi(b);
    Mag z0 = mulMag(a0, b0);
    Mag z2 = mulMag(a1, b1);
    Mag z1 = subMag(subMag(mulMag(addMag(a0, a1), addMag(b0, b1)), z0), z2);
    return addMag(addMag(z0, shiftLimbs(z1, m)), shiftLimbs(z2, 2 * m));
}

int clz32(uint32_t x) {
    if (x == 0) return 32;
    int n = 0;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

uint32_t divSmallInPlace(Mag& a, uint32_t d) {
    uint64_t rem = 0;
    for (size_t i = a.size(); i-- > 0;) {
        uint64_t cur = (rem << 32) | a[i];
        a[i] = static_cast<uint32_t>(cur / d);
        rem = cur % d;
    }
    trimMag(a);
    return static_cast<uint32_t>(rem);
}

void mulAddSmall(Mag& a, uint32_t m, uint32_t add) {
    uint64_t carry = add;
    for (auto& x : a) {
        uint64_t t = static_cast<uint64_t>(x) * m + carry;
        x = static_cast<uint32_t>(t);
        carry = t >> 32;
    }
    if (carry) a.push_back(static_cast<uint32_t>(carry));
}

// Knuth's Algorithm D (after Hacker's Delight, divmnu). v must be non-empty.
void divModMag(const Mag& u, const Mag& v, Mag& q, Mag& r) {
    if (cmpMag(u, v) < 0) {
        q.clear();
        r = u;
        return;
    }
    if (v.size() == 1) {
        q = u;
        uint32_t rem = divSmallInPlace(q, v[0]);
        r.clear();
        if (rem) r.push_back(rem);
        return;
    }
    size_t n = v.size();
    size_t m = u.size() - n;
    int s = clz32(v[n - 1]);
    Mag vn(n), un(u.size() + 1);
    for (size_t i = n - 1; i > 0; i--)
        vn[i] = (v[i] << s) | (s ? static_cast<uint32_t>(static_cast<uint64_t>(v[i - 1]) >> (32 - s)) : 0);
    vn[0] = v[0] << s;
    un[u.size()] = s ? static_cast<uint32_t>(static_cast<uint64_t>(u[u.size() - 1]) >> (32 - s)) : 0;
    for (size_t i = u.size() - 1; i > 0; i--)
        un[i] = (u[i] << s) | (s ? static_cast<uint32_t>(static_cast<uint64_t>(u[i - 1]) >> (32 - s)) : 0);
    un[0] = u[0] << s;

    q.assign(m + 1, 0);
    for (int64_t j = static_cast<int64_t>(m); j >= 0; j--) {
        uint64_t num = (static_cast<uint64_t>(un[j + n]) << 32) | un[j + n - 1];
        uint64_t qhat = num / vn[n - 1];
        uint64_t rhat = num % vn[n - 1];
        while (qhat >= BASE || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= BASE) break;
        }
        int64_t k = 0, t;
        for (size_t i = 0; i < n; i++) {
            uint64_t p = qhat * vn[i];
            t = static_cast<int64_t>(un[i + j]) - k - static_cast<int64_t>(p & 0xFFFFFFFFULL);
            un[i + j] = static_cast<uint32_t>(t);
            k = static_cast<int64_t>(p >> 32) - (t >> 32);
        }
        t = static_cast<int64_t>(un[j + n]) - k;
        un[j + n] = static_cast<uint32_t>(t);
        q[j] = static_cast<uint32_t>(qhat);
        if (t < 0) {
            q[j]--;
            k = 0;
            for (size_t i = 0; i < n; i++) {
                t = static_cast<int64_t>(un[i + j]) + vn[i] + k;
                un[i + j] = static_cast<uint32_t>(t);
                k = t >> 32;
            }
            un[j + n] = static_cast<uint32_t>(static_cast<int64_t>(un[j + n]) + k);
        }
    }
    trimMag(q);
    r.assign(n, 0);
    for (size_t i = 0; i < n; i++)
        r[i] = (un[i] >> s) | (s ? static_cast<uint32_t>(static_cast<uint64_t>(un[i + 1]) << (32 - s)) : 0);
    trimMag(r);
}

}  // namespace

BigInt::BigInt(int64_t v) {
    uint64_t u;
    if (v < 0) {
        neg_ = true;
        u = static_cast<uint64_t>(-(v + 1)) + 1;
    } else {
        u = static_cast<uint64_t>(v);
    }
    while (u) {
        mag_.push_back(static_cast<uint32_t>(u & 0xFFFFFFFFu));
        u >>= 32;
    }
}

BigInt BigInt::fromString(const std::string& s) {
    BigInt r;
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        neg = s[i] == '-';
        i++;
    }
    uint32_t chunk = 0;
    int chunkLen = 0;
    static const uint32_t pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c == '_') continue;
        if (c < '0' || c > '9') throw SedError("invalid integer literal: " + s);
        chunk = chunk * 10 + static_cast<uint32_t>(c - '0');
        if (++chunkLen == 9) {
            mulAddSmall(r.mag_, pow10[9], chunk);
            chunk = 0;
            chunkLen = 0;
        }
    }
    if (chunkLen) mulAddSmall(r.mag_, pow10[chunkLen], chunk);
    trimMag(r.mag_);
    r.neg_ = neg && !r.mag_.empty();
    return r;
}

BigInt BigInt::fromDouble(double d) {
    if (!std::isfinite(d)) throw SedError("cannot convert a non-finite number to an integer");
    d = std::trunc(d);
    if (std::fabs(d) < 9.0e18) return BigInt(static_cast<int64_t>(d));
    int e;
    double m = std::frexp(std::fabs(d), &e);  // |d| = m * 2^e, 0.5 <= m < 1
    int64_t mant = static_cast<int64_t>(std::ldexp(m, 53));
    BigInt r = BigInt(mant).shl(static_cast<size_t>(e - 53));
    return d < 0 ? -r : r;
}

bool BigInt::fitsInt64() const {
    if (mag_.size() > 2) return false;
    uint64_t u = 0;
    if (mag_.size() > 0) u = mag_[0];
    if (mag_.size() > 1) u |= static_cast<uint64_t>(mag_[1]) << 32;
    return neg_ ? u <= (1ULL << 63) : u <= static_cast<uint64_t>(INT64_MAX);
}

int64_t BigInt::toInt64() const {
    uint64_t u = 0;
    if (mag_.size() > 0) u = mag_[0];
    if (mag_.size() > 1) u |= static_cast<uint64_t>(mag_[1]) << 32;
    return neg_ ? static_cast<int64_t>(0 - u) : static_cast<int64_t>(u);
}

double BigInt::toDouble() const {
    double d = 0;
    for (size_t i = mag_.size(); i-- > 0;) d = d * 4294967296.0 + mag_[i];
    return neg_ ? -d : d;
}

std::string BigInt::toString() const {
    if (mag_.empty()) return "0";
    Mag cur = mag_;
    std::vector<uint32_t> parts;
    while (!cur.empty()) parts.push_back(divSmallInPlace(cur, 1000000000u));
    std::string s = neg_ ? "-" : "";
    s += std::to_string(parts.back());
    char buf[16];
    for (size_t i = parts.size() - 1; i-- > 0;) {
        std::snprintf(buf, sizeof buf, "%09u", parts[i]);
        s += buf;
    }
    return s;
}

size_t BigInt::bitLength() const {
    if (mag_.empty()) return 0;
    return (mag_.size() - 1) * 32 + (32 - clz32(mag_.back()));
}

size_t BigInt::hash() const {
    size_t h = neg_ ? 0x9e3779b97f4a7c15ULL : 0;
    for (uint32_t limb : mag_) h = (h * 1000003u) ^ limb;
    return h;
}

uint32_t BigInt::modSmall(uint32_t m) const {
    uint64_t rem = 0;
    for (size_t i = mag_.size(); i-- > 0;) rem = ((rem << 32) | mag_[i]) % m;
    return static_cast<uint32_t>(rem);
}

BigInt BigInt::abs() const {
    BigInt r = *this;
    r.neg_ = false;
    return r;
}

BigInt BigInt::operator-() const {
    BigInt r = *this;
    if (!r.mag_.empty()) r.neg_ = !r.neg_;
    return r;
}

BigInt BigInt::shl(size_t bits) const {
    if (mag_.empty()) return *this;
    size_t limbs = bits / 32, b = bits % 32;
    BigInt r;
    r.neg_ = neg_;
    r.mag_.assign(limbs, 0);
    uint32_t carry = 0;
    for (uint32_t x : mag_) {
        r.mag_.push_back(b ? ((x << b) | carry) : x);
        carry = b ? static_cast<uint32_t>(static_cast<uint64_t>(x) >> (32 - b)) : 0;
    }
    if (carry) r.mag_.push_back(carry);
    trimMag(r.mag_);
    return r;
}

BigInt BigInt::shr(size_t bits) const {
    size_t limbs = bits / 32, b = bits % 32;
    if (limbs >= mag_.size()) return BigInt();
    BigInt r;
    r.neg_ = neg_;
    r.mag_.assign(mag_.begin() + limbs, mag_.end());
    if (b) {
        for (size_t i = 0; i < r.mag_.size(); i++) {
            uint32_t next = i + 1 < r.mag_.size() ? r.mag_[i + 1] : 0;
            r.mag_[i] = (r.mag_[i] >> b) | static_cast<uint32_t>(static_cast<uint64_t>(next) << (32 - b));
        }
    }
    trimMag(r.mag_);
    if (r.mag_.empty()) r.neg_ = false;
    return r;
}

BigInt operator+(const BigInt& a, const BigInt& b) {
    BigInt r;
    if (a.neg_ == b.neg_) {
        r.mag_ = addMag(a.mag_, b.mag_);
        r.neg_ = a.neg_;
    } else {
        int c = cmpMag(a.mag_, b.mag_);
        if (c == 0) return BigInt();
        if (c > 0) {
            r.mag_ = subMag(a.mag_, b.mag_);
            r.neg_ = a.neg_;
        } else {
            r.mag_ = subMag(b.mag_, a.mag_);
            r.neg_ = b.neg_;
        }
    }
    if (r.mag_.empty()) r.neg_ = false;
    return r;
}

BigInt operator-(const BigInt& a, const BigInt& b) { return a + (-b); }

BigInt operator*(const BigInt& a, const BigInt& b) {
    BigInt r;
    r.mag_ = mulMag(a.mag_, b.mag_);
    r.neg_ = !r.mag_.empty() && (a.neg_ != b.neg_);
    return r;
}

void BigInt::divMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r) {
    if (b.isZero()) throw SedError("division by zero");
    BigInt qq, rr;
    divModMag(a.mag_, b.mag_, qq.mag_, rr.mag_);
    qq.neg_ = !qq.mag_.empty() && (a.neg_ != b.neg_);
    rr.neg_ = !rr.mag_.empty() && a.neg_;
    q = std::move(qq);
    r = std::move(rr);
}

BigInt operator/(const BigInt& a, const BigInt& b) {
    BigInt q, r;
    BigInt::divMod(a, b, q, r);
    return q;
}

BigInt operator%(const BigInt& a, const BigInt& b) {
    BigInt q, r;
    BigInt::divMod(a, b, q, r);
    return r;
}

int compare(const BigInt& a, const BigInt& b) {
    if (a.neg_ != b.neg_) return a.neg_ ? -1 : 1;
    int c = cmpMag(a.mag_, b.mag_);
    return a.neg_ ? -c : c;
}

BigInt BigInt::pow(const BigInt& base, uint64_t exp) {
    BigInt result(1), b = base;
    while (exp) {
        if (exp & 1) result = result * b;
        exp >>= 1;
        if (exp) b = b * b;
    }
    return result;
}

BigInt BigInt::gcd(BigInt a, BigInt b) {
    a = a.abs();
    b = b.abs();
    while (!b.isZero()) {
        BigInt r = a % b;
        a = std::move(b);
        b = std::move(r);
    }
    return a;
}

BigInt BigInt::isqrt(const BigInt& a) {
    if (a.isNeg()) throw SedError("square root of a negative integer");
    if (a.isZero()) return BigInt();
    BigInt x = BigInt(1).shl((a.bitLength() + 1) / 2);
    for (;;) {
        BigInt y = (x + a / x).shr(1);
        if (y >= x) return x;
        x = std::move(y);
    }
}

BigInt BigInt::iroot(const BigInt& a, unsigned n) {
    if (n == 0) throw SedError("zeroth root");
    if (n == 1) return a;
    if (a.isNeg()) throw SedError("even root of a negative integer");
    if (a.isZero()) return BigInt();
    if (n == 2) return isqrt(a);
    BigInt x = BigInt(1).shl(a.bitLength() / n + 1);
    BigInt nn(static_cast<int64_t>(n)), n1(static_cast<int64_t>(n - 1));
    for (;;) {
        BigInt y = (n1 * x + a / BigInt::pow(x, n - 1)) / nn;
        if (y >= x) return x;
        x = std::move(y);
    }
}
