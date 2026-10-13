#include <algorithm>
#include <map>

#include "cas/expr.h"
#include "cas/poly.h"

namespace cas {

namespace {

std::vector<BigInt> divisorsOf(BigInt n, size_t cap = 4000) {
    n = n.abs();
    std::vector<BigInt> divs{BigInt(1)};
    if (n.isZero()) return divs;
    std::vector<std::pair<BigInt, int>> pf;
    for (int64_t p = 2; p < 100000 && n > BigInt(1); p++) {
        if (n.modSmall(static_cast<uint32_t>(p)) != 0) continue;
        int k = 0;
        while (n.modSmall(static_cast<uint32_t>(p)) == 0) {
            n = n / BigInt(p);
            k++;
        }
        pf.push_back({BigInt(p), k});
        if (BigInt(p) * BigInt(p) > n) break;
    }
    if (n > BigInt(1)) pf.push_back({n, 1});
    for (auto& f : pf) {
        size_t sz = divs.size();
        BigInt pk(1);
        for (int k = 1; k <= f.second; k++) {
            pk = pk * f.first;
            for (size_t i = 0; i < sz; i++) divs.push_back(divs[i] * pk);
        }
        if (divs.size() > cap) break;
    }
    std::sort(divs.begin(), divs.end());
    return divs;
}

Poly primitive(const Poly& p, Rational& content) {
    content = p.content();
    return p * (Rational(1) / content);
}

bool dividesExactly(const Poly& a, const Poly& b, Poly& q) {
    Poly r;
    Poly::divMod(a, b, q, r);
    return r.isZero();
}

// Square-free decomposition (Yun): returns monic factors a_i with p = lead * prod a_i^i.
std::vector<PolyFactor> squareFree(const Poly& p) {
    std::vector<PolyFactor> out;
    Poly f = p.monic();
    Poly fp = f.derivative();
    Poly a0 = Poly::gcd(f, fp);
    Poly b, c, r;
    Poly::divMod(f, a0, b, r);
    Poly::divMod(fp, a0, c, r);
    Poly d = c - b.derivative();
    int i = 1;
    while (b.degree() > 0) {
        Poly a = Poly::gcd(b, d);
        Poly nb, nc;
        Poly::divMod(b, a, nb, r);
        Poly::divMod(d, a, nc, r);
        if (a.degree() > 0) out.push_back({a, i});
        b = nb;
        d = nc - b.derivative();
        i++;
        if (i > 100000) break;
    }
    return out;
}

// ---------------------------------------------------------------- Zassenhaus (big prime)

BigInt modp(const BigInt& a, const BigInt& p) {
    BigInt r = a % p;
    return r.isNeg() ? r + p : r;
}

BigInt powmodInt(BigInt b, BigInt e, const BigInt& m) {
    BigInt r(1);
    b = modp(b, m);
    while (!e.isZero()) {
        if (!e.isEven()) r = (r * b) % m;
        b = (b * b) % m;
        e = e.shr(1);
    }
    return r;
}

bool isProbablePrime(const BigInt& n) {
    if (n < BigInt(2)) return false;
    static const int64_t small[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (int64_t q : small) {
        if (n == BigInt(q)) return true;
        if (n.modSmall(static_cast<uint32_t>(q)) == 0) return false;
    }
    BigInt d = n - BigInt(1);
    int s = 0;
    while (d.isEven()) {
        d = d.shr(1);
        s++;
    }
    BigInt nm1 = n - BigInt(1);
    for (int64_t a : small) {
        BigInt x = powmodInt(BigInt(a), d, n);
        if (x.isOne() || x == nm1) continue;
        bool composite = true;
        for (int r = 1; r < s && composite; r++) {
            x = (x * x) % n;
            if (x == nm1) composite = false;
        }
        if (composite) return false;
    }
    return true;
}

// Dense polynomials over the field Z/p, coefficients in [0, p), lowest degree first.
class Fp {
public:
    using P = std::vector<BigInt>;
    explicit Fp(BigInt prime) : p_(std::move(prime)) {}

    const BigInt& prime() const { return p_; }
    BigInt red(const BigInt& a) const { return modp(a, p_); }

    BigInt inv(const BigInt& a) const {
        BigInt r0 = red(a), r1 = p_, s0(1), s1(0);
        while (!r1.isZero()) {
            BigInt q = r0 / r1;
            BigInt t = r0 - q * r1;
            r0 = r1;
            r1 = t;
            t = s0 - q * s1;
            s0 = s1;
            s1 = t;
        }
        return red(s0);
    }

    static void trim(P& a) {
        while (!a.empty() && a.back().isZero()) a.pop_back();
    }
    static int deg(const P& a) { return static_cast<int>(a.size()) - 1; }

    P sub(const P& a, const P& b) const {
        P r(std::max(a.size(), b.size()), BigInt(0));
        for (size_t i = 0; i < r.size(); i++)
            r[i] = red((i < a.size() ? a[i] : BigInt(0)) - (i < b.size() ? b[i] : BigInt(0)));
        trim(r);
        return r;
    }

    P mul(const P& a, const P& b) const {
        if (a.empty() || b.empty()) return {};
        P r(a.size() + b.size() - 1, BigInt(0));
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i].isZero()) continue;
            for (size_t j = 0; j < b.size(); j++) r[i + j] = r[i + j] + a[i] * b[j];
        }
        for (auto& c : r) c = red(c);
        trim(r);
        return r;
    }

