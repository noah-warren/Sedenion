#include "cas/expr.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace cas {

const char* const PI_NAME = "π";
const char* const E_NAME = "e";
const char* const I_NAME = "i";

namespace {

size_t hashCombine(size_t h, size_t v) { return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)); }

bool sameNode(const Expr& a, const Expr& b) {
    if (a.kind != b.kind || a.exact != b.exact || a.name != b.name || a.args.size() != b.args.size()) return false;
    if (a.kind == Kind::Num) return a.exact ? a.q == b.q : (a.d == b.d || (std::isnan(a.d) && std::isnan(b.d)));
    for (size_t i = 0; i < a.args.size(); i++)
        if (a.args[i] != b.args[i]) return false;
    return true;
}

struct Table {
    std::unordered_multimap<size_t, std::weak_ptr<const Expr>> map;
    size_t inserts = 0;
};

Table& table() {
    static Table* t = new Table();
    return *t;
}

E intern(Expr x) {
    size_t h = static_cast<size_t>(x.kind) * 1000003u + 17;
    if (x.kind == Kind::Num) {
        if (!x.exact && x.d == 0) x.d = 0.0;
        h = hashCombine(h, x.exact ? x.q.hash() : std::hash<double>()(x.d));
        h = hashCombine(h, x.exact ? 1 : 2);
    }
    if (!x.name.empty()) h = hashCombine(h, std::hash<std::string>()(x.name));
    for (const auto& a : x.args) h = hashCombine(h, a->hash);
    x.hash = h;
    Table& t = table();
    auto range = t.map.equal_range(h);
    for (auto it = range.first; it != range.second;) {
        if (E e = it->second.lock()) {
            if (sameNode(*e, x)) return e;
            ++it;
        } else {
            it = t.map.erase(it);
        }
    }
    E e = std::make_shared<const Expr>(std::move(x));
    t.map.emplace(h, e);
    if (++t.inserts % 200000 == 0) {
        for (auto it = t.map.begin(); it != t.map.end();) {
            if (it->second.expired())
                it = t.map.erase(it);
            else
                ++it;
        }
    }
    return e;
}

E node(Kind k, std::vector<E> args, const std::string& name = "") {
    Expr x;
    x.kind = k;
    x.args = std::move(args);
    x.name = name;
    return intern(std::move(x));
}

int symbolRank(const std::string& name) {
    if (name == PI_NAME) return 0;
    if (name == E_NAME) return 1;
    if (name == I_NAME) return 3;
    return 2;
}

int cmpNum(const E& a, const E& b) {
    if (a->exact && b->exact) return compare(a->q, b->q);
    double x = numValue(a), y = numValue(b);
    if (x < y) return -1;
    if (x > y) return 1;
    if (a->exact != b->exact) return a->exact ? -1 : 1;
    return 0;
}

int cmpBackward(const std::vector<E>& a, const std::vector<E>& b) {
    size_t m = a.size(), n = b.size();
    for (size_t k = 0; k < std::min(m, n); k++) {
        int c = compare(a[m - 1 - k], b[n - 1 - k]);
        if (c) return c;
    }
    return m < n ? -1 : (m > n ? 1 : 0);
}

}  // namespace

// ---------------------------------------------------------------- numbers

E num(const Rational& q) {
    Expr x;
    x.kind = Kind::Num;
    x.q = q;
    return intern(std::move(x));
}

E num(int64_t v) { return num(Rational(v)); }

E real(double d) {
    Expr x;
    x.kind = Kind::Num;
    x.exact = false;
    x.d = d;
    return intern(std::move(x));
}

E sym(const std::string& name) { return node(Kind::Sym, {}, name); }

const E& zero() {
    static const E z = num(0);
    return z;
}
const E& one() {
    static const E o = num(1);
    return o;
}
const E& minusOne() {
    static const E m = num(-1);
    return m;
}
const E& half() {
    static const E h = num(Rational(BigInt(1), BigInt(2)));
    return h;
}
const E& piConst() {
    static const E p = sym(PI_NAME);
    return p;
}
const E& eConst() {
    static const E e = sym(E_NAME);
    return e;
}
const E& imagUnit() {
    static const E i = sym(I_NAME);
    return i;
}

