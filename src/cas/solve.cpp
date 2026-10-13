#include <algorithm>
#include <cmath>
#include <functional>

#include "cas/expr.h"
#include "cas/poly.h"
#include "cas/steps.h"

namespace cas {

namespace {

using C = std::complex<double>;

bool noting() { return stepsWant(StepKind::Solve); }
void note(const std::string& text) { stepNote(StepKind::Solve, text); }
std::string eqText(const E& lhs, const E& rhs) { return stepExpr(lhs) + " = " + stepExpr(rhs); }

std::string rootList(const std::vector<E>& rs, const E& x) {
    if (rs.empty()) return "no solutions";
    std::string s;
    for (size_t i = 0; i < rs.size(); i++) s += (i ? ", " : "") + eqText(x, rs[i]);
    return s;
}

E lhsMinusRhs(const E& e) { return e->kind == Kind::Eq ? sub(e->args[0], e->args[1]) : e; }

void addUnique(std::vector<E>& out, const E& v) {
    for (const auto& o : out)
        if (o == v) return;
    out.push_back(v);
}

E complexToExpr(const C& z) {
    if (z.imag() == 0) return real(z.real());
    if (z.real() == 0) return mul(real(z.imag()), imagUnit());
    return add(real(z.real()), mul(real(z.imag()), imagUnit()));
}

std::vector<E> numericPolyRoots(const std::vector<E>& coeffs) {
    std::vector<C> c;
    for (const auto& k : coeffs) {
        C z;
        if (!evalComplex(k, nullptr, z)) throw CasError("solve: polynomial coefficients must be numeric");
        c.push_back(z);
    }
    std::vector<E> out;
    for (const auto& r : numericRoots(c)) addUnique(out, complexToExpr(r));
    return out;
}

std::vector<E> quadratic(const E& a, const E& b, const E& c) {
    E disc = simplify(sub(pow(b, num(2)), mul({num(4), a, c})));
    E twoA = mul(num(2), a);
    if (isZero(disc)) return {simplify(div(neg(b), twoA))};
    E s = sqrt(disc);
    return {simplify(div(sub(neg(b), s), twoA)), simplify(div(add(neg(b), s), twoA))};
}

std::vector<E> solveRationalFactor(const Poly& p) {
    int deg = p.degree();
    std::vector<E> c;
    for (const auto& r : p.c) c.push_back(num(r));
    if (deg == 1) return {num(-p.c[0] / p.c[1])};
    if (deg == 2) {
        if (noting()) {
            E disc = simplify(sub(pow(c[1], num(2)), mul({num(4), c[2], c[0]})));
            note("quadratic formula on a = " + stepExpr(c[2]) + ", b = " + stepExpr(c[1]) + ", c = " +
                 stepExpr(c[0]) + ": discriminant b^2 - 4ac = " + stepExpr(disc));
        }
        return quadratic(c[2], c[1], c[0]);
    }
    if (deg == 4 && p.c[1].isZero() && p.c[3].isZero()) {
        if (noting()) note("biquadratic: solve the quadratic in y = x^2, then take square roots");
        std::vector<E> out;
        for (const auto& y : quadratic(c[4], c[2], c[0])) {
            E r = simplify(sqrt(y));
            addUnique(out, simplify(neg(r)));
            addUnique(out, r);
        }
        return out;
    }
    if (deg == 3) {
        // Depressed cubic t^3 + p t + q with x = t - b/(3a).
        Rational a = p.c[3], b = p.c[2], cc = p.c[1], d = p.c[0];
        Rational pp = (Rational(3) * a * cc - b * b) / (Rational(3) * a * a);
        Rational qq = (Rational(2) * b * b * b - Rational(9) * a * b * cc + Rational(27) * a * a * d) /
                      (Rational(27) * a * a * a);
        Rational D = qq * qq / Rational(4) + pp * pp * pp / Rational(27);
        if (D.sign() > 0) {
            if (noting())
                note("cubic: depressed form t^3 + (" + stepExpr(num(pp)) + ")t + (" + stepExpr(num(qq)) +
                     ") = 0; one real root by Cardano's formula, the complex pair numerically");
            E sD = sqrt(num(D));
            E halfQ = num(-qq / Rational(2));
            E t = add(pow(add(halfQ, sD), num(Rational(BigInt(1), BigInt(3)))),
                      pow(sub(halfQ, sD), num(Rational(BigInt(1), BigInt(3)))));
            E realRoot = simplify(sub(t, num(b / (Rational(3) * a))));
            std::vector<E> out{realRoot};
            for (const auto& r : numericPolyRoots(c))
                if (isInexact(r) && r->kind != Kind::Num) out.push_back(r);
            return out;
        }
    }
    if (noting()) note("no closed form used for this degree " + std::to_string(deg) + " factor; roots found numerically");
    return numericPolyRoots(c);
}

bool allRational(const std::vector<E>& c) {
    for (const auto& k : c)
        if (!isRationalNum(k)) return false;
    return true;
}

bool allNumeric(const std::vector<E>& c) {
    for (const auto& k : c)
        if (!freeSymbols(k).empty()) return false;
    return true;
}

std::vector<E> solvePolynomial(const std::vector<E>& cIn, const E& x) {
    std::vector<E> c;
    for (const auto& k : cIn) c.push_back(simplify(k));
    while (!c.empty() && isZero(c.back())) c.pop_back();
    int deg = static_cast<int>(c.size()) - 1;
    if (deg < 0) throw CasError("solve: every value of " + toString(x) + " is a solution");
    if (deg == 0) return {};
    if (deg == 1) {
        E r = simplify(div(neg(c[0]), c[1]));
        if (noting()) note("linear: " + eqText(x, r));
        return {r};
    }
    if (noting()) note("polynomial of degree " + std::to_string(deg) + " in " + stepExpr(x));
    if (allRational(c)) {
        std::vector<Rational> q;
        for (const auto& k : c) q.push_back(k->q);
        Rational unit;
        std::vector<PolyFactor> fs = factorPoly(Poly(q), unit);
        if (noting() && !fs.empty() && (fs.size() > 1 || fs[0].mult > 1)) {
            std::vector<E> parts{num(unit)};
            for (const auto& f : fs) parts.push_back(pow(fromPoly(f.p, x), num(f.mult)));
            note("factor: " + stepExpr(mul(parts)) + " = 0");
        }
        std::vector<E> out;
        for (const auto& f : fs) {
            bool linear = f.p.degree() == 1;
            std::string eq0 = noting() ? stepExpr(fromPoly(f.p, x)) + " = 0" : "";
            if (noting() && !linear && fs.size() > 1) note("solve " + eq0);
            std::vector<E> rs = solveRationalFactor(f.p);
            if (noting() && (linear || fs.size() > 1))
                note(linear ? eq0 + " gives " + rootList(rs, x) : "  " + rootList(rs, x));
            for (const auto& r : rs) addUnique(out, r);
        }
        return out;
    }
    bool inexact = false;
    for (const auto& k : c) inexact = inexact || isInexact(k);
    if (allNumeric(c) && (inexact || deg > 2)) {
        if (deg == 2 && !inexact) {
            if (noting()) note("quadratic formula: x = (-b ± sqrt(b^2 - 4ac))/(2a)");
            return quadratic(c[2], c[1], c[0]);
        }
        if (noting()) note("roots found numerically");
        return numericPolyRoots(c);
    }
    if (deg == 2) {
        if (noting())
            note("quadratic formula with a = " + stepExpr(c[2]) + ", b = " + stepExpr(c[1]) + ", c = " +
                 stepExpr(c[0]));
        return quadratic(c[2], c[1], c[0]);
    }
    if (deg == 4 && isZero(c[1]) && isZero(c[3])) {
        if (noting()) note("biquadratic: solve the quadratic in y = x^2, then take square roots");
        std::vector<E> out;
        for (const auto& y : quadratic(c[4], c[2], c[0])) {
            E r = simplify(sqrt(y));
            addUnique(out, simplify(neg(r)));
            addUnique(out, r);
        }
        return out;
    }
    throw CasError("solve: cannot solve polynomials of degree " + std::to_string(deg) + " with symbolic coefficients");
}

void solveExpr(const E& f, const E& x, std::vector<E>& out, int depth);

void invert(const E& t, const E& rhs, const E& x, std::vector<E>& out, int depth) {
    if (depth > 12) throw CasError("solve: equation is too deeply nested");
    if (t == x) {
        E r = simplify(rhs);
        if (noting()) note(eqText(x, r));
        addUnique(out, r);
        return;
    }
    if (noting()) note(eqText(t, simplify(rhs)));
    switch (t->kind) {
        case Kind::Mul: {
            std::vector<E> freeF, dep;
            for (const auto& f : t->args) (freeOf(f, x) ? freeF : dep).push_back(f);
            if (dep.size() == 1) return invert(dep[0], div(rhs, mul(freeF)), x, out, depth + 1);
            return solveExpr(sub(t, rhs), x, out, depth + 1);
        }
        case Kind::Add: return solveExpr(sub(t, rhs), x, out, depth + 1);
        case Kind::Pow: {
            const E& b = t->args[0];
            const E& n = t->args[1];
            if (freeOf(n, x)) {
                if (isInteger(n) && n->q.num.isEven() && n->q.sign() > 0) {
                    if (noting()) note("take the " + stepExpr(n) + "th root of both sides (±)");
                    E r = simplify(pow(rhs, div(one(), n)));
                    invert(b, neg(r), x, out, depth + 1);
                    invert(b, r, x, out, depth + 1);
                    return;
                }
                if (noting()) note("raise both sides to the power " + stepExprParen(simplify(div(one(), n))));
                return invert(b, pow(rhs, div(one(), n)), x, out, depth + 1);
            }
            if (freeOf(b, x)) {
                if (noting()) note(b == eConst() ? "take ln of both sides" : "take logarithms base " + stepExpr(b));
                E l = b == eConst() ? fn("ln", rhs) : div(fn("ln", rhs), fn("ln", b));
                return invert(n, l, x, out, depth + 1);
            }
            break;
        }
        case Kind::Fn: {
            const E& u = t->args[0];
            const std::string& nm = t->name;
            std::vector<E> values;
            if (nm == "sin") values = {fn("asin", rhs), sub(piConst(), fn("asin", rhs))};
            else if (nm == "cos") values = {fn("acos", rhs), neg(fn("acos", rhs))};
            else if (nm == "tan") values = {fn("atan", rhs)};
            else if (nm == "ln") values = {pow(eConst(), rhs)};
            else if (nm == "asin") values = {fn("sin", rhs)};
            else if (nm == "acos") values = {fn("cos", rhs)};
            else if (nm == "atan") values = {fn("tan", rhs)};
            else if (nm == "sinh") values = {fn("asinh", rhs)};
            else if (nm == "cosh") values = {fn("acosh", rhs), neg(fn("acosh", rhs))};
            else if (nm == "tanh") values = {fn("atanh", rhs)};
            else if (nm == "abs") values = {rhs, neg(rhs)};
            else break;
            if (noting()) {
                if (nm == "abs") note("|u| = c gives u = c or u = -c");
                else if (nm == "sin" || nm == "cos") note("invert " + nm + " (principal solutions)");
                else note("invert " + nm);
            }
            for (const auto& v : values) invert(u, simplify(v), x, out, depth + 1);
            return;
        }
        default: break;
    }
    throw CasError("solve: cannot isolate " + toString(x) + " in " + toString(t));
}

// Replaces g (and exponentials b^(m*inner) when g = b^inner) by u.
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

void collectCandidates(const E& e, const E& x, std::vector<E>& out) {
    if (freeOf(e, x)) return;
    if ((e->kind == Kind::Fn || (e->kind == Kind::Pow && !freeOf(e->args[1], x)) ||
         (e->kind == Kind::Pow && !isInteger(e->args[1]))) &&
        e != x)
        addUnique(out, e);
    for (const auto& a : e->args) collectCandidates(a, x, out);
}

void solveExpr(const E& fIn, const E& x, std::vector<E>& out, int depth) {
    if (depth > 12) throw CasError("solve: equation is too complicated");
    E n, d;
    numDen(together(fIn), n, d);
    if (noting() && !freeOf(d, x)) note("multiply by the denominator " + stepExprParen(d) + ": " + stepExpr(n) + " = 0");
    n = expand(n);
    if (freeOf(n, x)) {
        if (isZero(simplify(n))) throw CasError("solve: every value of " + toString(x) + " is a solution");
        if (noting()) note(stepExpr(n) + " = 0 is never true: no solutions");
        return;
    }
    std::vector<E> coeffs;
    if (toPolyE(n, x, coeffs)) {
        if (noting() && n != fIn && freeOf(d, x)) note("expand: " + stepExpr(n) + " = 0");
        for (const auto& r : solvePolynomial(coeffs, x)) addUnique(out, r);
        return;
    }
    E fac = factor(n);
    if (fac->kind == Kind::Mul) {
        if (noting()) note("factor: " + stepExpr(fac) + " = 0, so one of the factors is zero");
        bool any = false;
        for (const auto& f : fac->args) {
            if (freeOf(f, x)) continue;
            E ex;
            E base = splitPow(f, ex);
            if (isNum(ex) && numValue(ex) > 0) {
                solveExpr(base, x, out, depth + 1);
                any = true;
            }
        }
        if (any) return;
    }
    if (n->kind == Kind::Add) {
        std::vector<E> dep, rest;
        for (const auto& t : n->args) (freeOf(t, x) ? rest : dep).push_back(t);
        if (dep.size() == 1) return invert(dep[0], neg(add(rest)), x, out, depth + 1);
    } else {
        return invert(n, zero(), x, out, depth + 1);
    }
    // Radical equations: isolate a square root and square both sides. solve() later drops the
    // extraneous roots that squaring introduces.
    for (const auto& t : n->args) {
        bool radical = false;
        auto isRoot = [&](const E& f) { return f->kind == Kind::Pow && f->args[1] == half() && !freeOf(f, x); };
        if (isRoot(t)) radical = true;
        if (t->kind == Kind::Mul)
            for (const auto& f : t->args) radical = radical || isRoot(f);
        if (!radical) continue;
        E rest = simplify(neg(sub(n, t)));
        if (noting()) {
            note("isolate the square root: " + eqText(t, rest));
            note("square both sides (this can add extraneous roots): " + eqText(simplify(expand(pow(t, num(2)))),
                                                                              simplify(expand(pow(rest, num(2))))));
        }
        return solveExpr(sub(expand(pow(t, num(2))), expand(pow(rest, num(2)))), x, out, depth + 1);
    }
    std::vector<E> candidates;
    collectCandidates(n, x, candidates);
    E u = sym("_u");
    for (const auto& g : candidates) {
        E replaced = replaceSub(n, g, u);
        if (!freeOf(replaced, x)) continue;
        std::vector<E> us;
        size_t mark = stepsMark();
        if (noting()) note("substitute u = " + stepExpr(g) + ": " + stepExpr(replaced) + " = 0");
        try {
            solveExpr(replaced, u, us, depth + 1);
        } catch (const CasError&) {
            stepsRollback(mark);
            continue;
        }
        if (noting()) note("back-substitute " + rootList(us, u) + " into u = " + stepExpr(g));
        for (const auto& r : us) {
            try {
                invert(g, r, x, out, depth + 1);
            } catch (const CasError&) {
            }
        }
        return;
    }
    throw CasError("solve: cannot solve this equation symbolically; try root(f, a, b) for a numeric solution");
}

bool numericZero(const E& f, const E& x, const E& r) {
    C v;
    C rv;
    if (!evalComplex(r, nullptr, rv)) return true;  // symbolic roots cannot be checked numerically
    Env env = [&](const std::string& name, C& out) {
        if (name != x->name) return false;
        out = rv;
        return true;
    };
    if (!evalComplex(f, env, v)) return true;
    return std::abs(v) <= 1e-8 * std::max(1.0, std::abs(rv));
}

}  // namespace

std::vector<E> solve(const E& equation, const E& x) {
    if (!isSymbol(x) || isConstantSymbol(x)) throw CasError("solve: the unknown must be a symbol");
    StepScope s(StepKind::Solve);
    if (s.active()) {
        s.head("solve " + stepExpr(equation) + " for " + stepExpr(x));
        if (equation->kind == Kind::Eq && !isZero(equation->args[1]))
            note("move everything to one side: " + stepExpr(simplify(lhsMinusRhs(equation))) + " = 0");
    }
    E f = lhsMinusRhs(equation);
    std::vector<E> raw;
    solveExpr(f, x, raw, 0);
    E n, d;
    numDen(together(f), n, d);
    std::vector<E> out;
    for (const auto& r : raw) {
        bool ok = true;
        const char* why = "it does not satisfy the original equation";
        try {
            E dv = simplify(subs(d, x, r));
            if (isZero(dv)) {
                ok = false;
                why = "it makes a denominator zero";
            }
        } catch (const CasError&) {
            ok = false;
            why = "the equation is undefined there";
        }
        if (ok && !numericZero(f, x, r)) ok = false;
        if (ok) addUnique(out, r);
        else if (s.active()) note("reject " + eqText(x, r) + ": " + why);
    }
    std::sort(out.begin(), out.end(), [](const E& a, const E& b) {
        C za, zb;
        bool ea = evalComplex(a, nullptr, za), eb = evalComplex(b, nullptr, zb);
        if (ea && eb) {
            bool ra = std::fabs(za.imag()) < 1e-12, rb = std::fabs(zb.imag()) < 1e-12;
            if (ra != rb) return ra;
            if (za.real() != zb.real()) return za.real() < zb.real();
            return za.imag() < zb.imag();
        }
        if (ea != eb) return ea;
        return compare(a, b) < 0;
    });
    if (s.active()) s.doneText(out.empty() ? "no solutions" : rootList(out, x));
    return out;
}

// ---------------------------------------------------------------- systems

namespace {

bool linearSystem(const std::vector<E>& fs, const std::vector<E>& vars, std::vector<Solution>& result) {
    size_t m = fs.size(), n = vars.size();
    std::vector<std::vector<E>> a(m, std::vector<E>(n + 1));
    for (size_t i = 0; i < m; i++) {
        E f = expand(fs[i]);
        E rest = f;
        for (size_t j = 0; j < n; j++) {
            E c = diff(f, vars[j]);
            for (const auto& v : vars)
                if (!freeOf(c, v)) return false;
            a[i][j] = simplify(c);
            rest = subs(rest, vars[j], zero());
        }
        a[i][n] = simplify(neg(rest));
    }
    std::vector<size_t> pivotCol;
    size_t r = 0;
    for (size_t c = 0; c < n && r < m; c++) {
        size_t p = m;
        for (size_t i = r; i < m; i++)
            if (!isZero(a[i][c])) {
                p = i;
                break;
            }
        if (p == m) continue;
        std::swap(a[p], a[r]);
        E piv = a[r][c];
        for (size_t k = c; k <= n; k++) a[r][k] = simplify(div(a[r][k], piv));
        for (size_t i = 0; i < m; i++) {
            if (i == r || isZero(a[i][c])) continue;
            E f = a[i][c];
            for (size_t k = c; k <= n; k++) a[i][k] = simplify(sub(a[i][k], mul(f, a[r][k])));
        }
        pivotCol.push_back(c);
        r++;
    }
    for (size_t i = r; i < m; i++)
        if (!isZero(a[i][n])) {
            result.clear();
            return true;  // inconsistent: no solutions
        }
    Solution sol;
    for (size_t k = 0; k < pivotCol.size(); k++) {
        size_t c = pivotCol[k];
        E v = a[k][n];
        for (size_t j = 0; j < n; j++) {
            if (j == c || std::find(pivotCol.begin(), pivotCol.end(), j) != pivotCol.end()) continue;
            if (!isZero(a[k][j])) v = sub(v, mul(a[k][j], vars[j]));
        }
        sol.push_back({vars[c], simplify(v)});
    }
    result.push_back(sol);
    return true;
}

void nonlinear(std::vector<E> fs, std::vector<E> vars, Solution partial, std::vector<Solution>& out, int depth) {
    if (depth > 10 || out.size() > 64) return;
    std::vector<E> live;
    for (const auto& f : fs) {
        E s = simplify(f);
        bool hasVar = false;
        for (const auto& v : vars) hasVar = hasVar || !freeOf(s, v);
        if (!hasVar) {
            if (!isZero(s)) {
                C z;
                if (!evalComplex(s, nullptr, z) || std::abs(z) > 1e-9) return;
            }
            continue;
        }
        live.push_back(s);
    }
    if (live.empty()) {
        // Back-substitute later values into earlier ones.
        for (size_t i = partial.size(); i-- > 0;)
            for (size_t j = i + 1; j < partial.size(); j++)
                partial[i].second = simplify(subs(partial[i].second, partial[j].first, partial[j].second));
        out.push_back(partial);
        return;
    }
    std::sort(live.begin(), live.end(), [&](const E& a, const E& b) { return countNodes(a) < countNodes(b); });
    for (size_t k = 0; k < live.size(); k++) {
        for (size_t vi = 0; vi < vars.size(); vi++) {
            const E& v = vars[vi];
            if (freeOf(live[k], v)) continue;
            std::vector<E> roots;
            try {
                roots = solve(live[k], v);
            } catch (const CasError&) {
                continue;
            }
            std::vector<E> restVars = vars;
            restVars.erase(restVars.begin() + static_cast<long>(vi));
            for (const auto& r : roots) {
                std::vector<E> next;
                for (size_t j = 0; j < live.size(); j++)
                    if (j != k) next.push_back(subs(live[j], v, r));
                Solution p = partial;
                p.push_back({v, r});
                nonlinear(next, restVars, p, out, depth + 1);
            }
            return;
        }
    }
    throw CasError("solve: cannot solve this system symbolically");
}

}  // namespace

std::vector<Solution> solveSystem(const std::vector<E>& equations, const std::vector<E>& vars) {
    std::vector<E> fs;
    for (const auto& e : equations) fs.push_back(lhsMinusRhs(e));
    for (const auto& v : vars)
        if (!isSymbol(v) || isConstantSymbol(v)) throw CasError("solve: unknowns must be symbols");
    std::vector<Solution> result;
    if (linearSystem(fs, vars, result)) return result;
    nonlinear(fs, vars, {}, result, 0);
    for (auto& s : result)
        std::sort(s.begin(), s.end(), [](const std::pair<E, E>& a, const std::pair<E, E>& b) {
            return a.first->name < b.first->name;
        });
    return result;
}

}  // namespace cas
