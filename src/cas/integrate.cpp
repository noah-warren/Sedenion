#include <algorithm>
#include <functional>

#include "cas/expr.h"
#include "cas/poly.h"
#include "cas/steps.h"

namespace cas {

namespace {

const int kMaxDepth = 9;

E integ(const E& f, const E& x, int depth);

bool linearIn(const E& u, const E& x, E& a, E& b) {
    std::vector<E> c;
    if (!toPolyE(u, x, c) || c.size() != 2) return false;
    a = c[1];
    b = c[0];
    return !isZero(a);
}

bool isFnOf(const E& e, const char* name) { return e->kind == Kind::Fn && e->name == name && e->args.size() == 1; }

// Antiderivative of name(u) with respect to u.
E tableFn(const std::string& name, const E& u) {
    if (name == "sin") return neg(fn("cos", u));
    if (name == "cos") return fn("sin", u);
    if (name == "tan") return neg(fn("ln", fn("cos", u)));
    if (name == "sinh") return fn("cosh", u);
    if (name == "cosh") return fn("sinh", u);
    if (name == "tanh") return fn("ln", fn("cosh", u));
    if (name == "ln") return sub(mul(u, fn("ln", u)), u);
    if (name == "asin") return add(mul(u, fn("asin", u)), sqrt(sub(one(), pow(u, num(2)))));
    if (name == "acos") return sub(mul(u, fn("acos", u)), sqrt(sub(one(), pow(u, num(2)))));
    if (name == "atan") return sub(mul(u, fn("atan", u)), div(fn("ln", add(one(), pow(u, num(2)))), num(2)));
    if (name == "asinh") return sub(mul(u, fn("asinh", u)), sqrt(add(pow(u, num(2)), one())));
    if (name == "erf") return add(mul(u, fn("erf", u)), div(pow(eConst(), neg(pow(u, num(2)))), sqrt(piConst())));
    return nullptr;
}

// ∫ (c + a x^2)^n dx for n = ±1/2 (arcsine / inverse hyperbolic forms).
E quadraticRoot(const E& base, const E& n, const E& x) {
    std::vector<E> c;
    if (!toPolyE(base, x, c) || c.size() != 3 || !isZero(c[1])) return nullptr;
    E k = c[0], a = c[2];
    if (!isRationalNum(k) || !isRationalNum(a)) return nullptr;
    bool aNeg = a->q.sign() < 0, kPos = k->q.sign() > 0;
    if (n == neg(half())) {
        if (aNeg && kPos) return div(fn("asin", mul(sqrt(div(neg(a), k)), x)), sqrt(neg(a)));
        if (!aNeg && kPos) return div(fn("asinh", mul(sqrt(div(a, k)), x)), sqrt(a));
        if (!aNeg && !kPos) return div(fn("acosh", mul(sqrt(div(a, neg(k))), x)), sqrt(a));
        return nullptr;
    }
    if (n == half() && aNeg && kPos) {
        // ∫ sqrt(k - |a| x^2) = x sqrt(...)/2 + k/(2 sqrt|a|) asin(sqrt(|a|/k) x)
        E r = sqrt(base);
        return add(div(mul(x, r), num(2)), mul(div(k, mul(num(2), sqrt(neg(a)))), fn("asin", mul(sqrt(div(neg(a), k)), x))));
    }
    return nullptr;
}

// ∫ (p1 x + p0) / (x^2 + b x + c) for an irreducible quadratic.
E quadraticFraction(const Poly& numer, const Poly& quad, const E& x) {
    Rational l = quad.lead();
    Rational b = quad.c[1] / l, c = quad.c[0] / l;
    Rational B = numer.c.size() > 1 ? numer.c[1] / l : Rational(0);
    Rational Cc = numer.c.empty() ? Rational(0) : numer.c[0] / l;
    E q = add({pow(x, num(2)), mul(num(b), x), num(c)});
    std::vector<E> terms;
    if (!B.isZero()) terms.push_back(mul(num(B / Rational(2)), fn("ln", q)));
    Rational rest = Cc - B * b / Rational(2);
    if (!rest.isZero()) {
        Rational disc = Rational(4) * c - b * b;
        E lin = add(mul(num(2), x), num(b));
        if (disc.sign() > 0) {
            E s = sqrt(num(disc));
            terms.push_back(mul({num(rest * Rational(2)), pow(s, minusOne()), fn("atan", div(lin, s))}));
        } else {
            E s = sqrt(num(-disc));
            terms.push_back(mul({num(rest), pow(s, minusOne()), fn("ln", div(sub(lin, s), add(lin, s)))}));
        }
    }
    return add(terms);
}

E rationalIntegral(const E& f, const E& x, int depth) {
    E n, d;
    numDen(together(f), n, d);
    Poly pn, pd;
    if (!toPoly(n, x, pn) || !toPoly(d, x, pd) || pd.degree() < 1) return nullptr;
    E parts = apart(f, x);
    std::vector<E> terms = parts->kind == Kind::Add ? parts->args : std::vector<E>{parts};
    std::vector<E> out;
    for (const auto& t : terms) {
        E tn, td;
        numDen(t, tn, td);
        Poly ptn, ptd;
        if (!toPoly(tn, x, ptn) || !toPoly(td, x, ptd)) return nullptr;
        if (ptd.degree() == 2 && ptn.degree() <= 1) {
            Rational unit;
            std::vector<PolyFactor> fs = factorPoly(ptd, unit);
            if (fs.size() == 1 && fs[0].mult == 1 && fs[0].p.degree() == 2) {
                out.push_back(quadraticFraction(ptn * (Rational(1) / unit), fs[0].p, x));
                continue;
            }
        }
        if (ptd.degree() >= 2) {
            Rational unit;
            std::vector<PolyFactor> fs = factorPoly(ptd, unit);
            if (!(fs.size() == 1 && fs[0].p.degree() == 1)) return nullptr;
        }
        E r = integ(t, x, depth + 1);
        if (!r) return nullptr;
        out.push_back(r);
    }
    return add(out);
}

E powerReduce(const E& e, bool& changed) {
    if (e->kind == Kind::Pow && isInteger(e->args[1]) && e->args[1]->q >= Rational(2) &&
        (isFnOf(e->args[0], "sin") || isFnOf(e->args[0], "cos"))) {
        int64_t n = e->args[1]->q.num.toInt64();
        const E& u = e->args[0]->args[0];
        bool isSin = e->args[0]->name == "sin";
        changed = true;
        if (n % 2 == 0) {
            E c2 = fn("cos", mul(num(2), u));
            E sq = div(isSin ? sub(one(), c2) : add(one(), c2), num(2));
            return pow(sq, num(n / 2));
        }
        E other2 = pow(fn(isSin ? "cos" : "sin", u), num(2));
        return mul(e->args[0], pow(sub(one(), other2), num((n - 1) / 2)));
    }
    if (e->args.empty()) return e;
    std::vector<E> args;
    for (const auto& a : e->args) args.push_back(powerReduce(a, changed));
    return rebuild(e, args);
}

E productToSum(const E& f) {
    if (f->kind != Kind::Mul) return nullptr;
    std::vector<E> trig, other;
    for (const auto& a : f->args) ((isFnOf(a, "sin") || isFnOf(a, "cos")) ? trig : other).push_back(a);
    if (trig.size() != 2) return nullptr;
    const E &A = trig[0]->args[0], &B = trig[1]->args[0];
    bool sa = trig[0]->name == "sin", sb = trig[1]->name == "sin";
    E plus = add(A, B), minus = sub(A, B);
    E r;
    if (sa && !sb) r = div(add(fn("sin", plus), fn("sin", minus)), num(2));
    else if (!sa && sb) r = div(sub(fn("sin", plus), fn("sin", minus)), num(2));
    else if (sa && sb) r = div(sub(fn("cos", minus), fn("cos", plus)), num(2));
    else r = div(add(fn("cos", minus), fn("cos", plus)), num(2));
    other.push_back(r);
    return expand(mul(other));
}

// ∫ e^(a x + b) sin/cos(c x + d)
E expTrig(const E& f, const E& x) {
    if (f->kind != Kind::Mul || f->args.size() != 2) return nullptr;
    E ex, tr;
    for (const auto& a : f->args) {
        if (a->kind == Kind::Pow && a->args[0] == eConst()) ex = a;
        else if (isFnOf(a, "sin") || isFnOf(a, "cos")) tr = a;
    }
    if (!ex || !tr) return nullptr;
    E a, b, c, d;
    if (!linearIn(ex->args[1], x, a, b) || !linearIn(tr->args[0], x, c, d)) return nullptr;
    E denom = add(pow(a, num(2)), pow(c, num(2)));
    E s = fn("sin", tr->args[0]), co = fn("cos", tr->args[0]);
    E inner = tr->name == "sin" ? sub(mul(a, s), mul(c, co)) : add(mul(a, co), mul(c, s));
    return div(mul(ex, inner), denom);
}

E replaceSub(const E& e, const E& g, const E& u) {
    if (e == g) return u;
    if (g->kind == Kind::Pow && e->kind == Kind::Pow && e->args[0] == g->args[0]) {
        E ratio = simplify(div(e->args[1], g->args[1]));
        if (isRationalNum(ratio)) return pow(u, ratio);
    }
    if (e->args.empty()) return e;
    std::vector<E> args;
    for (const auto& a : e->args) args.push_back(replaceSub(a, g, u));
    return rebuild(e, args);
}

void substitutionCandidates(const E& e, const E& x, std::vector<E>& out) {
    if (freeOf(e, x) || e == x) return;
    E a, b;
    if (e->kind == Kind::Fn || e->kind == Kind::Pow || e->kind == Kind::Add) {
        if (!linearIn(e, x, a, b) && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
    }
    for (const auto& c : e->args) substitutionCandidates(c, x, out);
}

E uSubstitution(const E& f, const E& x, int depth, StepScope& s) {
    std::vector<E> candidates;
    substitutionCandidates(f, x, candidates);
    E u = sym("_u" + std::to_string(depth));
    for (const auto& g : candidates) {
        E dg = diff(g, x);
        if (isZero(dg)) continue;
        E q;
        try {
            q = simplify(div(f, dg));
        } catch (const CasError&) {
            continue;
        }
        E qu = replaceSub(q, g, u);
        if (!freeOf(qu, x)) continue;
        E r = integ(qu, u, depth + 1);
        if (r) {
            if (s.active())
                s.rule("substitution u = " + stepExpr(g) + ", du = " + stepExprParen(simplify(dg)) + " d" +
                       stepExpr(x));
            return subs(r, u, g);
        }
    }
    return nullptr;
}

int liate(const E& f, const E& x) {
    if (isFnOf(f, "ln")) return 5;
    if (isFnOf(f, "asin") || isFnOf(f, "acos") || isFnOf(f, "atan") || isFnOf(f, "asinh")) return 4;
    if (f == x || (f->kind == Kind::Pow && f->args[0] == x && isInteger(f->args[1]) && f->args[1]->q.sign() > 0))
        return 3;
    std::vector<E> c;
    if (f->kind == Kind::Add && toPolyE(f, x, c)) return 3;
    if (isFnOf(f, "sin") || isFnOf(f, "cos") || isFnOf(f, "sinh") || isFnOf(f, "cosh")) return 2;
    if (f->kind == Kind::Pow && freeOf(f->args[0], x)) return 1;
    return 0;
}

E byParts(const E& f, const E& x, int depth, StepScope& s) {
    std::vector<E> fs = f->kind == Kind::Mul ? f->args : std::vector<E>{f};
    size_t best = fs.size();
    int bestScore = 0;
    for (size_t k = 0; k < fs.size(); k++) {
        int score = liate(fs[k], x);
        if (score > bestScore) {
            bestScore = score;
            best = k;
        }
    }
    if (best == fs.size()) return nullptr;
    E u = fs[best];
    std::vector<E> rest = fs;
    rest.erase(rest.begin() + static_cast<long>(best));
    E dv = mul(rest);
    if (fs.size() == 1 && bestScore < 4) return nullptr;
    E v = integ(dv, x, depth + 1);
    if (!v) return nullptr;
    E w = integ(simplify(mul(diff(u, x), v)), x, depth + 1);
    if (!w) return nullptr;
    if (s.active())
        s.rule("integration by parts: u = " + stepExpr(u) + ", dv = " + stepExprParen(dv) + " d" + stepExpr(x) +
               ", v = " + stepExpr(simplify(v)));
    return sub(mul(u, v), w);
}

bool hasExpandableFactor(const E& f) {
    if (f->kind != Kind::Mul) return f->kind == Kind::Pow && f->args[0]->kind == Kind::Add && isInteger(f->args[1]) &&
                                      f->args[1]->q.sign() > 0;
    for (const auto& a : f->args) {
        if (a->kind == Kind::Add) return true;
        if (a->kind == Kind::Pow && a->args[0]->kind == Kind::Add && isInteger(a->args[1]) && a->args[1]->q.sign() > 0)
            return true;
    }
    return false;
}

E integCore(const E& f, const E& x, int depth, StepScope& s) {
    if (freeOf(f, x)) {
        s.rule("constant rule");
        return mul(f, x);
    }
    if (f == x) {
        s.rule("power rule: ∫ x^n dx = x^(n+1)/(n+1)");
        return div(pow(x, num(2)), num(2));
    }
    if (f->kind == Kind::Add) {
        std::vector<E> parts;
        for (const auto& t : f->args) {
            E r = integ(t, x, depth + 1);
            if (!r) return nullptr;
            parts.push_back(r);
        }
        s.rule("sum rule: integrate term by term");
        return add(parts);
    }
    if (f->kind == Kind::Mul) {
        std::vector<E> constant, dep;
        for (const auto& a : f->args) (freeOf(a, x) ? constant : dep).push_back(a);
        if (!constant.empty()) {
            E r = integ(mul(dep), x, depth);
            if (!r) return nullptr;
            s.rule("constant multiple: ∫ c f dx = c ∫ f dx");
            return mul(mul(constant), r);
        }
    }
    E a, b;
    auto linearNote = [&](const std::string& rule) {
        s.rule(isOne(a) ? rule : rule + ", divided by the inner derivative " + stepExpr(a));
    };
    if (f->kind == Kind::Pow) {
        const E& base = f->args[0];
        const E& n = f->args[1];
        if (freeOf(n, x) && linearIn(base, x, a, b)) {
            if (n == minusOne()) {
                linearNote("∫ 1/u du = ln(u)");
                return div(fn("ln", base), a);
            }
            linearNote("power rule: ∫ u^n du = u^(n+1)/(n+1)");
            E n1 = add(n, one());
            return div(pow(base, n1), mul(n1, a));
        }
        if (freeOf(base, x) && linearIn(n, x, a, b)) {
            if (base == eConst()) {
                linearNote("∫ e^u du = e^u");
                return div(f, a);
            }
            linearNote("∫ a^u du = a^u/ln(a)");
            return div(f, mul(a, fn("ln", base)));
        }
        if (freeOf(n, x) && (n == half() || n == neg(half()))) {
            if (E r = quadraticRoot(base, n, x)) {
                s.rule("standard integral of a square root of a quadratic");
                return r;
            }
        }
        if (isInteger(n) && (isFnOf(base, "cos") || isFnOf(base, "sin")) && linearIn(base->args[0], x, a, b)) {
            const E& u = base->args[0];
            if (n == num(-2)) {
                linearNote(base->name == "cos" ? "∫ sec(u)^2 du = tan(u)" : "∫ csc(u)^2 du = -cot(u)");
                return base->name == "cos" ? div(fn("tan", u), a) : neg(div(pow(fn("tan", u), minusOne()), a));
            }
            if (n == minusOne()) {
                linearNote(base->name == "cos" ? "∫ sec(u) du = ln((1 + sin(u))/cos(u))"
                                               : "∫ csc(u) du = ln((1 - cos(u))/sin(u))");
                if (base->name == "cos") return div(fn("ln", div(add(one(), fn("sin", u)), fn("cos", u))), a);
                return div(fn("ln", div(sub(one(), fn("cos", u)), fn("sin", u))), a);
            }
        }
    }
    if (f->kind == Kind::Fn && f->args.size() == 1 && linearIn(f->args[0], x, a, b)) {
        if (E r = tableFn(f->name, f->args[0])) {
            if (s.active()) {
                E u = sym("u");
                E entry = tableFn(f->name, u);
                linearNote("table: ∫ " + f->name + "(u) du = " + (entry ? stepExpr(entry) : "?"));
            }
            return div(r, a);
        }
    }
    if (f->kind == Kind::Mul && f->args.size() == 2) {
        // tan(u)^2 style products are handled through rewriting below; sec·tan:
        const E &p = f->args[0], &q = f->args[1];
        if (isFnOf(p, "sin") && q->kind == Kind::Pow && isFnOf(q->args[0], "cos") && q->args[1] == num(-2) &&
            p->args[0] == q->args[0]->args[0] && linearIn(p->args[0], x, a, b)) {
            linearNote("∫ sin(u)/cos(u)^2 du = 1/cos(u)");
            return div(pow(q->args[0], minusOne()), a);
        }
    }
    size_t before = s.mark();
    if (E r = rationalIntegral(f, x, depth)) {
        if (s.active()) {
            E parts;
            try {
                parts = apart(f, x);
            } catch (const CasError&) {
            }
            s.rule(parts && parts != f ? "partial fractions: " + stepExpr(parts) : "rational function: standard forms");
        }
        return r;
    }
    s.rollback(before);
    if (E r = expTrig(f, x)) {
        s.rule("∫ e^(au) sin/cos(bu): standard result of integrating by parts twice");
        return r;
    }
    {
        bool changed = false;
        E reduced = powerReduce(f, changed);
        if (changed) {
            E ex = expand(reduced);
            if (E r = integ(ex, x, depth + 1)) {
                if (s.active()) s.rule("power-reduction identity: " + stepExpr(ex));
                return r;
            }
        }
    }
    if (E p2s = productToSum(f)) {
        if (E r = integ(p2s, x, depth + 1)) {
            if (s.active()) s.rule("product-to-sum identity: " + stepExpr(p2s));
            return r;
        }
    }
    if (hasExpandableFactor(f)) {
        E ex = expand(f);
        if (ex != f) {
            if (E r = integ(ex, x, depth + 1)) {
                s.rule("expand the integrand");
                return r;
            }
        }
    }
    if (E r = uSubstitution(f, x, depth, s)) return r;
    if (E r = byParts(f, x, depth, s)) return r;
    return nullptr;
}

E integ(const E& f, const E& x, int depth) {
    if (depth > kMaxDepth) return nullptr;
    StepScope s(StepKind::Integrate);
    if (s.active()) s.head("∫ " + stepExprParen(f) + " d" + stepExpr(x));
    E r = integCore(f, x, depth, s);
    if (r) s.done(r);
    return r;
}

}  // namespace

E integrate(const E& e, const E& x) {
    if (!isSymbol(x) || isConstantSymbol(x)) throw CasError("integrate: the variable must be a symbol");
    E r = integ(e, x, 0);
    if (!r) return nullptr;
    E s = simplify(r);
    return countNodes(s) <= countNodes(r) ? s : r;
}

E integrateDefinite(const E& e, const E& x, const E& a, const E& b) {
    StepScope s(StepKind::Integrate);
    if (s.active()) {
        s.head("∫ from " + stepExpr(a) + " to " + stepExpr(b) + " of " + stepExprParen(e) + " d" + stepExpr(x));
        s.rule("fundamental theorem of calculus");
    }
    E F = integrate(e, x);
    if (!F) return nullptr;
    E Fb = limit(F, x, b), Fa = limit(F, x, a);
    if (s.active())
        stepNote(StepKind::Integrate, "F(" + stepExpr(b) + ") - F(" + stepExpr(a) + ") = " + stepExprParen(Fb) +
                                          " - " + stepExprParen(Fa));
    E r = simplify(sub(Fb, Fa));
    s.done(r);
    return r;
}

}  // namespace cas