double numValue(const E& e) { return e->exact ? e->q.toDouble() : e->d; }

bool isZero(const E& e) { return e->kind == Kind::Num && (e->exact ? e->q.isZero() : e->d == 0); }
bool isOne(const E& e) { return e->kind == Kind::Num && e->exact && e->q.isOne(); }
bool isInteger(const E& e) { return e->kind == Kind::Num && e->exact && e->q.isInteger(); }
bool isRationalNum(const E& e) { return e->kind == Kind::Num && e->exact; }
bool isNegativeNum(const E& e) { return e->kind == Kind::Num && (e->exact ? e->q.sign() < 0 : e->d < 0); }
bool isNegativeTerm(const E& e) {
    if (isNum(e)) return isNegativeNum(e);
    return e->kind == Kind::Mul && isNegativeNum(e->args[0]);
}
bool isConstantSymbol(const E& e) {
    return e->kind == Kind::Sym && (e->name == PI_NAME || e->name == E_NAME || e->name == I_NAME);
}
bool isSymbol(const E& e) { return e->kind == Kind::Sym; }

E numAddE(const E& a, const E& b) {
    if (a->exact && b->exact) return num(a->q + b->q);
    return real(numValue(a) + numValue(b));
}

E numMulE(const E& a, const E& b) {
    if (a->exact && b->exact) return num(a->q * b->q);
    return real(numValue(a) * numValue(b));
}

// ---------------------------------------------------------------- ordering

int compare(const E& a, const E& b) {
    if (a == b) return 0;
    Kind ka = a->kind, kb = b->kind;
    if (ka == Kind::Num && kb == Kind::Num) return cmpNum(a, b);
    if (ka == Kind::Num) return -1;
    if (kb == Kind::Num) return 1;
    if (ka == Kind::Eq || kb == Kind::Eq) {
        if (ka != kb) return ka == Kind::Eq ? 1 : -1;
        return cmpBackward(a->args, b->args);
    }
    if (ka == Kind::Sym && kb == Kind::Sym) {
        int ra = symbolRank(a->name), rb = symbolRank(b->name);
        if (ra != rb) return ra < rb ? -1 : 1;
        return a->name < b->name ? -1 : (a->name > b->name ? 1 : 0);
    }
    if (ka == kb && (ka == Kind::Add || ka == Kind::Mul)) return cmpBackward(a->args, b->args);
    if (ka == Kind::Pow && kb == Kind::Pow) {
        int c = compare(a->args[0], b->args[0]);
        return c ? c : compare(a->args[1], b->args[1]);
    }
    if (ka == Kind::Fn && kb == Kind::Fn) {
        if (a->name != b->name) return a->name < b->name ? -1 : 1;
        size_t n = std::min(a->args.size(), b->args.size());
        for (size_t k = 0; k < n; k++) {
            int c = compare(a->args[k], b->args[k]);
            if (c) return c;
        }
        return a->args.size() < b->args.size() ? -1 : (a->args.size() > b->args.size() ? 1 : 0);
    }
    if (ka == Kind::Mul) return cmpBackward(a->args, {b});
    if (kb == Kind::Mul) return -cmpBackward(b->args, {a});
    if (ka == Kind::Pow) {
        int c = compare(a->args[0], b);
        return c ? c : compare(a->args[1], one());
    }
    if (kb == Kind::Pow) {
        int c = compare(b->args[0], a);
        return -(c ? c : compare(b->args[1], one()));
    }
    if (ka == Kind::Add) return cmpBackward(a->args, {b});
    if (kb == Kind::Add) return -cmpBackward(b->args, {a});
    if (ka == Kind::Fn && kb == Kind::Sym) return a->name == b->name ? 1 : (a->name < b->name ? -1 : 1);
    if (ka == Kind::Sym && kb == Kind::Fn) return a->name == b->name ? -1 : (a->name < b->name ? -1 : 1);
    return 0;
}

namespace {
bool lessE(const E& a, const E& b) { return compare(a, b) < 0; }
}  // namespace

// ---------------------------------------------------------------- sums

