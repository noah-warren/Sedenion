#include <algorithm>
#include <functional>
#include <map>

#include "cas/expr.h"
#include "cas/poly.h"

namespace cas {

namespace {

std::vector<E> termsOf(const E& e) { return e->kind == Kind::Add ? e->args : std::vector<E>{e}; }
std::vector<E> factorsOf(const E& e) { return e->kind == Kind::Mul ? e->args : std::vector<E>{e}; }

E distribute(const E& a, const E& b) {
    std::vector<E> ta = termsOf(a), tb = termsOf(b);
    if (ta.size() == 1 && tb.size() == 1) return mul(a, b);
    if (ta.size() * tb.size() > 200000) throw CasError("expansion is too large");
    std::vector<E> terms;
    terms.reserve(ta.size() * tb.size());
    for (const auto& x : ta)
        for (const auto& y : tb) terms.push_back(mul(x, y));
    return add(terms);
}

E powExpand(const E& base, int64_t n) {
    E result = one(), b = base;
    while (n) {
        if (n & 1) result = distribute(result, b);
        n >>= 1;
        if (n) b = distribute(b, b);
    }
    return result;
}

}  // namespace

E expand(const E& e) {
    switch (e->kind) {
        case Kind::Num:
        case Kind::Sym: return e;
        case Kind::Add: {
            std::vector<E> terms;
            for (const auto& t : e->args) terms.push_back(expand(t));
            return add(terms);
        }
        case Kind::Mul: {
            E r = one();
            for (const auto& f : e->args) r = distribute(r, expand(f));
            return r;
        }
        case Kind::Pow: {
            E base = expand(e->args[0]), ex = expand(e->args[1]);
            if (base->kind == Kind::Add && isInteger(ex) && ex->q.num.fitsInt64()) {
                int64_t n = ex->q.num.toInt64();
                if (n > 0 && n <= 1000) return powExpand(base, n);
                if (n < 0 && n >= -1000) return pow(powExpand(base, -n), minusOne());
            }
            E p = pow(base, ex);
            if (p->kind == Kind::Mul || p->kind == Kind::Add) return expand(p);
            return p;
        }
        case Kind::Fn: {
            std::vector<E> args;
            for (const auto& a : e->args) args.push_back(expand(a));
            return fn(e->name, args);
        }
        case Kind::Eq: return eq(expand(e->args[0]), expand(e->args[1]));
    }
    return e;
}

namespace {

// Splits a denominator into (base -> exponent) with an integer coefficient, for LCM computation.
void denominatorParts(const E& d, BigInt& coeff, std::vector<std::pair<E, E>>& parts) {
    coeff = BigInt(1);
    parts.clear();
    for (const auto& f : factorsOf(d)) {
        if (isInteger(f)) {
            coeff = coeff * f->q.num;
            continue;
        }
        E ex;
        E base = splitPow(f, ex);
        parts.push_back({base, ex});
    }
}

E lcmDenominators(const std::vector<E>& dens) {
    BigInt lcmCoeff(1);
    std::vector<std::pair<E, E>> best;
    for (const auto& d : dens) {
        BigInt c;
        std::vector<std::pair<E, E>> parts;
        denominatorParts(d, c, parts);
        lcmCoeff = lcmCoeff / BigInt::gcd(lcmCoeff, c) * c;
        for (const auto& p : parts) {
            bool found = false;
            for (auto& b : best) {
                if (b.first != p.first) continue;
                found = true;
                if (isNum(b.second) && isNum(p.second) && numValue(p.second) > numValue(b.second)) b.second = p.second;
                else if (!(isNum(b.second) && isNum(p.second)) && b.second != p.second)
                    b.second = add(b.second, p.second);
            }
            if (!found) best.push_back(p);
        }
    }
    std::vector<E> fs{num(Rational(lcmCoeff))};
    for (const auto& b : best) fs.push_back(pow(b.first, b.second));
    return mul(fs);
}

}  // namespace

void numDen(const E& e, E& n, E& d) {
    switch (e->kind) {
        case Kind::Num:
            if (e->exact) {
                n = num(Rational(e->q.num));
                d = num(Rational(e->q.den));
            } else {
                n = e;
                d = one();
            }
            return;
        case Kind::Pow: {
            const E& ex = e->args[1];
            if (isNegativeTerm(ex)) {
                E bn, bd;
                numDen(e->args[0], bn, bd);
                E k = neg(ex);
                n = pow(bd, k);
                d = pow(bn, k);
                return;
            }
            if (isInteger(ex)) {
                E bn, bd;
                numDen(e->args[0], bn, bd);
                n = pow(bn, ex);
                d = pow(bd, ex);
                return;
            }
            n = e;
            d = one();
            return;
        }
        case Kind::Mul: {
            std::vector<E> ns, ds;
            for (const auto& f : e->args) {
                E fn_, fd;
                numDen(f, fn_, fd);
                ns.push_back(fn_);
                ds.push_back(fd);
            }
            n = mul(ns);
            d = mul(ds);
            return;
        }
        case Kind::Add: {
            std::vector<E> ns, ds;
            for (const auto& t : e->args) {
                E tn, td;
                numDen(t, tn, td);
                ns.push_back(tn);
                ds.push_back(td);
            }
            E D = lcmDenominators(ds);
            std::vector<E> terms;
            for (size_t k = 0; k < ns.size(); k++) terms.push_back(mul(ns[k], div(D, ds[k])));
            n = add(terms);
            d = D;
            return;
        }
        default:
            n = e;
            d = one();
    }
}

E together(const E& e) {
    E n, d;
    numDen(e, n, d);
    if (isOne(d)) return n;
    return div(n, d);
}

E cancel(const E& e) {
    if (e->kind == Kind::Eq) return eq(cancel(e->args[0]), cancel(e->args[1]));
    E n, d;
    numDen(e, n, d);
    n = expand(n);
    d = expand(d);
    if (isOne(d)) return n;
    std::vector<E> syms = freeSymbols(add(n, d));
    if (syms.size() == 1) {
        const E& x = syms[0];
        Poly pn, pd;
        if (!toPoly(n, x, pn) || !toPoly(d, x, pd)) return div(n, d);
        Poly g = Poly::gcd(pn, pd);
        Poly qn = pn, qd = pd;
        if (g.degree() > 0) {
            Poly r;
            Poly::divMod(pn, g, qn, r);
            Poly::divMod(pd, g, qd, r);
        }
        Rational cd = qd.content();
        qn = qn * (Rational(1) / cd);
        qd = qd * (Rational(1) / cd);
        Rational cn = qn.content();
        qn = qn * (Rational(1) / cn);
        E top = mul(num(cn), fromPoly(qn, x));
        if (qd.degree() == 0) return mul(top, num(Rational(1) / qd.c[0]));
        return div(top, fromPoly(qd, x));
    }
    return div(n, d);
}

namespace {

E mapTree(const E& e, const std::function<E(const E&)>& f) {
    if (e->args.empty()) return f(e);
    std::vector<E> args;
    bool changed = false;
    for (const auto& a : e->args) {
        E m = mapTree(a, f);
        changed = changed || m != a;
        args.push_back(m);
    }
    return f(changed ? rebuild(e, args) : e);
}

bool isFnOf(const E& e, const char* name) { return e->kind == Kind::Fn && e->name == name && e->args.size() == 1; }

// Rewrites even powers of `from` using from^2 = sign*(1 - to^2) style identities.
E powerIdentity(const E& e, const char* from, const char* to, bool hyperbolic) {
    return mapTree(e, [&](const E& x) -> E {
        if (x->kind != Kind::Pow || !isFnOf(x->args[0], from) || !isInteger(x->args[1])) return x;
        const Rational& k = x->args[1]->q;
        if (k < Rational(2)) return x;
        int64_t n = k.num.toInt64();
        const E& u = x->args[0]->args[0];
        E other2 = pow(fn(to, u), num(2));
        E sq;
        if (!hyperbolic) {
            sq = sub(one(), other2);  // sin^2 = 1 - cos^2 and cos^2 = 1 - sin^2
        } else if (std::string(from) == "sinh") {
            sq = sub(other2, one());  // sinh^2 = cosh^2 - 1
        } else {
            sq = add(one(), other2);  // cosh^2 = 1 + sinh^2
        }
        E r = pow(sq, num(n / 2));
        if (n % 2) r = mul(r, x->args[0]);
        return r;
    });
}

E tanCombine(const E& e) {
    return mapTree(e, [](const E& x) -> E {
        if (x->kind != Kind::Mul) return x;
        std::vector<E> fs = x->args;
        bool changed = false;
        for (size_t i = 0; i < fs.size(); i++) {
            E pi;
            E si = splitPow(fs[i], pi);
            if (!isFnOf(si, "sin") || !isNum(pi)) continue;
            for (size_t j = 0; j < fs.size(); j++) {
                E pj;
                E cj = splitPow(fs[j], pj);
                if (i == j || !isFnOf(cj, "cos") || cj->args[0] != si->args[0] || !isNum(pj)) continue;
                if (!isZero(numAddE(pi, pj))) continue;
                fs[i] = pow(fn("tan", si->args[0]), pi);
                fs[j] = one();
                changed = true;
            }
        }
        return changed ? mul(fs) : x;
    });
}

bool hasFn(const E& e, const char* name) {
    if (isFnOf(e, name)) return true;
    for (const auto& a : e->args)
        if (hasFn(a, name)) return true;
    return false;
}

bool hasDivision(const E& e) {
    if (e->kind == Kind::Pow && isNegativeTerm(e->args[1])) return true;
    if (isRationalNum(e) && !e->q.isInteger()) return true;
    for (const auto& a : e->args)
        if (hasDivision(a)) return true;
    return false;
}

int score(const E& e) {
    int n = countNodes(e);
    if (hasDivision(e)) n += 1;
    return n;
}

E simplifyRec(const E& e, int depth) {
    if (e->args.empty()) return e;
    if (e->kind == Kind::Eq) return eq(simplifyRec(e->args[0], depth), simplifyRec(e->args[1], depth));
    std::vector<E> args;
    for (const auto& a : e->args) args.push_back(simplifyRec(a, depth + 1));
    E r = rebuild(e, args);
    if (r->args.empty()) return r;
    std::vector<E> candidates{r};
    auto tryAdd = [&](const std::function<E()>& f) {
        try {
            candidates.push_back(f());
        } catch (const CasError&) {
        }
    };
    if (countNodes(r) < 400) {
        tryAdd([&] { return expand(r); });
        if (hasDivision(r)) {
            tryAdd([&] { return cancel(r); });
            tryAdd([&] {
                E n, d;
                numDen(r, n, d);
                return div(factor(n), factor(d));
            });
        }
        if (hasFn(r, "sin") || hasFn(r, "cos")) {
            tryAdd([&] { return expand(powerIdentity(r, "sin", "cos", false)); });
            tryAdd([&] { return expand(powerIdentity(r, "cos", "sin", false)); });
            tryAdd([&] { return tanCombine(r); });
        }
        if (hasFn(r, "sinh") || hasFn(r, "cosh")) {
            tryAdd([&] { return expand(powerIdentity(r, "sinh", "cosh", true)); });
            tryAdd([&] { return expand(powerIdentity(r, "cosh", "sinh", true)); });
        }
        size_t base = candidates.size();
        for (size_t k = 1; k < base; k++) {
            E c = candidates[k];
            if (hasDivision(c)) tryAdd([&] { return cancel(c); });
        }
    }
    E best = r;
    int bestScore = score(r);
    for (const auto& c : candidates) {
        int s = score(c);
        if (s < bestScore) {
            best = c;
            bestScore = s;
        }
    }
    return best;
}

}  // namespace

E simplify(const E& e) { return simplifyRec(e, 0); }

}  // namespace cas
