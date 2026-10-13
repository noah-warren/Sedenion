#include "cas/bridge.h"

#include <algorithm>
#include <cmath>

#include "calculus/numeric_calc.h"
#include "cas/poly.h"
#include "cas/rewrite.h"
#include "cas/steps.h"
#include "common/error.h"
#include "linalg/matrix.h"
#include "numeric/ops.h"
#include "sets/set.h"
#include "vm/vm.h"

using cas::E;

namespace {

E fromDoubleComponent(double d) {
    if (std::floor(d) == d && std::fabs(d) < 9.007199254740992e15) return cas::num(static_cast<int64_t>(d));
    return cas::real(d);
}

// Pure complex constants a + b·i with exact rational parts.
bool complexParts(const E& e, Rational& re, Rational& im) {
    auto imagPart = [](const E& t, Rational& out) {
        if (t == cas::imagUnit()) {
            out = Rational(1);
            return true;
        }
        if (t->kind == cas::Kind::Mul && t->args.size() == 2 && cas::isRationalNum(t->args[0]) &&
            t->args[1] == cas::imagUnit()) {
            out = t->args[0]->q;
            return true;
        }
        return false;
    };
    re = Rational(0);
    im = Rational(0);
    if (imagPart(e, im)) return true;
    if (e->kind == cas::Kind::Add && e->args.size() == 2 && cas::isRationalNum(e->args[0]) && imagPart(e->args[1], im)) {
        re = e->args[0]->q;
        return true;
    }
    return false;
}

Value numericValue(const E& e) {
    std::complex<double> z;
    if (!cas::evalComplex(e, nullptr, z)) return newSym(e);
    return fromComplex(z);
}

}  // namespace

E toExpr(const Value& v) {
    switch (numKind(v)) {
        case NumKind::Int:
        case NumKind::Big:
        case NumKind::Rat: return cas::num(toRational(v));
        case NumKind::Real: {
            double d = toDouble(v);
            if (d == M_PI) return cas::piConst();
            if (d == M_E) return cas::eConst();
            return cas::real(d);
        }
        case NumKind::Cplx: {
            std::complex<double> z = toComplex(v);
            return cas::add(fromDoubleComponent(z.real()), cas::mul(fromDoubleComponent(z.imag()), cas::imagUnit()));
        }
        default: break;
    }
    if (isSym(v)) return as<ObjSym>(v)->e;
    if (isQuantity(v)) fail("quantities with units cannot be used in symbolic expressions");
    fail("a value of type " + typeName(v) + " cannot be used in a symbolic expression");
}

Value fromExpr(const E& e) {
    if (cas::isNum(e)) {
        if (e->exact) return fromRational(e->q);
        return Value::real(e->d);
    }
    Rational re, im;
    if (complexParts(e, re, im)) return fromComplex(std::complex<double>(re.toDouble(), im.toDouble()));
    return newSym(e);
}

namespace {

// Floating-point numbers make constant expressions numeric: π + 0.5 is 3.6416..., not symbolic.
Value finish(const E& r) {
    if (cas::isInexact(r) && cas::freeSymbols(r).empty()) return numericValue(r);
    return fromExpr(r);
}

}  // namespace

Value symBinary(BinOp op, const Value& a, const Value& b) {
    E x = toExpr(a), y = toExpr(b);
    try {
        switch (op) {
            case BinOp::Add: return finish(cas::add(x, y));
            case BinOp::Sub: return finish(cas::sub(x, y));
            case BinOp::Mul:
            case BinOp::Dot:
            case BinOp::Cross: return finish(cas::mul(x, y));
            case BinOp::Div: return finish(cas::div(x, y));
            case BinOp::LDiv: return finish(cas::div(y, x));
            case BinOp::Pow: return finish(cas::pow(x, y));
            case BinOp::Mod: {
                double p, q;
                if (symToDouble(a, p) && symToDouble(b, q)) return Value::real(p - q * std::floor(p / q));
                fail("% is not defined for symbolic expressions");
            }
        }
    } catch (const cas::CasError& e) {
        fail(e.what());
    }
    fail("unsupported symbolic operation");
}

Value symEquation(const Value& a, const Value& b) {
    E l = toExpr(a), r = toExpr(b);
    if (l == r) return Value::boolean(true);  // hash-consed: identical expressions
    return newSym(cas::eq(l, r));
}

Value symFunction(const std::string& name, const Value& arg) { return finish(cas::fn(name, toExpr(arg))); }