E splitCoeff(const E& term, E& rest) {
    if (term->kind == Kind::Mul && isNum(term->args[0])) {
        if (term->args.size() == 2) {
            rest = term->args[1];
        } else {
            rest = node(Kind::Mul, std::vector<E>(term->args.begin() + 1, term->args.end()));
        }
        return term->args[0];
    }
    if (isNum(term)) {
        rest = one();
        return term;
    }
    rest = term;
    return one();
}

E splitPow(const E& factor, E& exponent) {
    if (factor->kind == Kind::Pow) {
        exponent = factor->args[1];
        return factor->args[0];
    }
    exponent = one();
    return factor;
}

namespace {

// c * rest where rest is a canonical non-numeric expression and c is not 0 or 1.
E scaleTerm(const E& c, const E& rest) {
    if (rest->kind == Kind::Mul) {
        std::vector<E> args{c};
        args.insert(args.end(), rest->args.begin(), rest->args.end());
        return node(Kind::Mul, std::move(args));
    }
    return node(Kind::Mul, {c, rest});
}

}  // namespace

E add(std::vector<E> terms) {
    std::vector<E> flat;
    for (const auto& t : terms) {
        if (t->kind == Kind::Add)
            flat.insert(flat.end(), t->args.begin(), t->args.end());
        else
            flat.push_back(t);
    }
    E constant = zero();
    std::vector<std::pair<E, E>> groups;
    std::unordered_map<const Expr*, size_t> index;
    for (const auto& t : flat) {
        if (t->kind == Kind::Eq) throw CasError("cannot add an equation to an expression");
        if (isNum(t)) {
            constant = numAddE(constant, t);
            continue;
        }
        E rest;
        E c = splitCoeff(t, rest);
        auto it = index.find(rest.get());
        if (it == index.end()) {
            index[rest.get()] = groups.size();
            groups.push_back({rest, c});
        } else {
            groups[it->second].second = numAddE(groups[it->second].second, c);
        }
    }
    std::vector<E> out;
    for (auto& g : groups) {
        if (isZero(g.second)) continue;
        out.push_back(isOne(g.second) ? g.first : scaleTerm(g.second, g.first));
    }
    if (out.empty()) return constant;
    if (!isZero(constant)) out.push_back(constant);
    if (out.size() == 1) return out[0];
    std::sort(out.begin(), out.end(), lessE);
    return node(Kind::Add, std::move(out));
}

E add(const E& a, const E& b) { return add(std::vector<E>{a, b}); }
E sub(const E& a, const E& b) { return add(a, neg(b)); }
E neg(const E& a) { return mul(minusOne(), a); }

// ---------------------------------------------------------------- products

E mul(std::vector<E> factors) {
    std::vector<E> flat;
    for (const auto& f : factors) {
        if (f->kind == Kind::Mul)
            flat.insert(flat.end(), f->args.begin(), f->args.end());
        else
            flat.push_back(f);
    }
    E coeff = one();
    struct Group {
        E base;
        std::vector<E> exps;
        E original;
    };
    std::vector<Group> groups;
    std::unordered_map<const Expr*, size_t> index;
    for (const auto& f : flat) {
        if (f->kind == Kind::Eq) throw CasError("cannot multiply an equation");
        if (isNum(f)) {
            coeff = numMulE(coeff, f);
            continue;
        }
        E ex;
        E base = splitPow(f, ex);
        auto it = index.find(base.get());
        if (it == index.end()) {
            index[base.get()] = groups.size();
            groups.push_back({base, {ex}, f});
        } else {
            groups[it->second].exps.push_back(ex);
        }
    }
    if (isZero(coeff) && coeff->exact) return zero();
    std::vector<E> out, pending;
    for (auto& g : groups) {
        E p = g.exps.size() == 1 ? g.original : pow(g.base, add(g.exps));
        if (isNum(p)) {
            coeff = numMulE(coeff, p);
        } else if (p->kind == Kind::Mul) {
            pending.insert(pending.end(), p->args.begin(), p->args.end());
        } else {
            out.push_back(p);
        }
    }
    // Radicals of integers with the same exponent merge: sqrt(2) sqrt(3) -> sqrt(6).
    std::map<std::string, std::vector<size_t>> radicals;
    for (size_t k = 0; k < out.size(); k++) {
        const E& f = out[k];
        if (f->kind == Kind::Pow && isInteger(f->args[0]) && f->args[0]->q.sign() > 0 && isRationalNum(f->args[1]))
            radicals[f->args[1]->q.toString()].push_back(k);
    }
    bool merged = false;
    std::vector<bool> drop(out.size(), false);
    for (auto& r : radicals) {
        if (r.second.size() < 2) continue;
        Rational prod(1);
        for (size_t k : r.second) {
            prod = prod * out[k]->args[0]->q;
            drop[k] = true;
        }
        pending.push_back(pow(num(prod), out[r.second[0]]->args[1]));
        merged = true;
    }
    if (merged) {
        std::vector<E> kept;
        for (size_t k = 0; k < out.size(); k++)
            if (!drop[k]) kept.push_back(out[k]);
        out = std::move(kept);
    }
    if (!pending.empty()) {
        out.insert(out.end(), pending.begin(), pending.end());
        out.push_back(coeff);
        return mul(out);
    }
    if (isZero(coeff)) return coeff;
    if (out.empty()) return coeff;
    std::sort(out.begin(), out.end(), lessE);
    if (isOne(coeff)) {
        if (out.size() == 1) return out[0];
        return node(Kind::Mul, std::move(out));
    }
    out.insert(out.begin(), coeff);
    return node(Kind::Mul, std::move(out));
}

