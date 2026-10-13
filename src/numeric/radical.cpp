#include "numeric/radical.h"

#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "numeric/ops.h"

#include "cas/bridge.h"
#include "cas/expr.h"

namespace {

using TermMap = std::map<BigInt, Rational>;

void extractSquare(BigInt n, BigInt& square, BigInt& rest) {
    square = BigInt(1);
    rest = BigInt(1);
    if (n.isZero() || n.isNeg()) return;
    if (n.fitsInt64()) {
        uint64_t x = static_cast<uint64_t>(n.toInt64());
        uint64_t sq = 1, rs = 1;
        int e = 0;
        while ((x & 1ull) == 0 && x != 0) {
            x >>= 1;
            e++;
        }
        sq <<= static_cast<unsigned>(e / 2);
        if (e % 2) rs *= 2;
        for (uint64_t p = 3; p <= x / p; p += 2) {
            e = 0;
            while (x % p == 0) {
                x /= p;
                e++;
            }
            for (int i = 0; i < e / 2; i++) sq *= p;
            if (e % 2) rs *= p;
        }
        if (x > 1) rs *= x;
        square = BigInt(static_cast<int64_t>(sq));
        rest = BigInt(static_cast<int64_t>(rs));
        return;
    }
    BigInt p(2);
    while (p * p <= n) {
        int e = 0;
        while ((n % p).isZero()) {
            n = n / p;
            e++;
        }
        if (e) {
            square = square * BigInt::pow(p, static_cast<uint64_t>(e / 2));
            if (e % 2) rest = rest * p;
        }
        p = p.isEven() ? BigInt(3) : p + 2;
    }
    if (!n.isZero() && !n.isOne()) {
        BigInt s = BigInt::isqrt(n);
        if (s * s == n) square = square * s;
        else rest = rest * n;
    }
}

void addTerm(TermMap& m, const BigInt& rad, const Rational& coeff) {
    if (coeff.isZero()) return;
    auto it = m.find(rad);
    if (it == m.end()) {
        m.emplace(rad, coeff);
        return;
    }
    it->second = it->second + coeff;
    if (it->second.isZero()) m.erase(it);
}

TermMap mulTerm(const Rational& c1, const BigInt& r1, const Rational& c2, const BigInt& r2) {
    BigInt square, rest;
    extractSquare(r1 * r2, square, rest);
    TermMap m;
    addTerm(m, rest, c1 * c2 * Rational(square));
    return m;
}

TermMap fromValue(const Value& v) {
    TermMap m;
    if (isRadical(v)) {
        for (const auto& t : as<ObjRadical>(v)->terms) addTerm(m, t.first, t.second);
        return m;
    }
    if (isExactNumber(v)) addTerm(m, BigInt(1), toRational(v));
    return m;
}

Value pack(TermMap m) {
    if (m.empty()) return Value::integer(0);
    if (m.size() == 1 && m.begin()->first.isOne()) return fromRational(m.begin()->second);
    std::vector<std::pair<BigInt, Rational>> terms;
    terms.reserve(m.size());
    for (auto& t : m) terms.push_back(std::move(t));
    return Value::object(Heap::instance().make<ObjRadical>(std::move(terms)));
}

TermMap addMaps(const TermMap& a, const TermMap& b) {
    TermMap m = a;
    for (const auto& t : b) addTerm(m, t.first, t.second);
    return m;
}

TermMap negMap(TermMap m) {
    for (auto& t : m) t.second = -t.second;
    return m;
}

TermMap mulMaps(const TermMap& a, const TermMap& b) {
    TermMap m;
    for (const auto& x : a)
        for (const auto& y : b) {
            TermMap p = mulTerm(x.second, x.first, y.second, y.first);
            m = addMaps(m, p);
        }
    return m;
}

std::vector<BigInt> primeFactors(const BigInt& rad) {
    std::vector<BigInt> out;
    if (rad.isOne() || rad.isZero()) return out;
    if (rad.fitsInt64()) {
        uint64_t x = static_cast<uint64_t>(rad.toInt64());
        if (x % 2 == 0) {
            out.push_back(BigInt(2));
            while (x % 2 == 0) x /= 2;
        }
        for (uint64_t p = 3; p <= x / p; p += 2) {
            if (x % p == 0) {
                out.push_back(BigInt(static_cast<int64_t>(p)));
                while (x % p == 0) x /= p;
            }
        }
        if (x > 1) out.push_back(BigInt(static_cast<int64_t>(x)));
        return out;
    }
    BigInt n = rad;
    BigInt p(2);
    while (p * p <= n) {
        if ((n % p).isZero()) {
            out.push_back(p);
            while ((n % p).isZero()) n = n / p;
        }
        p = p.isEven() ? BigInt(3) : p + 2;
    }
    if (!n.isOne()) out.push_back(n);
    return out;
}

cas::E exprOf(const TermMap& m) {
    cas::E sum = cas::zero();
    for (const auto& t : m) {
        cas::E term = cas::num(t.second);
        if (!t.first.isOne()) term = cas::mul(term, cas::sqrt(cas::num(Rational(t.first))));
        sum = cas::add(sum, term);
    }
    return sum;
}

// Invert an element of a multiquadratic field by solving a linear system over Q.
bool invert(const TermMap& src, TermMap& out) {
    if (src.empty()) return false;
    if (src.size() == 1) {
        const BigInt& rad = src.begin()->first;
        Rational c = src.begin()->second;
        if (c.isZero()) return false;
        // 1/(c*sqrt(n)) = sqrt(n)/(c*n)
        out.clear();
        addTerm(out, rad, Rational(1) / (c * Rational(rad)));
        return true;
    }
    std::set<BigInt> primeSet;
    for (const auto& t : src)
        for (const BigInt& p : primeFactors(t.first)) primeSet.insert(p);
    if (primeSet.size() > 8) return false;
    std::vector<BigInt> primes(primeSet.begin(), primeSet.end());
    int k = static_cast<int>(primes.size());
    int n = 1 << k;
    std::vector<Rational> v(static_cast<size_t>(n));
    for (const auto& t : src) {
        int mask = 0;
        BigInt left = t.first;
        for (int i = 0; i < k; i++) {
            if ((left % primes[static_cast<size_t>(i)]).isZero()) {
                mask |= 1 << i;
                left = left / primes[static_cast<size_t>(i)];
            }
        }
        v[static_cast<size_t>(mask)] = v[static_cast<size_t>(mask)] + t.second;
    }
    std::vector<std::vector<Rational>> M(static_cast<size_t>(n), std::vector<Rational>(static_cast<size_t>(n)));
    std::vector<Rational> b(static_cast<size_t>(n));
    b[0] = Rational(1);
    for (int col = 0; col < n; col++) {
        for (int i = 0; i < n; i++) {
            if (v[static_cast<size_t>(i)].isZero()) continue;
            int mask = i ^ col;
            Rational coeff = v[static_cast<size_t>(i)];
            int both = i & col;
            for (int bit = 0; bit < k; bit++)
                if (both & (1 << bit)) coeff = coeff * Rational(primes[static_cast<size_t>(bit)]);
            M[static_cast<size_t>(mask)][static_cast<size_t>(col)] =
                M[static_cast<size_t>(mask)][static_cast<size_t>(col)] + coeff;
        }
    }
    for (int col = 0; col < n; col++) {
        int piv = col;
        while (piv < n && M[static_cast<size_t>(piv)][static_cast<size_t>(col)].isZero()) piv++;
        if (piv == n) return false;
        if (piv != col) {
            std::swap(M[static_cast<size_t>(piv)], M[static_cast<size_t>(col)]);
            std::swap(b[static_cast<size_t>(piv)], b[static_cast<size_t>(col)]);
        }
        Rational inv = Rational(1) / M[static_cast<size_t>(col)][static_cast<size_t>(col)];
        for (int j = col; j < n; j++)
            M[static_cast<size_t>(col)][static_cast<size_t>(j)] =
                M[static_cast<size_t>(col)][static_cast<size_t>(j)] * inv;
        b[static_cast<size_t>(col)] = b[static_cast<size_t>(col)] * inv;
        for (int row = 0; row < n; row++) {
            if (row == col || M[static_cast<size_t>(row)][static_cast<size_t>(col)].isZero()) continue;
            Rational f = M[static_cast<size_t>(row)][static_cast<size_t>(col)];
            for (int j = col; j < n; j++)
                M[static_cast<size_t>(row)][static_cast<size_t>(j)] =
                    M[static_cast<size_t>(row)][static_cast<size_t>(j)] -
                    f * M[static_cast<size_t>(col)][static_cast<size_t>(j)];
            b[static_cast<size_t>(row)] = b[static_cast<size_t>(row)] - f * b[static_cast<size_t>(col)];
        }
    }
    out.clear();
    for (int mask = 0; mask < n; mask++) {
        if (b[static_cast<size_t>(mask)].isZero()) continue;
        BigInt rad(1);
        for (int bit = 0; bit < k; bit++)
            if (mask & (1 << bit)) rad = rad * primes[static_cast<size_t>(bit)];
        BigInt square, rest;
        extractSquare(rad, square, rest);
        addTerm(out, rest, b[static_cast<size_t>(mask)] * Rational(square));
    }
    return true;
}

bool isPerfectSquare(const Rational& r, Rational& root) {
    if (r.sign() < 0) return false;
    BigInt sn = BigInt::isqrt(r.num), sd = BigInt::isqrt(r.den);
    if (sn * sn != r.num || sd * sd != r.den) return false;
    root = Rational(sn, sd);
    return true;
}

Value tryDenest(const Rational& a, const Rational& b, const BigInt& rad) {
    if (b.isZero() || rad.isOne()) return Value::nil();
    Rational disc = a * a - Rational(rad) * b * b;
    Rational s;
    if (!isPerfectSquare(disc, s)) return Value::nil();
    Rational x = (a + s) / Rational(2);
    Rational y = (a - s) / Rational(2);
    if (x.sign() < 0 || y.sign() < 0) return Value::nil();
    Value left = sqrtExact(x);
    Value right = sqrtExact(y);
    if (b.sign() < 0) right = radicalNeg(right);
    Value got = radicalBinary(BinOp::Add, left, right);
    TermMap back = mulMaps(fromValue(got), fromValue(got));
    TermMap want;
    addTerm(want, BigInt(1), a);
    addTerm(want, rad, b);
    if (back != want) return Value::nil();
    return got;
}

double termDouble(const Rational& c, const BigInt& r) {
    return c.toDouble() * std::sqrt(r.toDouble());
}

}  // namespace