    void divMod(const P& a, const P& b, P& q, P& r) const {
        r = a;
        q.clear();
        int db = deg(b);
        if (deg(r) < db) return;
        q.assign(static_cast<size_t>(deg(r) - db + 1), BigInt(0));
        BigInt il = inv(b.back());
        for (int i = deg(r); i >= db; i--) {
            BigInt c = red(r[static_cast<size_t>(i)] * il);
            if (c.isZero()) continue;
            q[static_cast<size_t>(i - db)] = c;
            for (int j = 0; j <= db; j++) {
                size_t k = static_cast<size_t>(i - db + j);
                r[k] = red(r[k] - c * b[static_cast<size_t>(j)]);
            }
        }
        trim(r);
        trim(q);
    }

    P rem(const P& a, const P& b) const {
        P q, r;
        divMod(a, b, q, r);
        return r;
    }

    P quo(const P& a, const P& b) const {
        P q, r;
        divMod(a, b, q, r);
        return q;
    }

    P monic(const P& a) const {
        if (a.empty()) return a;
        BigInt il = inv(a.back());
        P r = a;
        for (auto& c : r) c = red(c * il);
        return r;
    }

    P gcd(P a, P b) const {
        while (!b.empty()) {
            P r = rem(a, b);
            a = std::move(b);
            b = std::move(r);
        }
        return monic(a);
    }

    P derivative(const P& a) const {
        P r;
        for (size_t i = 1; i < a.size(); i++) r.push_back(red(a[i] * BigInt(static_cast<int64_t>(i))));
        trim(r);
        return r;
    }

    P powMod(P b, const BigInt& e, const P& f) const {
        P r{BigInt(1)};
        b = rem(b, f);
        for (size_t i = e.bitLength(); i-- > 0;) {
            r = rem(mul(r, r), f);
            if (!e.shr(i).isEven()) r = rem(mul(r, b), f);
        }
        return r;
    }

private:
    BigInt p_;
};