E mul(const E& a, const E& b) { return mul(std::vector<E>{a, b}); }

E div(const E& a, const E& b) {
    if (isZero(b) && b->exact) throw CasError("division by zero");
    return mul(a, pow(b, minusOne()));
}

// ---------------------------------------------------------------- powers

namespace {

E powNode(const E& b, const E& e) { return node(Kind::Pow, {b, e}); }

// n^f for an integer n > 1 and 0 < f < 1: pulls out perfect powers of small primes and groups
// the remaining prime powers by their fractional exponent.
E integerRadical(const BigInt& n, const Rational& f) {
    if (f.den > BigInt(1000)) return powNode(num(Rational(n)), num(f));
    BigInt rest = n;
    std::map<std::string, std::pair<Rational, BigInt>> byExp;  // exponent text -> (exponent, base)
    Rational outside(1);
    auto addPrime = [&](const BigInt& p, const Rational& e) {
        BigInt whole = e.floor();
        Rational frac = e - Rational(whole);
        if (whole > BigInt(0)) outside = outside * Rational(BigInt::pow(p, static_cast<uint64_t>(whole.toInt64())));
        if (!frac.isZero()) {
            auto& slot = byExp[frac.toString()];
            if (slot.second.isZero()) {
                slot.first = frac;
                slot.second = p;
            } else {
                slot.second = slot.second * p;
            }
        }
    };
    for (uint32_t p = 2; p < 2000 && rest > BigInt(1); p++) {
        if (rest.modSmall(p) != 0) continue;
        int64_t k = 0;
        BigInt bp(static_cast<int64_t>(p));
        while (rest.modSmall(p) == 0) {
            rest = rest / bp;
            k++;
        }
        addPrime(bp, f * Rational(k));
    }
    if (rest > BigInt(1)) {
        // The cofactor may itself be a perfect power, e.g. a large prime squared.
        int64_t power = 1;
        BigInt base = rest;
        for (unsigned k = 2; k <= 64 && k <= rest.bitLength(); k++) {
            BigInt r = BigInt::iroot(rest, k);
            if (BigInt::pow(r, k) == rest) {
                base = r;
                power = k;
            }
        }
        addPrime(base, f * Rational(power));
    }
    std::vector<E> factors{num(outside)};
    for (auto& kv : byExp) factors.push_back(powNode(num(Rational(kv.second.second)), num(kv.second.first)));
    if (factors.size() == 1) return factors[0];
    if (factors.size() == 2 && isOne(factors[0])) return factors[1];
    std::vector<E> rad(factors.begin() + 1, factors.end());
    std::sort(rad.begin(), rad.end(), lessE);
    if (!isOne(factors[0])) rad.insert(rad.begin(), factors[0]);
    if (rad.size() == 1) return rad[0];
    return node(Kind::Mul, std::move(rad));
}

E positiveRadical(const BigInt& n, const Rational& r) {
    // n^r = n^floor(r) * n^frac(r)
    BigInt whole = r.floor();
    Rational frac = r - Rational(whole);
    Rational intPart = Rational::pow(Rational(n), whole.toInt64());
    if (n.isOne()) return one();
    E radical = integerRadical(n, frac);
    if (intPart.isOne()) return radical;
    return mul(num(intPart), radical);
}

E numericPow(const E& b, const E& e) {
    if (!b->exact || !e->exact) {
        double x = numValue(b), y = numValue(e);
        if (x < 0 && std::floor(y) != y) {
            double m = std::pow(-x, y);
            return add(real(m * std::cos(M_PI * y)), mul(real(m * std::sin(M_PI * y)), imagUnit()));
        }
        return real(std::pow(x, y));
    }
    const Rational& q = b->q;
    const Rational& r = e->q;
    if (r.isInteger()) {
        if (!r.num.fitsInt64()) throw CasError("exponent is too large");
        int64_t k = r.num.toInt64();
        if (q.isZero()) {
            if (k < 0) throw CasError("division by zero");
            return zero();
        }
        double bits = static_cast<double>(std::max(q.num.bitLength(), q.den.bitLength())) * std::fabs(static_cast<double>(k));
        if (bits > 4e6) throw CasError("result is too large");
        return num(Rational::pow(q, k));
    }
    if (q.isZero()) {
        if (r.sign() < 0) throw CasError("division by zero");
        return zero();
    }
    if (q.isOne()) return one();
    if (q.sign() < 0) {
        E magnitude = numericPow(num(-q), e);
        if (r.den == BigInt(2)) return mul(pow(imagUnit(), num(Rational(r.num))), magnitude);
        if (!r.den.isEven()) return r.num.isEven() ? magnitude : neg(magnitude);
        return powNode(b, e);
    }
    E top = positiveRadical(q.num, r);
    if (q.den.isOne()) return top;
    return mul(top, positiveRadical(q.den, -r));
}

}  // namespace