Value sqrtExact(const Rational& r) {
    if (r.sign() < 0) fail("sqrtExact of a negative number");
    if (r.isZero()) return Value::integer(0);
    BigInt square, rest;
    extractSquare(r.num.abs() * r.den, square, rest);
    Rational coeff(square, r.den);
    if (rest.isOne()) return fromRational(coeff);
    return pack(mulTerm(coeff, rest, Rational(1), BigInt(1)));
}

Value radicalNeg(const Value& v) {
    if (!isRadical(v)) return numNeg(v);
    return pack(negMap(fromValue(v)));
}

Value radicalAbs(const Value& v) {
    if (radicalCompare(v, Value::integer(0)) < 0) return radicalNeg(v);
    return v;
}

Value radicalAsSym(const Value& v) { return fromExpr(exprOf(fromValue(v))); }

double radicalToDouble(const Value& v) {
    double s = 0;
    if (!isRadical(v)) return toDouble(v);
    for (const auto& t : as<ObjRadical>(v)->terms) s += termDouble(t.second, t.first);
    return s;
}

bool radicalEqual(const Value& a, const Value& b) {
    if (!isRadical(a) || !isRadical(b)) return false;
    const auto &x = as<ObjRadical>(a)->terms, &y = as<ObjRadical>(b)->terms;
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++)
        if (x[i].first != y[i].first || x[i].second != y[i].second) return false;
    return true;
}