bool symToDouble(const Value& v, double& out) {
    std::complex<double> z;
    if (!cas::evalComplex(toExpr(v), nullptr, z)) return false;
    if (std::fabs(z.imag()) > 1e-12 * std::max(1.0, std::abs(z))) return false;
    out = z.real();
    return true;
}

bool symTruth(const Value& v) {
    if (v.isBool()) return v.as.b;
    E e = toExpr(v);
    if (e->kind == cas::Kind::Eq) {
        if (e->args[0] == e->args[1]) return true;
        E d = cas::simplify(cas::sub(e->args[0], e->args[1]));
        if (cas::isZero(d)) return true;
        std::complex<double> z;
        if (cas::evalComplex(d, nullptr, z)) return std::abs(z) < 1e-12;
        return false;
    }
    std::complex<double> z;
    if (cas::evalComplex(e, nullptr, z)) return z != std::complex<double>(0);
    fail("cannot decide whether the expression " + cas::toString(e) + " is true");
}

Value simplifyValue(const Value& v) {
    if (!isSym(v)) return v;
    return fromExpr(cas::simplify(as<ObjSym>(v)->e));
}

// ---------------------------------------------------------------- natives

namespace {

[[noreturn]] void rethrow(const cas::CasError& e) { fail(e.what()); }

// Functions passed to CAS routines are applied to a fresh symbol to get an expression.
E exprArg(VM& vm, const Value& v, const E* var) {
    if (isCallable(v)) {
        E x = var ? *var : cas::sym("x");
        return toExpr(vm.call(v, {newSym(x)}));
    }
    return toExpr(v);
}

E symbolArg(const Value& v, const char* fname) {
    if (!isSym(v)) fail(std::string(fname) + ": expected a symbol, got " + toString(v));
    E e = as<ObjSym>(v)->e;
    if (!cas::isSymbol(e) || cas::isConstantSymbol(e)) fail(std::string(fname) + ": expected a symbol, got " + toString(v));
    return e;
}

// The variable of a single-variable expression (or x for functions).
E inferVariable(const E& e, const char* fname) {
    std::vector<E> syms = cas::freeSymbols(e);
    if (syms.size() == 1) return syms[0];
    if (syms.empty()) return cas::sym("x");
    fail(std::string(fname) + ": the expression has several variables; pass the variable explicitly");
}

template <class F>
Value mapSym(const Value& v, F f) {
    if (isList(v) || isMatrix(v)) return mapElements(v, [&](const Value& x) { return mapSym(x, f); });
    if (!isSym(v)) return v;
    try {
        return fromExpr(f(as<ObjSym>(v)->e));
    } catch (const cas::CasError& e) {
        rethrow(e);
    }
}

Value n_simplify(VM&, int, Value* args) {
    return mapSym(args[0], [](const E& e) { return cas::applyAssumptions(cas::simplify(e)); });
}
Value n_expand(VM&, int, Value* args) { return mapSym(args[0], [](const E& e) { return cas::expand(e); }); }
Value n_factor(VM&, int, Value* args) {
    if (isNumber(args[0]) && isIntegerValue(args[0]))
        fail("factor works on expressions; use factorint(n) for integers");
    return mapSym(args[0], [](const E& e) { return cas::factor(e); });
}
Value n_together(VM&, int, Value* args) { return mapSym(args[0], [](const E& e) { return cas::together(e); }); }
Value n_cancel(VM&, int, Value* args) { return mapSym(args[0], [](const E& e) { return cas::cancel(e); }); }

Value n_apart(VM&, int argc, Value* args) {
    if (!isSym(args[0])) return args[0];
    E e = toExpr(args[0]);
    E x = argc > 1 ? symbolArg(args[1], "apart") : inferVariable(e, "apart");
    try {
        return fromExpr(cas::apart(e, x));
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

Value n_subs(VM&, int argc, Value* args) {
    if (!isSym(args[0]) && !isList(args[0]) && !isMatrix(args[0])) return args[0];
    std::vector<std::pair<E, E>> pairs;
    auto addEquation = [&](const Value& v) {
        E q = toExpr(v);
        if (q->kind != cas::Kind::Eq) fail("subs: expected an equation like x == 2");
        pairs.push_back({q->args[0], q->args[1]});
    };
    if (argc == 3) {
        pairs.push_back({toExpr(args[1]), toExpr(args[2])});
    } else if (isList(args[1])) {
        for (const auto& v : as<ObjList>(args[1])->items) addEquation(v);
    } else {
        addEquation(args[1]);
    }
    return mapSym(args[0], [&](const E& e) {
        E r = e;
        for (const auto& p : pairs) r = cas::subs(r, p.first, p.second);
        return r;
    });
}

Value n_diff(VM& vm, int argc, Value* args) {
    try {
        E x = argc > 1 && isSym(args[1]) ? symbolArg(args[1], "diff") : E();
        E e = exprArg(vm, args[0], x ? &x : nullptr);
        if (!x) x = inferVariable(e, "diff");
        int64_t n = 1;
        if (argc > 2) n = toInt64(args[2], "a derivative order");
        else if (argc == 2 && !isSym(args[1])) n = toInt64(args[1], "a derivative order");
        if (n < 0 || n > 100) fail("diff: order must be between 0 and 100");
        for (int64_t k = 0; k < n; k++) e = cas::simplify(cas::diff(e, x));
        return finish(e);
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

Value n_integrate(VM& vm, int argc, Value* args) {
    try {
        E x = argc > 1 && isSym(args[1]) && cas::isSymbol(toExpr(args[1])) && !cas::isConstantSymbol(toExpr(args[1]))
                  ? toExpr(args[1])
                  : E();
        E e = exprArg(vm, args[0], x ? &x : nullptr);
        if (!x) x = inferVariable(e, "integrate");
        int boundsAt = (argc > 1 && isSym(args[1]) && toExpr(args[1]) == x) ? 2 : 1;
        if (argc == boundsAt) {
            E r = cas::integrate(e, x);
            if (!r) fail("integrate: no closed form found for ∫ " + cas::toString(e) + " d" + x->name);
            return fromExpr(r);
        }
        if (argc != boundsAt + 2) fail("integrate(f, x) or integrate(f, x, a, b)");
        E a = toExpr(args[boundsAt]), b = toExpr(args[boundsAt + 1]);
        E r;
        try {
            r = cas::integrateDefinite(e, x, a, b);
        } catch (const cas::CasError&) {
            r = nullptr;
        }
        if (r && cas::freeSymbols(r).empty()) {
            std::complex<double> z;
            if (cas::evalComplex(r, nullptr, z) && std::isfinite(z.real())) return finish(r);
        } else if (r) {
            return finish(r);
        }
        double da, db;
        Value va = args[boundsAt], vb = args[boundsAt + 1];
        auto num = [&](const Value& v, double& d) {
            if (isRealNumber(v)) {
                d = toDouble(v);
                return true;
            }
            return isSym(v) && symToDouble(v, d);
        };
        if (!num(va, da) || !num(vb, db)) fail("integrate: no closed form found and the bounds are not numeric");
        std::function<double(double)> f = cas::compileReal(e, x->name);
        return Value::real(integrateNumeric(f, da, db));
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

Value solutionsValue(const std::vector<E>& roots) {
    std::vector<Value> out;
    for (const auto& r : roots) out.push_back(fromExpr(r));
    return newSet(std::move(out));
}

Value n_solve(VM& vm, int argc, Value* args) {
    if (isMatrix(args[0])) {
        if (argc < 2) fail("solve(A, b) needs a right-hand side");
        return solveLinear(args[0], args[1]);
    }
    try {
        if (isList(args[0])) {
            std::vector<E> eqs, vars;
            for (const auto& v : as<ObjList>(args[0])->items) eqs.push_back(toExpr(v));
            if (argc > 1) {
                if (isList(args[1])) {
                    for (const auto& v : as<ObjList>(args[1])->items) vars.push_back(symbolArg(v, "solve"));
                } else {
                    for (int k = 1; k < argc; k++) vars.push_back(symbolArg(args[k], "solve"));
                }
            } else {
                std::vector<E> all;
                for (const auto& q : eqs)
                    for (const auto& s : cas::freeSymbols(q))
                        if (std::find(all.begin(), all.end(), s) == all.end()) all.push_back(s);
                std::sort(all.begin(), all.end(), [](const E& a, const E& b) { return a->name < b->name; });
                vars = all;
            }
            std::vector<cas::Solution> sols = cas::solveSystem(eqs, vars);
            std::vector<Value> out;
            for (const auto& s : sols) {
                std::vector<Value> eqsOut;
                for (const auto& p : s) eqsOut.push_back(newSym(cas::eq(p.first, p.second)));
                out.push_back(newList(std::move(eqsOut)));
            }
            if (out.size() == 1) return out[0];
            return newList(std::move(out));
        }
        E x = argc > 1 ? symbolArg(args[1], "solve") : E();
        E e = exprArg(vm, args[0], x ? &x : nullptr);
        if (!x) x = inferVariable(e, "solve");
        return solutionsValue(cas::solve(e, x));
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

Value n_taylor(VM& vm, int argc, Value* args) {
    try {
        E x = isSym(args[1]) && cas::isSymbol(toExpr(args[1])) && !cas::isConstantSymbol(toExpr(args[1]))
                  ? toExpr(args[1])
                  : E();
        int at = x ? 2 : 1;
        E e = exprArg(vm, args[0], x ? &x : nullptr);
        if (!x) x = inferVariable(e, "taylor");
        E a = at < argc ? toExpr(args[at]) : cas::zero();
        int64_t order = at + 1 < argc ? toInt64(args[at + 1], "an order") : 5;
        return fromExpr(cas::taylor(e, x, a, static_cast<int>(order)));
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

Value n_limit(VM& vm, int argc, Value* args) {
    try {
        E x = argc > 2 ? symbolArg(args[1], "limit") : E();
        E e = exprArg(vm, args[0], x ? &x : nullptr);
        if (!x) x = inferVariable(e, "limit");
        E a = toExpr(args[argc - 1]);
        return finish(cas::limit(e, x, a));
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

Value n_degree(VM&, int argc, Value* args) {
    if (!isSym(args[0])) return Value::integer(isZeroValue(args[0]) ? -1 : 0);
    E e = toExpr(args[0]);
    E x = argc > 1 ? symbolArg(args[1], "degree") : inferVariable(e, "degree");
    std::vector<E> c;
    if (!cas::toPolyE(e, x, c)) fail("degree: " + cas::toString(e) + " is not a polynomial in " + x->name);
    return Value::integer(static_cast<int64_t>(c.size()) - 1);
}

Value n_coeffs(VM&, int argc, Value* args) {
    E e = toExpr(args[0]);
    E x = argc > 1 ? symbolArg(args[1], "coeffs") : inferVariable(e, "coeffs");
    std::vector<E> c;
    if (!cas::toPolyE(e, x, c)) fail("coeffs: " + cas::toString(e) + " is not a polynomial in " + x->name);
    std::vector<Value> out;
    for (const auto& k : c) out.push_back(fromExpr(k));
    return newList(std::move(out));
}

Value n_polydiv(VM&, int argc, Value* args) {
    E a = toExpr(args[0]), b = toExpr(args[1]);
    E x = argc > 2 ? symbolArg(args[2], "polydiv") : inferVariable(cas::add(a, b), "polydiv");
    cas::Poly pa, pb, q, r;
    if (!cas::toPoly(a, x, pa) || !cas::toPoly(b, x, pb)) fail("polydiv needs polynomials with rational coefficients");
    try {
        cas::Poly::divMod(pa, pb, q, r);
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
    return newList({fromExpr(cas::fromPoly(q, x)), fromExpr(cas::fromPoly(r, x))});
}

Value n_polygcd(VM&, int argc, Value* args) {
    E a = toExpr(args[0]), b = toExpr(args[1]);
    E x = argc > 2 ? symbolArg(args[2], "polygcd") : inferVariable(cas::add(a, b), "polygcd");
    cas::Poly pa, pb;
    if (!cas::toPoly(a, x, pa) || !cas::toPoly(b, x, pb)) fail("polygcd needs polynomials with rational coefficients");
    return fromExpr(cas::fromPoly(cas::Poly::gcd(pa, pb), x));
}

Value n_lhs(VM&, int, Value* args) {
    E e = toExpr(args[0]);
    if (e->kind != cas::Kind::Eq) fail("lhs expects an equation");
    return fromExpr(e->args[0]);
}

Value n_rhs(VM&, int, Value* args) {
    E e = toExpr(args[0]);
    if (e->kind != cas::Kind::Eq) fail("rhs expects an equation");
    return fromExpr(e->args[1]);
}

Value n_symbols(VM&, int, Value* args) {
    std::vector<Value> out;
    if (isSym(args[0]))
        for (const auto& s : cas::freeSymbols(toExpr(args[0]))) out.push_back(newSym(s));
    return newList(std::move(out));
}

Value n_symbol(VM&, int, Value* args) {
    if (!isString(args[0])) fail("symbol expects a name string");
    return newSym(cas::sym(as<ObjString>(args[0])->s));
}

Value n_lambdify(VM& vm, int argc, Value* args) {
    // Evaluates an expression at a point: at(expr, x, value) or at(expr, value).
    (void)vm;
    E e = toExpr(args[0]);
    E x = argc > 2 ? symbolArg(args[1], "at") : inferVariable(e, "at");
    E v = toExpr(args[argc - 1]);
    try {
        return finish(cas::simplify(cas::subs(e, x, v)));
    } catch (const cas::CasError& err) {
        rethrow(err);
    }
}

cas::StepNode traceCall(VM& vm, const Value& f, Value& result) {
    if (!isCallable(f)) fail("expected an expression or a function of no arguments");
    cas::stepsBegin();
    try {
        result = vm.call(f, {});
        if (isCallable(result)) result = vm.call(result, {});
    } catch (...) {
        cas::stepsEnd();
        throw;
    }
    return cas::stepsEnd();
}

std::string latexOf(const Value& result, const std::string& text) {
    std::string math = isSym(result) ? cas::toLatex(as<ObjSym>(result)->e) : toString(result);
    return "\\begin{verbatim}\n" + text + "\\end{verbatim}\n\\[" + math + "\\]";
}

Value n_steps(VM& vm, int argc, Value* args) {
    (void)argc;
    Value result;
    cas::StepNode root = traceCall(vm, args[0], result);
    if (root.kids.empty())
        *vm.out << "(no step-by-step working for this computation)\n";
    else
        *vm.out << cas::renderSteps(root);
    return result;
}

Value n_working(VM& vm, int argc, Value* args) {
    (void)argc;
    Value result;
    cas::StepNode root = traceCall(vm, args[0], result);
    std::string text = root.kids.empty() ? "(no step-by-step working for this computation)" : cas::renderSteps(root);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    Value rec = newRecord();
    recordSet(rec, newString("result"), result);
    recordSet(rec, newString("text"), newString(text));
    recordSet(rec, newString("latex"), newString(latexOf(result, text)));
    return rec;
}

}  // namespace

void registerCas(VM& vm) {
    const char* c = "symbolic";
    vm.defineNative("simplify", 1, 1, n_simplify, "simplify(e) - simplest equivalent form found", c);
    vm.defineNative("expand", 1, 1, n_expand, "expand(e) - multiply out products and powers", c);
    vm.defineNative("factor", 1, 1, n_factor, "factor(p) - factor a polynomial over the rationals", c);
    vm.defineNative("together", 1, 1, n_together, "together(e) - combine into a single fraction", c);
    vm.defineNative("cancel", 1, 1, n_cancel, "cancel(e) - cancel common polynomial factors", c);
    vm.defineNative("apart", 1, 2, n_apart, "apart(e, x) - partial fraction decomposition", c);
    vm.defineNative("subs", 2, 3, n_subs, "subs(e, x, v) or subs(e, x == v) - substitute", c);
    vm.defineNative("diff", 1, 3, n_diff, "diff(e, x, n) - n-th derivative with respect to x", c);
    vm.defineNative("integrate", 1, 4, n_integrate, "integrate(e, x) - antiderivative; integrate(e, x, a, b)", c);
    vm.defineNative("solve", 1, -1, n_solve,
                    "solve(eq, x) - set of solutions; solve([eqs], [vars]) - systems; solve(A, b) - linear", c);
    vm.defineNative("taylor", 2, 4, n_taylor, "taylor(e, x, a, n) - Taylor polynomial of order n around a", c);
    vm.defineNative("series", 2, 4, n_taylor, "series(e, x, a, n) - same as taylor", c);
    vm.defineNative("limit", 2, 3, n_limit, "limit(e, x, a) - limit as x -> a (a may be ±∞)", c);
    vm.defineNative("degree", 1, 2, n_degree, "degree(p, x) - polynomial degree", c);
    vm.defineNative("coeffs", 1, 2, n_coeffs, "coeffs(p, x) - [c0, c1, ...] lowest degree first", c);
    vm.defineNative("polydiv", 2, 3, n_polydiv, "polydiv(a, b, x) - [quotient, remainder]", c);
    vm.defineNative("polygcd", 2, 3, n_polygcd, "polygcd(a, b, x) - monic polynomial gcd", c);
    vm.defineNative("lhs", 1, 1, n_lhs, "lhs(eq) - left-hand side", c);
    vm.defineNative("rhs", 1, 1, n_rhs, "rhs(eq) - right-hand side", c);
    vm.defineNative("symbols", 1, 1, n_symbols, "symbols(e) - free symbols of e", c);
    vm.defineNative("symbol", 1, 1, n_symbol, "symbol(name) - create a symbol from a string", c);
    vm.defineNative("at", 2, 3, n_lambdify, "at(e, x, v) - value of e at x = v", c);
    vm.defineNative("steps", 1, 1, n_steps,
                    "steps(expr) - evaluate expr, printing the working of diff, integrate, solve and limit", c);
    vm.defineNative("working", 1, 1, n_working,
                    "working(expr) - like steps, but returns {result, text, latex} instead of printing", c);
}