E pow(const E& base, const E& exp) {
    if (base->kind == Kind::Eq || exp->kind == Kind::Eq) throw CasError("cannot raise an equation to a power");
    if (isNum(exp) && exp->exact) {
        if (exp->q.isZero()) return one();
        if (exp->q.isOne()) return base;
    }
    if (isNum(base) && isNum(exp)) return numericPow(base, exp);
    if (isZero(base) && base->exact) {
        if (isNum(exp)) {
            if (isNegativeNum(exp)) throw CasError("division by zero");
            return zero();
        }
        return powNode(base, exp);
    }
    if (isOne(base)) return one();
    if (base == imagUnit() && isInteger(exp)) {
        uint32_t k = exp->q.num.modSmall(4);
        if (exp->q.sign() < 0) k = (4 - k) % 4;
        switch (k) {
            case 0: return one();
            case 1: return imagUnit();
            case 2: return minusOne();
            default: return neg(imagUnit());
        }
    }
    if (base == eConst()) {
        if (exp->kind == Kind::Fn && exp->name == "ln") return exp->args[0];
        if (exp->kind == Kind::Mul && exp->args.size() == 2 && isNum(exp->args[0]) && exp->args[1]->kind == Kind::Fn &&
            exp->args[1]->name == "ln")
            return pow(exp->args[1]->args[0], exp->args[0]);
        // Euler: e^(r iπ) = cos(rπ) + i sin(rπ) when both values are known exactly.
        if (exp->kind == Kind::Mul) {
            Rational r(1);
            bool hasPi = false, hasI = false, other = false;
            for (const auto& f : exp->args) {
                if (isNum(f) && f->exact)
                    r = r * f->q;
                else if (f == piConst() && !hasPi)
                    hasPi = true;
                else if (f == imagUnit() && !hasI)
                    hasI = true;
                else
                    other = true;
            }
            uint32_t den = r.den.fitsInt64() ? static_cast<uint32_t>(r.den.toInt64()) : 0;
            if (hasPi && hasI && !other && (den == 1 || den == 2 || den == 3 || den == 4 || den == 6)) {
                E angle = mul(num(r), piConst());
                return add(fn("cos", angle), mul(imagUnit(), fn("sin", angle)));
            }
        }
    }
    if (base->kind == Kind::Pow) {
        const E& b = base->args[0];
        const E& e1 = base->args[1];
        bool positiveBase = isNum(b) && !isNegativeNum(b);
        if (isInteger(exp) || (positiveBase && isNum(e1) && isNum(exp))) return pow(b, mul(e1, exp));
    }
    if (base->kind == Kind::Mul) {
        if (isInteger(exp)) {
            std::vector<E> fs;
            for (const auto& f : base->args) fs.push_back(pow(f, exp));
            return mul(fs);
        }
        const E& c = base->args[0];
        if (isNum(c) && !isNegativeNum(c) && !isOne(c)) {
            E rest = base->args.size() == 2 ? base->args[1]
                                            : node(Kind::Mul, std::vector<E>(base->args.begin() + 1, base->args.end()));
            return mul(pow(c, exp), powNode(rest, exp));
        }
    }
    return powNode(base, exp);
}