uint64_t nextRandom() {
    static uint64_t state = 0x9E3779B97F4A7C15ull;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

// Cantor-Zassenhaus: splits g (monic, square-free, all irreducible factors of degree d).
void equalDegree(const Fp& F, const Fp::P& g, int d, std::vector<Fp::P>& out) {
    if (Fp::deg(g) <= d) {
        out.push_back(g);
        return;
    }
    BigInt e = (BigInt::pow(F.prime(), static_cast<uint64_t>(d)) - BigInt(1)).shr(1);
    for (int attempt = 0; attempt < 400; attempt++) {
        Fp::P a;
        for (int i = 0; i < Fp::deg(g); i++) a.push_back(F.red(BigInt(static_cast<int64_t>(nextRandom() >> 2))));
        Fp::trim(a);
        if (Fp::deg(a) < 1) continue;
        Fp::P b = F.sub(F.powMod(a, e, g), Fp::P{BigInt(1)});
        Fp::P c = F.gcd(b, g);
        if (Fp::deg(c) > 0 && Fp::deg(c) < Fp::deg(g)) {
            equalDegree(F, c, d, out);
            equalDegree(F, F.quo(g, c), d, out);
            return;
        }
    }
    out.push_back(g);  // practically unreachable; recombination still gives a correct result
}

// All monic irreducible factors of a monic square-free polynomial over Z/p.
std::vector<Fp::P> factorModP(const Fp& F, Fp::P f) {
    std::vector<Fp::P> out;
    Fp::P x{BigInt(0), BigInt(1)};
    Fp::P h = x;
    for (int d = 1; 2 * d <= Fp::deg(f); d++) {
        h = F.powMod(h, F.prime(), f);
        Fp::P g = F.gcd(F.sub(h, x), f);
        if (Fp::deg(g) > 0) {
            equalDegree(F, g, d, out);
            f = F.quo(f, g);
            h = F.rem(h, f);
        }
    }
    if (Fp::deg(f) > 0) out.push_back(F.monic(f));
    return out;
}

// Irreducible factors over Z of a square-free primitive integer polynomial with positive leading
// coefficient. Factors mod a prime beyond the Mignotte bound, then recombines subsets.
void zassenhaus(const Poly& f, std::vector<Poly>& out) {
    int n = f.degree();
    BigInt lc = f.lead().num;
    BigInt maxc(0);
    for (const auto& c : f.c)
        if (c.num.abs() > maxc) maxc = c.num.abs();
    BigInt bound = lc.abs() * BigInt::pow(BigInt(2), static_cast<uint64_t>(n)) * BigInt(n + 1) * maxc;
    BigInt p = bound * BigInt(2) + BigInt(1);
    if (p.isEven()) p = p + BigInt(1);

    Fp::P fp;
    for (;;) {
        if (isProbablePrime(p) && !(lc % p).isZero()) {
            Fp F(p);
            fp.clear();
            for (const auto& c : f.c) fp.push_back(F.red(c.num));
            Fp::trim(fp);
            fp = F.monic(fp);
            if (Fp::deg(F.gcd(fp, F.derivative(fp))) == 0) break;
        }
        p = p + BigInt(2);
    }
    Fp F(p);
    std::vector<Fp::P> modular = factorModP(F, fp);
    if (modular.size() <= 1) {
        out.push_back(f);
        return;
    }

    BigInt half = p.shr(1);
    Poly rest = f;
    std::vector<bool> used(modular.size(), false);
    size_t remaining = modular.size();
    size_t budget = 2000000;
    for (size_t k = 1; 2 * k <= remaining; k++) {
        std::vector<size_t> idx;
        for (size_t i = 0; i < modular.size(); i++)
            if (!used[i]) idx.push_back(i);
        // Enumerate k-subsets of idx in lexicographic order.
        std::vector<size_t> pick(k);
        for (size_t i = 0; i < k; i++) pick[i] = i;
        bool found = false;
        while (!found) {
            if (budget-- == 0) {
                out.push_back(rest);
                return;
            }
            Fp::P g{F.red(rest.lead().num)};
            for (size_t i : pick) g = F.mul(g, modular[idx[i]]);
            std::vector<Rational> coeffs;
            for (const auto& c : g) coeffs.push_back(Rational(c > half ? c - p : c));
            Rational cont;
            Poly cand = primitive(Poly(coeffs), cont);
            Poly q;
            if (cand.degree() > 0 && dividesExactly(rest, cand, q)) {
                out.push_back(cand);
                rest = q;
                for (size_t i : pick) used[idx[i]] = true;
                remaining -= k;
                found = true;
                break;
            }
            size_t i = k;
            while (i > 0 && pick[i - 1] == idx.size() - k + (i - 1)) i--;
            if (i == 0) break;
            pick[i - 1]++;
            for (size_t j = i; j < k; j++) pick[j] = pick[j - 1] + 1;
        }
        if (found) k--;  // retry the same subset size on what is left
    }
    if (rest.degree() > 0) {
        Rational cont;
        out.push_back(primitive(rest, cont));
    }
}

// Splits a square-free primitive integer polynomial into irreducible factors over the rationals.
void splitSquareFree(Poly s, std::vector<Poly>& out) {
    if (s.degree() <= 1) {
        if (s.degree() == 1) out.push_back(s);
        return;
    }
    if (s.c[0].isZero()) {
        out.push_back(Poly(std::vector<Rational>{Rational(0), Rational(1)}));
        std::vector<Rational> rest(s.c.begin() + 1, s.c.end());
        splitSquareFree(Poly(rest), out);
        return;
    }
    Rational cont;
    s = primitive(s, cont);
    std::vector<BigInt> ps = divisorsOf(s.c[0].num), qs = divisorsOf(s.lead().num);
    for (const auto& q : qs) {
        for (const auto& p : ps) {
            for (int sign = 1; sign >= -1; sign -= 2) {
                Rational r(sign > 0 ? p : -p, q);
                if (s.degree() >= 1 && s.eval(r).isZero()) {
                    Poly lin(std::vector<Rational>{Rational(-r.num), Rational(r.den)});
                    Poly quo;
                    dividesExactly(s, lin, quo);
                    out.push_back(lin);
                    splitSquareFree(quo, out);
                    return;
                }
            }
        }
    }
    // Without a rational root, degrees 2 and 3 are irreducible.
    if (s.degree() >= 4) {
        zassenhaus(s, out);
        return;
    }
    out.push_back(s);
}

}  // namespace