int radicalCompare(const Value& a, const Value& b) {
    auto asDouble = [](const Value& v) { return isRadical(v) ? radicalToDouble(v) : toDouble(v); };
    if (isRadical(a) && isRadical(b) && radicalEqual(a, b)) return 0;
    if (isExactNumber(a) && isRadical(b)) {
        TermMap m = fromValue(b);
        if (m.size() == 1 && m.begin()->first.isOne() && m.begin()->second == toRational(a)) return 0;
    }
    if (isExactNumber(b) && isRadical(a)) return -radicalCompare(b, a);
    double x = asDouble(a), y = asDouble(b);
    if (x < y) return -1;
    if (x > y) return 1;
    if (valuesEqual(a, b)) return 0;
    std::string sa = toString(a), sb = toString(b);
    return sa < sb ? -1 : (sa > sb ? 1 : 0);
}

std::string radicalToString(const ObjRadical* r) {
    std::string s;
    for (const auto& t : r->terms) {
        Rational c = t.second;
        bool neg = c.sign() < 0;
        if (neg) c = -c;
        std::string piece;
        if (t.first.isOne()) {
            piece = c.toString();
        } else {
            std::string rad = "sqrt(" + t.first.toString() + ")";
            if (c.den.isOne()) {
                piece = c.num.isOne() ? rad : c.num.toString() + rad;
            } else if (c.num.isOne()) {
                piece = rad + "/" + c.den.toString();
            } else {
                piece = c.num.toString() + rad + "/" + c.den.toString();
            }
        }
        if (s.empty()) s = neg ? "-" + piece : piece;
        else s += neg ? " - " + piece : " + " + piece;
    }
    return s.empty() ? "0" : s;
}