E sqrt(const E& a) { return pow(a, half()); }

E eq(const E& lhs, const E& rhs) {
    if (lhs->kind == Kind::Eq || rhs->kind == Kind::Eq) throw CasError("cannot nest equations");
    return node(Kind::Eq, {lhs, rhs});
}

// ---------------------------------------------------------------- functions

namespace {

// Returns true and sets k when e is a rational multiple of π.
bool piMultiple(const E& e, Rational& k) {
    if (isZero(e) && e->exact) {
        k = Rational(0);
        return true;
    }
    if (e == piConst()) {
        k = Rational(1);
        return true;
    }
    if (e->kind == Kind::Mul && e->args.size() == 2 && isRationalNum(e->args[0]) && e->args[1] == piConst()) {
        k = e->args[0]->q;
        return true;
    }
    return false;
}

// sin(kπ) for k with denominator 1, 2, 3, 4 or 6; nullptr otherwise.
E sinPi(Rational k) {
    // Reduce k to [0, 2).
    Rational two(2);
    BigInt f = (k / two).floor();
    k = k - two * Rational(f);
    if (!(k.den == BigInt(1) || k.den == BigInt(2) || k.den == BigInt(3) || k.den == BigInt(4) || k.den == BigInt(6)))
        return nullptr;
    bool negate = false;
    if (k >= Rational(1)) {
        k = k - Rational(1);
        negate = true;
    }
    if (k > Rational(BigInt(1), BigInt(2))) k = Rational(1) - k;
    E r;
    if (k.isZero())
        r = zero();
    else if (k == Rational(BigInt(1), BigInt(2)))
        r = one();
    else if (k == Rational(BigInt(1), BigInt(6)))
        r = half();
    else if (k == Rational(BigInt(1), BigInt(4)))
        r = div(sqrt(num(2)), num(2));
    else if (k == Rational(BigInt(1), BigInt(3)))
        r = div(sqrt(num(3)), num(2));
    else
        return nullptr;
    return negate ? neg(r) : r;
}

bool knownFunction(const std::string& n) {
    static const char* names[] = {"sin",  "cos",   "tan",   "asin", "acos",  "atan",  "sinh",      "cosh",
                                  "tanh", "asinh", "acosh", "atanh", "ln",   "abs",   "floor",     "ceil",
                                  "sign", "gamma", "erf",   "erfc", "factorial", "re", "im", "conj", "arg"};
    for (const char* k : names)
        if (n == k) return true;
    return false;
}

E fnNode(const std::string& name, std::vector<E> args) { return node(Kind::Fn, std::move(args), name); }

bool isPositiveConstant(const E& e) {
    if (isNum(e)) return !isNegativeNum(e) && !isZero(e);
    if (e == piConst() || e == eConst()) return true;
    if (e->kind == Kind::Mul || e->kind == Kind::Add) {
        for (const auto& a : e->args)
            if (!isPositiveConstant(a)) return false;
        return true;
    }
    if (e->kind == Kind::Pow) return isPositiveConstant(e->args[0]) && isNum(e->args[1]);
    return false;
}

}  // namespace