std::vector<PolyFactor> factorPoly(const Poly& p, Rational& unit) {
    std::vector<PolyFactor> result;
    if (p.degree() <= 0) {
        unit = p.isZero() ? Rational(0) : p.c[0];
        return result;
    }
    Poly product = Poly::constant(Rational(1));
    for (const auto& sf : squareFree(p)) {
        std::vector<Poly> irreducible;
        splitSquareFree(sf.p, irreducible);
        for (auto& f : irreducible) {
            Rational c;
            Poly pf = primitive(f, c);
            result.push_back({pf, sf.mult});
            for (int k = 0; k < sf.mult; k++) product = product * pf;
        }
    }
    unit = p.lead() / product.lead();
    std::sort(result.begin(), result.end(), [](const PolyFactor& a, const PolyFactor& b) {
        if (a.p.degree() != b.p.degree()) return a.p.degree() < b.p.degree();
        return compare(a.p.c[0], b.p.c[0]) < 0;
    });
    return result;
}

namespace {

E factorUnivariate(const E& e, const E& x) {
    Poly p;
    if (!toPoly(e, x, p)) return e;
    Rational unit;
    std::vector<PolyFactor> fs = factorPoly(p, unit);
    std::vector<E> parts{num(unit)};
    for (const auto& f : fs) parts.push_back(pow(fromPoly(f.p, x), num(f.mult)));
    return mul(parts);
}

// Total degree of a polynomial term in the given symbols, or -1 if it is not a monomial.
int64_t termDegree(const E& t, const std::vector<E>& syms) {
    int64_t deg = 0;
    for (const auto& f : t->kind == Kind::Mul ? t->args : std::vector<E>{t}) {
        if (isNum(f)) continue;
        E ex;
        E b = splitPow(f, ex);
        if (std::find(syms.begin(), syms.end(), b) == syms.end() || !isInteger(ex)) return -1;
        deg += ex->q.num.toInt64();
    }
    return deg;
}

// Homogeneous polynomial in x and y: factor p(x, 1) and homogenise each factor.
E factorHomogeneous(const E& e, const E& x, const E& y) {
    std::vector<E> terms = e->args;
    int64_t D = termDegree(terms[0], {x, y});
    if (D < 0) return e;
    for (const auto& t : terms)
        if (termDegree(t, {x, y}) != D) return e;
    Poly p;
    if (!toPoly(subs(e, y, one()), x, p)) return e;
    Rational unit;
    std::vector<PolyFactor> fs = factorPoly(p, unit);
    std::vector<E> parts{num(unit)};
    int64_t used = 0;
    for (const auto& f : fs) {
        std::vector<E> hterms;
        int64_t k = f.p.degree();
        for (int64_t j = 0; j <= k; j++) {
            if (f.p.c[static_cast<size_t>(j)].isZero()) continue;
            hterms.push_back(mul({num(f.p.c[static_cast<size_t>(j)]), pow(x, num(j)), pow(y, num(k - j))}));
        }
        parts.push_back(pow(add(hterms), num(f.mult)));
        used += k * f.mult;
    }
    if (D - used > 0) parts.push_back(pow(y, num(D - used)));
    return mul(parts);
}

// Pulls the common numeric content and common powers out of a sum.
void factorTerms(const E& e, E& common, E& rest) {
    if (e->kind != Kind::Add) {
        common = one();
        rest = e;
        return;
    }
    BigInt g(0), l(1);
    bool allRational = true;
    std::map<const Expr*, std::pair<E, Rational>> minExp;
    bool first = true;
    for (const auto& t : e->args) {
        E r;
        E c = splitCoeff(t, r);
        if (isRationalNum(c)) {
            g = BigInt::gcd(g, c->q.num);
            l = l / BigInt::gcd(l, c->q.den) * c->q.den;
        } else {
            allRational = false;
        }
        std::map<const Expr*, std::pair<E, Rational>> here;
        if (!isOne(r)) {
            for (const auto& f : r->kind == Kind::Mul ? r->args : std::vector<E>{r}) {
                E ex;
                E b = splitPow(f, ex);
                if (isRationalNum(ex) && ex->q.sign() > 0) here[b.get()] = {b, ex->q};
            }
        }
        if (first) {
            minExp = here;
            first = false;
        } else {
            for (auto it = minExp.begin(); it != minExp.end();) {
                auto h = here.find(it->first);
                if (h == here.end()) {
                    it = minExp.erase(it);
                } else {
                    if (h->second.second < it->second.second) it->second.second = h->second.second;
                    ++it;
                }
            }
        }
    }
    std::vector<E> cf;
    if (allRational && !g.isZero()) cf.push_back(num(Rational(g, l)));
    for (auto& m : minExp) cf.push_back(pow(m.second.first, num(m.second.second)));
    common = mul(cf);
    if (isOne(common)) {
        rest = e;
        return;
    }
    std::vector<E> terms;
    for (const auto& t : e->args) terms.push_back(div(t, common));
    rest = add(terms);
}

E factorExpanded(const E& e) {
    E common, rest;
    factorTerms(e, common, rest);
    if (rest->kind != Kind::Add) return mul(common, rest);
    std::vector<E> syms = freeSymbols(rest);
    E fr = rest;
    if (syms.size() == 1) {
        fr = factorUnivariate(rest, syms[0]);
    } else if (syms.size() == 2) {
        fr = factorHomogeneous(rest, syms[0], syms[1]);
    }
    return mul(common, fr);
}

}  // namespace