Value radicalBinary(BinOp op, const Value& a, const Value& b) {
    if (a.isReal() || b.isReal() || numKind(a) == NumKind::Cplx || numKind(b) == NumKind::Cplx) {
        double x = isRadical(a) ? radicalToDouble(a) : toDouble(a);
        double y = isRadical(b) ? radicalToDouble(b) : toDouble(b);
        switch (op) {
            case BinOp::Add: return Value::real(x + y);
            case BinOp::Sub: return Value::real(x - y);
            case BinOp::Mul:
            case BinOp::Dot:
            case BinOp::Cross: return Value::real(x * y);
            case BinOp::Div: return Value::real(x / y);
            case BinOp::LDiv: return Value::real(y / x);
            case BinOp::Pow: return Value::real(std::pow(x, y));
            default: fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": radicals");
        }
    }
    if (op == BinOp::Pow && isRadical(a) && isExactNumber(b) && toRational(b).den.isOne()) {
        Rational e = toRational(b);
        BigInt mag = e.num.abs();
        if (mag.fitsInt64()) {
            int64_t n = mag.toInt64();
            if (n > 10000) fail("radical power is too large");
            Value base = a;
            Value result = Value::integer(1);
            while (n) {
                if (n & 1) result = radicalBinary(BinOp::Mul, result, base);
                n >>= 1;
                if (n) base = radicalBinary(BinOp::Mul, base, base);
            }
            if (e.sign() < 0) return radicalBinary(BinOp::Div, Value::integer(1), result);
            return result;
        }
    }
    if (op == BinOp::Pow && isRadical(a) && isExactNumber(b) && toRational(b) == Rational(BigInt(1), BigInt(2))) {
        const auto& terms = as<ObjRadical>(a)->terms;
        if (terms.size() <= 2) {
            Rational rat(0), coeff(0);
            BigInt rad(1);
            int extras = 0;
            for (const auto& t : terms) {
                if (t.first.isOne()) rat = t.second;
                else {
                    extras++;
                    coeff = t.second;
                    rad = t.first;
                }
            }
            if (extras == 1) {
                Value d = tryDenest(rat, coeff, rad);
                if (!d.isNil()) return d;
            }
        }
        return fromExpr(cas::simplify(cas::sqrt(exprOf(fromValue(a)))));
    }
    if (!isRadical(a) && !isExactNumber(a))
        fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": " + typeName(a));
    if (!isRadical(b) && !isExactNumber(b))
        fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": " + typeName(b));

    TermMap x = fromValue(a), y = fromValue(b);
    switch (op) {
        case BinOp::Add: return pack(addMaps(x, y));
        case BinOp::Sub: return pack(addMaps(x, negMap(y)));
        case BinOp::Mul:
        case BinOp::Dot:
        case BinOp::Cross: return pack(mulMaps(x, y));
        case BinOp::Div:
        case BinOp::LDiv: {
            if (op == BinOp::LDiv) std::swap(x, y);
            if (y.empty()) fail("division by zero");
            TermMap inv;
            if (!invert(y, inv)) return fromExpr(cas::div(exprOf(x), exprOf(y)));
            return pack(mulMaps(x, inv));
        }
        case BinOp::Pow: return Value::real(std::pow(radicalToDouble(a), toDouble(b)));
        default: fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": radicals");
    }
}
