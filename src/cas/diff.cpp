#include <cmath>

#include "cas/expr.h"
#include "cas/steps.h"

namespace cas {

namespace {

E fnDerivative(const std::string& name, const E& u) {
    if (name == "sin") return fn("cos", u);
    if (name == "cos") return neg(fn("sin", u));
    if (name == "tan") return add(one(), pow(fn("tan", u), num(2)));
    if (name == "asin") return pow(sub(one(), pow(u, num(2))), neg(half()));
    if (name == "acos") return neg(pow(sub(one(), pow(u, num(2))), neg(half())));
    if (name == "atan") return pow(add(one(), pow(u, num(2))), minusOne());
    if (name == "sinh") return fn("cosh", u);
    if (name == "cosh") return fn("sinh", u);
    if (name == "tanh") return sub(one(), pow(fn("tanh", u), num(2)));
    if (name == "asinh") return pow(add(pow(u, num(2)), one()), neg(half()));
    if (name == "acosh") return pow(sub(pow(u, num(2)), one()), neg(half()));
    if (name == "atanh") return pow(sub(one(), pow(u, num(2))), minusOne());
    if (name == "ln") return pow(u, minusOne());
    if (name == "abs") return div(u, fn("abs", u));
    if (name == "erf") return mul(div(num(2), sqrt(piConst())), pow(eConst(), neg(pow(u, num(2)))));
    if (name == "erfc") return neg(mul(div(num(2), sqrt(piConst())), pow(eConst(), neg(pow(u, num(2))))));
    if (name == "floor" || name == "ceil" || name == "sign") return zero();
    throw CasError("don't know how to differentiate " + name);
}

}  // namespace

namespace {

E diffCore(const E& e, const E& x, StepScope& s) {
    switch (e->kind) {
        case Kind::Add: {
            s.rule("sum rule");
            std::vector<E> terms;
            for (const auto& t : e->args) terms.push_back(diff(t, x));
            return add(terms);
        }
        case Kind::Mul: {
            size_t dependent = 0;
            for (const auto& f : e->args)
                if (!freeOf(f, x)) dependent++;
            s.rule(dependent == 1 ? "constant multiple" : "product rule: (uv)' = u'v + uv'");
            std::vector<E> terms;
            for (size_t i = 0; i < e->args.size(); i++) {
                if (freeOf(e->args[i], x)) continue;
                std::vector<E> fs = e->args;
                fs[i] = diff(e->args[i], x);
                terms.push_back(mul(fs));
            }
            return add(terms);
        }
        case Kind::Pow: {
            const E& b = e->args[0];
            const E& n = e->args[1];
            if (freeOf(n, x)) {
                s.rule(b == x ? "power rule: (x^n)' = n x^(n-1)" : "power rule with the chain rule");
                return mul({n, pow(b, sub(n, one())), diff(b, x)});
            }
            if (freeOf(b, x)) {
                if (b == eConst())
                    s.rule(n == x ? "(e^x)' = e^x" : "exponential with the chain rule: (e^u)' = e^u u'");
                else
                    s.rule("exponential rule: (a^u)' = a^u ln(a) u'");
                return mul({e, fn("ln", b), diff(n, x)});
            }
            s.rule("logarithmic differentiation: (f^g)' = f^g (g' ln f + g f'/f)");
            return mul(e, add(mul(diff(n, x), fn("ln", b)), mul({n, diff(b, x), pow(b, minusOne())})));
        }
        case Kind::Fn: {
            if (e->args.size() != 1) throw CasError("don't know how to differentiate " + e->name);
            E d = fnDerivative(e->name, e->args[0]);
            if (s.active()) {
                E u = sym("u");
                std::string table = e->name + "'(u) = " + stepExpr(fnDerivative(e->name, u));
                s.rule(e->args[0] == x ? table : "chain rule, " + table);
            }
            return mul(d, diff(e->args[0], x));
        }
        case Kind::Eq: return eq(diff(e->args[0], x), diff(e->args[1], x));
        default: return zero();
    }
}

}  // namespace

E diff(const E& e, const E& x) {
    if (e == x) return one();
    if (freeOf(e, x)) return zero();
    StepScope s(StepKind::Diff);
    if (s.active()) s.head("d/d" + stepExpr(x) + " " + (e->kind == Kind::Fn ? stepExpr(e) : "(" + stepExpr(e) + ")"));
    E r = diffCore(e, x, s);
    s.done(r);
    return r;
}

E taylor(const E& e, const E& x, const E& a, int order) {
    if (order < 0 || order > 60) throw CasError("taylor: order must be between 0 and 60");
    std::vector<E> terms;
    E d = e;
    E factorial = one();
    for (int k = 0; k <= order; k++) {
        if (k > 0) {
            d = simplify(diff(d, x));
            factorial = mul(factorial, num(k));
        }
        E c = simplify(subs(d, x, a));
        if (isZero(c)) continue;
        terms.push_back(mul({c, pow(factorial, minusOne()), pow(sub(x, a), num(k))}));
    }
    return add(terms);
}

// ---------------------------------------------------------------- limits

namespace {

bool isInfinite(const E& e, int& sign) {
    if (isNum(e) && !e->exact && std::isinf(e->d)) {
        sign = e->d > 0 ? 1 : -1;
        return true;
    }
    return false;
}

bool numericAt(const E& e, const E& x, double v, double& out) {
    std::complex<double> z;
    Env env = [&](const std::string& name, std::complex<double>& val) {
        if (name != x->name) return false;
        val = v;
        return true;
    };
    if (!evalComplex(e, env, z)) return false;
    if (std::fabs(z.imag()) > 1e-9 * std::max(1.0, std::abs(z))) return false;
    out = z.real();
    return true;
}

// Snaps a floating-point limit to a nearby simple fraction when it is clearly one.
E snap(double v) {
    if (std::isinf(v)) return real(v);
    for (int64_t den = 1; den <= 64; den++) {
        double n = std::round(v * static_cast<double>(den));
        if (std::fabs(n / static_cast<double>(den) - v) < 1e-7 * std::max(1.0, std::fabs(v)))
            return num(Rational(BigInt::fromDouble(n), BigInt(den)));
    }
    // Simple multiples of π, as from atan or acos at the boundary.
    for (int64_t den = 1; den <= 12; den++) {
        double n = std::round(v / M_PI * static_cast<double>(den));
        if (n != 0 && std::fabs(n * M_PI / static_cast<double>(den) - v) < 1e-7 * std::max(1.0, std::fabs(v)))
            return mul(num(Rational(BigInt::fromDouble(n), BigInt(den))), piConst());
    }
    return real(v);
}

E numericLimit(const E& e, const E& x, double a, int side) {
    double prev = NAN, cur = NAN;
    bool ok = false;
    for (int k = 3; k <= 9; k++) {
        double h = std::pow(10.0, -k) * std::max(1.0, std::fabs(a));
        double v;
        if (!numericAt(e, x, a + side * h, v)) continue;
        prev = cur;
        cur = v;
        ok = true;
    }
    if (!ok) throw CasError("limit: could not evaluate the expression near the point");
    if (std::isfinite(cur) && std::isfinite(prev) && std::fabs(cur - prev) > 1e-3 * std::max(1.0, std::fabs(cur))) {
        if (std::fabs(cur) > 1e6 && std::fabs(cur) > 10 * std::fabs(prev)) return real(cur > 0 ? INFINITY : -INFINITY);
        throw CasError("limit: the expression does not seem to converge");
    }
    return snap(cur);
}

E limitAt(const E& e, const E& x, const E& a, int side, int depth);

E directValue(const E& e, const E& x, const E& a, bool& ok) {
    ok = false;
    try {
        E v = simplify(subs(e, x, a));
        std::complex<double> z;
        if (freeOf(v, x) && (!evalComplex(v, nullptr, z) || std::isfinite(z.real()))) {
            ok = true;
            return v;
        }
    } catch (const CasError&) {
    }
    return nullptr;
}

E limitCore(const E& e, const E& x, const E& a, int side, int depth, StepScope& s) {
    bool ok;
    E v = directValue(e, x, a, ok);
    if (ok) {
        s.rule("direct substitution");
        return v;
    }
    // Indeterminate powers (1^∞, 0^0, ∞^0): lim f^g = e^(lim g ln f).
    if (depth < 8 && e->kind == Kind::Pow && !freeOf(e->args[0], x) && !freeOf(e->args[1], x)) {
        try {
            E l = limitAt(simplify(mul(e->args[1], fn("ln", e->args[0]))), x, a, side, depth + 1);
            s.rule("indeterminate power: lim f^g = e^(lim g ln f)");
            int sg;
            if (isInfinite(l, sg)) return sg > 0 ? l : zero();
            return simplify(pow(eConst(), l));
        } catch (const CasError&) {
        }
    }
    E n, d;
    numDen(together(e), n, d);
    if (depth < 8 && !isOne(d)) {
        bool okn, okd;
        E nv = directValue(n, x, a, okn), dv = directValue(d, x, a, okd);
        if (okn && okd && !isZero(dv)) {
            s.rule("combine into one fraction " + stepExprParen(n) + "/" + stepExprParen(d) + ", then substitute");
            return simplify(div(nv, dv));
        }
        if (okn && okd && isZero(nv) && isZero(dv)) {
            E dn = diff(n, x), dd = diff(d, x);
            s.rule("0/0 form: L'Hôpital's rule");
            if (s.active()) {
                stepNote(StepKind::Limit, "numerator' = " + stepExpr(simplify(dn)));
                stepNote(StepKind::Limit, "denominator' = " + stepExpr(simplify(dd)));
            }
            E ratio = simplify(div(dn, dd));
            return limitAt(ratio, x, a, side, depth + 1);
        }
        if (okn && okd && isZero(dv) && !isZero(nv)) {
            s.rule("nonzero/0: the expression grows without bound; estimated numerically");
            double av = numValue(evalf(a));
            E l = numericLimit(e, x, av, side == 0 ? 1 : side);
            if (side == 0) {
                E r = numericLimit(e, x, av, -1);
                if (isNum(l) && isNum(r) && numValue(l) != numValue(r))
                    throw CasError("limit: the one-sided limits differ (" + toString(l) + " and " + toString(r) + ")");
            }
            return l;
        }
    }
    std::complex<double> az;
    if (!evalComplex(a, nullptr, az)) throw CasError("limit: the point must be numeric");
    s.rule("no exact rule applies; estimated numerically");
    if (side != 0) return numericLimit(e, x, az.real(), side);
    // When the expression is only defined on one side (ln x at 0), that side decides.
    E l, r;
    try {
        l = numericLimit(e, x, az.real(), 1);
    } catch (const CasError&) {
        return numericLimit(e, x, az.real(), -1);
    }
    try {
        r = numericLimit(e, x, az.real(), -1);
    } catch (const CasError&) {
        return l;
    }
    if (isNum(l) && isNum(r) && std::fabs(numValue(l) - numValue(r)) > 1e-6 * std::max(1.0, std::fabs(numValue(l))))
        throw CasError("limit: the one-sided limits differ (" + toString(l) + " and " + toString(r) + ")");
    return l;
}

std::string limitHead(const E& e, const E& x, const E& a, int side) {
    std::string point = stepExpr(a);
    int sg;
    if (isInfinite(a, sg)) point = sg > 0 ? "∞" : "-∞";
    if (side > 0) point += "+";
    if (side < 0) point += "-";
    return "lim " + stepExpr(x) + "→" + point + " " + stepExprParen(e);
}

E limitAt(const E& e, const E& x, const E& a, int side, int depth) {
    StepScope s(StepKind::Limit);
    if (s.active()) s.head(limitHead(e, x, a, side));
    E r = limitCore(e, x, a, side, depth, s);
    s.done(r);
    return r;
}

}  // namespace

E limit(const E& e, const E& x, const E& a) {
    int sign;
    if (isInfinite(a, sign)) {
        StepScope s(StepKind::Limit);
        if (s.active()) {
            s.head(limitHead(e, x, a, 0));
            s.rule(std::string("substitute ") + stepExpr(x) + " = " + (sign > 0 ? "" : "-") + "1/t, t → 0+");
        }
        E t = sym("_t");
        E g = simplify(subs(e, x, div(num(sign), t)));
        E r = limitAt(g, t, zero(), 1, 0);
        s.done(r);
        return r;
    }
    return limitAt(e, x, a, 0, 0);
}

}  // namespace cas