E factor(const E& e) {
    if (e->args.empty()) return e;
    if (e->kind == Kind::Eq) return eq(factor(e->args[0]), factor(e->args[1]));
    E n, d;
    numDen(e, n, d);
    if (!isOne(d)) return div(factor(n), factor(d));
    if (e->kind == Kind::Fn) {
        std::vector<E> args;
        for (const auto& a : e->args) args.push_back(factor(a));
        return fn(e->name, args);
    }
    E x = expand(n);
    if (x->kind != Kind::Add) {
        if (x->kind == Kind::Mul || x->kind == Kind::Pow) {
            std::vector<E> args;
            for (const auto& a : x->args) args.push_back(a->kind == Kind::Add ? factor(a) : a);
            return rebuild(x, args);
        }
        return x;
    }
    return factorExpanded(x);
}

// ---------------------------------------------------------------- partial fractions

namespace {

bool solveRational(std::vector<std::vector<Rational>>& a, std::vector<Rational>& b, std::vector<Rational>& x) {
    size_t n = b.size();
    for (size_t col = 0; col < n; col++) {
        size_t piv = n;
        for (size_t r = col; r < n; r++)
            if (!a[r][col].isZero()) {
                piv = r;
                break;
            }
        if (piv == n) return false;
        std::swap(a[piv], a[col]);
        std::swap(b[piv], b[col]);
        for (size_t r = 0; r < n; r++) {
            if (r == col || a[r][col].isZero()) continue;
            Rational f = a[r][col] / a[col][col];
            for (size_t k = col; k < n; k++) a[r][k] = a[r][k] - f * a[col][k];
            b[r] = b[r] - f * b[col];
        }
    }
    x.resize(n);
    for (size_t i = 0; i < n; i++) x[i] = b[i] / a[i][i];
    return true;
}

}  // namespace