E fn(const std::string& rawName, std::vector<E> args) {
    std::string name = rawName;
    if (name == "log") name = "ln";
    if (name == "arcsin") name = "asin";
    if (name == "arccos") name = "acos";
    if (name == "arctan") name = "atan";
    if (args.size() == 1) {
        const E& u = args[0];
        if (u->kind == Kind::Eq) throw CasError(name + " cannot be applied to an equation");
        if (name == "sqrt") return sqrt(u);
        if (name == "cbrt") return pow(u, num(Rational(BigInt(1), BigInt(3))));
        if (name == "exp") return pow(eConst(), u);
        if (name == "sec") return pow(fn("cos", u), minusOne());
        if (name == "csc") return pow(fn("sin", u), minusOne());
        if (name == "cot") return pow(fn("tan", u), minusOne());
        if (isNum(u) && !u->exact && knownFunction(name)) {
            std::complex<double> out;
            if (evalFnComplex(name, {std::complex<double>(u->d, 0)}, out)) {
                if (out.imag() == 0) return real(out.real());
                return add(real(out.real()), mul(real(out.imag()), imagUnit()));
            }
        }
        Rational k;
        if (name == "sin") {
            if (piMultiple(u, k)) {
                if (E r = sinPi(k)) return r;
            }
            if (isNegativeTerm(u)) return neg(fn("sin", neg(u)));
            if (u->kind == Kind::Fn && u->name == "asin") return u->args[0];
        } else if (name == "cos") {
            if (piMultiple(u, k)) {
                if (E r = sinPi(k + Rational(BigInt(1), BigInt(2)))) return r;
            }
            if (isNegativeTerm(u)) return fn("cos", neg(u));
            if (u->kind == Kind::Fn && u->name == "acos") return u->args[0];
        } else if (name == "tan") {
            if (piMultiple(u, k)) {
                E s = sinPi(k), c = sinPi(k + Rational(BigInt(1), BigInt(2)));
                if (s && c && !isZero(c)) return div(s, c);
            }
            if (isNegativeTerm(u)) return neg(fn("tan", neg(u)));
            if (u->kind == Kind::Fn && u->name == "atan") return u->args[0];
        } else if (name == "asin" || name == "atan" || name == "sinh" || name == "tanh" || name == "asinh" ||
                   name == "atanh" || name == "erf") {
            if (isZero(u)) return zero();
            if (isNegativeTerm(u)) return neg(fn(name, neg(u)));
            if (name == "asin") {
                if (isOne(u)) return div(piConst(), num(2));
                if (u == half()) return div(piConst(), num(6));
                if (u == div(sqrt(num(2)), num(2))) return div(piConst(), num(4));
                if (u == div(sqrt(num(3)), num(2))) return div(piConst(), num(3));
            }
            if (name == "atan") {
                if (isOne(u)) return div(piConst(), num(4));
                if (u == sqrt(num(3))) return div(piConst(), num(3));
                if (u == div(sqrt(num(3)), num(3))) return div(piConst(), num(6));
            }
        } else if (name == "acos") {
            if (isOne(u)) return zero();
            if (isZero(u)) return div(piConst(), num(2));
            if (u == minusOne()) return piConst();
            if (u == half()) return div(piConst(), num(3));
            if (u == neg(half())) return div(mul(num(2), piConst()), num(3));
            if (u == div(sqrt(num(2)), num(2))) return div(piConst(), num(4));
            if (u == div(sqrt(num(3)), num(2))) return div(piConst(), num(6));
        } else if (name == "cosh") {
            if (isZero(u)) return one();
            if (isNegativeTerm(u)) return fn("cosh", neg(u));
        } else if (name == "acosh") {
            if (isOne(u)) return zero();
        } else if (name == "ln") {
            if (isOne(u)) return zero();
            if (u == eConst()) return one();
            if (isZero(u)) throw CasError("ln(0) is undefined");
            if (u->kind == Kind::Pow && u->args[0] == eConst()) return u->args[1];
            if (isRationalNum(u)) {
                if (u->q.sign() < 0) return add(fn("ln", num(-u->q)), mul(imagUnit(), piConst()));
                if (u->q.num.isOne()) return neg(fn("ln", num(Rational(u->q.den))));
                if (u->q.isInteger() && u->q.num.bitLength() <= 62) {
                    BigInt n = u->q.num;
                    for (unsigned p = static_cast<unsigned>(n.bitLength()); p >= 2; p--) {
                        BigInt r = BigInt::iroot(n, p);
                        if (BigInt::pow(r, p) == n) return mul(num(static_cast<int64_t>(p)), fn("ln", num(Rational(r))));
                    }
                }
            }
        } else if (name == "abs") {
            if (isNum(u)) return u->exact ? num(u->q.abs()) : real(std::fabs(u->d));
            if (isNegativeTerm(u)) return fn("abs", neg(u));
            if (isPositiveConstant(u)) return u;
            if (u->kind == Kind::Pow && isInteger(u->args[1]) && u->args[1]->q.num.isEven()) return u;
            if (u->kind == Kind::Fn && u->name == "abs") return u;
        } else if (name == "floor" || name == "ceil" || name == "sign") {
            if (isRationalNum(u)) {
                if (name == "floor") return num(Rational(u->q.floor()));
                if (name == "ceil") return num(Rational(u->q.ceil()));
                return num(u->q.sign());
            }
            if (freeSymbols(u).empty()) {
                std::complex<double> v;
                if (evalComplex(u, nullptr, v) && v.imag() == 0) {
                    double d = v.real();
                    if (name == "floor") return num(Rational(BigInt::fromDouble(std::floor(d))));
                    if (name == "ceil") return num(Rational(BigInt::fromDouble(std::ceil(d))));
                    return num(d > 0 ? 1 : (d < 0 ? -1 : 0));
                }
            }
        } else if (name == "factorial" || name == "gamma") {
            if (isInteger(u)) {
                BigInt n = u->q.num;
                if (name == "gamma") n = n - BigInt(1);
                if (n.sign() < 0) throw CasError(name + " is undefined at non-positive integers");
                if (n <= BigInt(2000)) {
                    BigInt r(1);
                    for (int64_t k = 2; k <= n.toInt64(); k++) r = r * BigInt(k);
                    return num(Rational(r));
                }
            }
            if (name == "gamma" && u == half()) return sqrt(piConst());
        }
    }
    return fnNode(name, std::move(args));
}