E apart(const E& e, const E& x) {
    E n, d;
    numDen(together(e), n, d);
    Poly pn, pd;
    if (!toPoly(n, x, pn) || !toPoly(d, x, pd) || pd.degree() <= 0) return e;
    Poly q, r;
    Poly::divMod(pn, pd, q, r);
    Rational unit;
    std::vector<PolyFactor> fs = factorPoly(pd, unit);
    struct Block {
        size_t factor;
        int power;
        Poly cofactor;
        int degree;
    };
    std::vector<Block> blocks;
    size_t unknowns = 0;
    for (size_t i = 0; i < fs.size(); i++) {
        Poly fpow = Poly::constant(Rational(1));
        for (int j = 1; j <= fs[i].mult; j++) {
            fpow = fpow * fs[i].p;
            Poly cof, rem;
            Poly::divMod(pd, fpow, cof, rem);
            blocks.push_back({i, j, cof, fs[i].p.degree()});
            unknowns += static_cast<size_t>(fs[i].p.degree());
        }
    }
    size_t N = static_cast<size_t>(pd.degree());
    if (unknowns != N) return e;
    std::vector<std::vector<Rational>> A(N, std::vector<Rational>(N, Rational(0)));
    std::vector<Rational> b(N, Rational(0));
    for (size_t t = 0; t < N && t < r.c.size(); t++) b[t] = r.c[t];
    size_t col = 0;
    for (const auto& bl : blocks) {
        for (int k = 0; k < bl.degree; k++, col++) {
            Poly term = bl.cofactor * Poly::monomial(Rational(1), static_cast<size_t>(k));
            for (size_t t = 0; t < N && t < term.c.size(); t++) A[t][col] = term.c[t];
        }
    }
    std::vector<Rational> sol;
    if (!solveRational(A, b, sol)) return e;
    std::vector<E> terms{fromPoly(q, x)};
    col = 0;
    for (const auto& bl : blocks) {
        std::vector<Rational> numer;
        for (int k = 0; k < bl.degree; k++) numer.push_back(sol[col++]);
        Poly np(numer);
        if (np.isZero()) continue;
        terms.push_back(div(fromPoly(np, x), pow(fromPoly(fs[bl.factor].p, x), num(bl.power))));
    }
    return add(terms);
}

}  // namespace cas