E fn(const std::string& name, const E& arg) { return fn(name, std::vector<E>{arg}); }

E rebuild(const E& e, std::vector<E> args) {
    switch (e->kind) {
        case Kind::Add: return add(std::move(args));
        case Kind::Mul: return mul(std::move(args));
        case Kind::Pow: return pow(args[0], args[1]);
        case Kind::Fn: return fn(e->name, std::move(args));
        case Kind::Eq: return eq(args[0], args[1]);
        default: return e;
    }
}

// ---------------------------------------------------------------- structure

bool freeOf(const E& e, const E& x) {
    if (e == x) return false;
    for (const auto& a : e->args)
        if (!freeOf(a, x)) return false;
    return true;
}

namespace {
void collectSymbols(const E& e, std::vector<E>& out) {
    if (e->kind == Kind::Sym) {
        if (!isConstantSymbol(e) && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        return;
    }
    for (const auto& a : e->args) collectSymbols(a, out);
}
}  // namespace

std::vector<E> freeSymbols(const E& e) {
    std::vector<E> out;
    collectSymbols(e, out);
    std::sort(out.begin(), out.end(), [](const E& a, const E& b) { return a->name < b->name; });
    return out;
}

int countNodes(const E& e) {
    int n = 1;
    for (const auto& a : e->args) n += countNodes(a);
    return n;
}

bool isInexact(const E& e) {
    if (isNum(e)) return !e->exact;
    for (const auto& a : e->args)
        if (isInexact(a)) return true;
    return false;
}

E subs(const E& e, const E& x, const E& v) {
    if (e == x) return v;
    if (e->args.empty()) return e;
    std::vector<E> args;
    bool changed = false;
    for (const auto& a : e->args) {
        E s = subs(a, x, v);
        changed = changed || s != a;
        args.push_back(s);
    }
    return changed ? rebuild(e, std::move(args)) : e;
}

}  // namespace cas
